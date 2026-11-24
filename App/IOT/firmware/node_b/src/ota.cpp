// ota.cpp - xem ota.h (luồng 4 bước). TODO 11–13 của Minh.
// Nguồn: Claude viết theo API Arduino core 2.0.17 (Update, HTTPClient, Preferences) và
// ESP-IDF 4.4 (esp_ota_ops.h, mbedtls). Đã đối chiếu tên hàm với mã nguồn core 2.0.17
// trên GitHub (Update.h, HTTPClient.h, Preferences.h, mbedtls/sha256.h). Không có code mẫu
// của thầy; OTA ở lab 10/11 (B5) dùng ArduinoOTA/espota, KHÁC cách này (espota đẩy từ
// PlatformIO, không qua MQTT, không kiểm sha256, không rollback).
#include "ota.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>

#include "esp_ota_ops.h"     // esp_ota_get_running_partition, mark_app_valid/invalid
#include "mbedtls/sha256.h"  // tính sha256 từng khối (có sẵn trong core, không cần lib_deps)
#include "net.h"
#include "ota_logic.h"
#include "version.h"

#define OTA_TASK_STACK 8192     // HTTPClient + WiFiClient + mbedtls ctx; bộ đệm 1 KB để static
#define OTA_TASK_PRIO 1         // thấp hơn task_espnow (3), task_net (2): tải chậm chút không sao
#define OTA_CHUNK 1024          // mỗi lần đọc/ghi 1 KB
#define OTA_HTTP_TIMEOUT_MS 10000
#define OTA_NO_DATA_MS 10000    // 10 s không nhận thêm byte nào -> coi là đứt
#define OTA_CONFIRM_POLL_MS 500
#define OTA_REBOOT_DELAY_MS 1500  // chờ task_net kịp gửi "rebooting" rồi mới restart

static const char *T_OTA_STATUS = "room1/ota/status";
static const char *NVS_NS = "ota";        // namespace riêng, không đụng "cfg" của config_store
static const char *NVS_KEY = "pending";

static TaskHandle_t s_task = NULL;
static OtaCmd s_cmd;              // lệnh đang chạy. Chỉ ota_on_mqtt ghi, và CHỈ khi !s_busy
static volatile bool s_busy = false;
static uint8_t s_buf[OTA_CHUNK];  // static: không chiếm 1 KB stack của task_ota

// =====================================================================================
// VIẾT SẴN: tắt tự xác nhận của Arduino
// =====================================================================================
// Arduino core 2.0.17 (cores/esp32/esp32-hal-misc.c, hàm initArduino) chạy TRƯỚC setup():
// nếu bản đang chạy ở trạng thái "chờ xác nhận" thì nó TỰ đánh dấu bản này tốt ngay, trừ
// khi verifyRollbackLater() trả true. Hàm gốc là `weak` trả false; định nghĩa lại ở đây
// (trả true) thì Arduino để việc xác nhận cho mình làm (TODO 13, sau khi nối được MQTT).
// Bắt buộc `extern "C"`: hàm gốc nằm trong file .c, tên ký hiệu kiểu C. Thiếu extern "C"
// thì C++ đổi tên (name mangling), linker không thay được -> rollback không bao giờ xảy ra.
extern "C" bool verifyRollbackLater() { return true; }

bool ota_busy() { return s_busy; }

// VIẾT SẴN. send_status: tạo JSON room1/ota/status rồi nhờ task_net gửi.
//   Vào : như ota_status_build (version = bản ĐANG OTA).
//   Làm : in Serial (để chụp log cho PR), cất vào hàng đợi net_publish_later.
//   Gọi từ task_ota được (không gọi PubSubClient trực tiếp).
static void send_status(const char *version, OtaState st, int progress, OtaError err) {
  char buf[128];
  size_t n = ota_status_build(version, st, progress, err, buf, sizeof(buf));
  if (n == 0) return;
  Serial.printf("[OTA] %s\n", buf);
  if (!net_publish_later(T_OTA_STATUS, buf)) Serial.println("[OTA] LOI hang doi publish day");
}

// VIẾT SẴN. 3 hàm NVS cho key "pending" (version vừa OTA, chờ biết kết quả sau khi khởi động lại).
// Preferences = thư viện NVS của Arduino (config_store #42 cũng dùng).
//   pending_load : đọc ra out ("" nếu chưa có). begin(..., true) = chỉ đọc; namespace chưa
//                  tồn tại thì begin trả false -> coi như "". isKey để khỏi in lỗi NOT_FOUND.
//   pending_save : ghi version.
//   pending_clear: xóa key (đã báo kết quả xong).
static void pending_load(char *out, size_t cap) {
  Preferences p;
  out[0] = '\0';
  if (!p.begin(NVS_NS, true)) return;
  if (p.isKey(NVS_KEY)) p.getString(NVS_KEY, out, cap);
  p.end();
}
static void pending_save(const char *version) {
  Preferences p;
  if (!p.begin(NVS_NS, false)) return;
  p.putString(NVS_KEY, version);
  p.end();
}
static void pending_clear() {
  Preferences p;
  if (!p.begin(NVS_NS, false)) return;
  p.remove(NVS_KEY);
  p.end();
}

// VIẾT SẴN. fail: dọn dẹp khi lỗi giữa chừng rồi trả lại mã lỗi, để viết gọn 1 dòng:
//     return fail(OTA_ERR_DOWNLOAD_FAILED, &http, &sha);
//   Vào : e = mã lỗi trả về; http, sha = cái nào ĐÃ mở thì truyền vào, chưa mở thì NULL.
//   Làm : Update đang ghi dở -> Update.abort() (phân vùng mới KHÔNG được đặt làm phân vùng
//         khởi động; Update chỉ ghi 16 byte đầu ảnh lúc end() thành công, nên ảnh dở dang
//         không bao giờ khởi động được); giải phóng sha; đóng HTTP.
static OtaError fail(OtaError e, HTTPClient *http, mbedtls_sha256_context *sha) {
  if (Update.isRunning()) Update.abort();
  if (sha) mbedtls_sha256_free(sha);
  if (http) http->end();
  return e;
}

// =====================================================================================
// TODO 11: ota_on_mqtt (chạy trong task_net, phải NHANH, không chặn)
// =====================================================================================
// Khung:
//   if (s_task == NULL) return;          // (có sẵn) task_ota chưa tạo xong
//   OtaCmd c;
//   r = ota_cmd_parse(...)               // điền: ép kiểu payload sang (const char *)
//   ca r != OTA_CMD_OK      : điền: Serial "[OTA] Bo lenh hong (bad_json/bad_field)", return
//   d = ota_decide(&c, FW_VERSION, s_busy)
//   ca BUSY                 : điền: Serial "[OTA] Dang OTA, bo lenh <version>", return
//   ca SAME_VERSION         : điền: Serial "[OTA] Trung ban dang chay <version>, bo qua", return
//   ca START                : điền: s_cmd = c; s_busy = true; Serial "[OTA] Nhan lenh 1.0.0 -> 1.0.1 (702157 byte)";
//                                   xTaskNotifyGive(s_task)   // đánh thức task_ota
// Vì sao s_busy = true TRƯỚC khi đánh thức: từ lúc này ota_on_mqtt không ghi s_cmd nữa
// (ca BUSY), nên task_ota đọc s_cmd an toàn, không cần mutex.
// Vì sao lệnh hỏng không gửi status: CONTRACT 9 không có mã lỗi cho lệnh hỏng; status
// cần version mà lệnh hỏng có thể không có version.
void ota_on_mqtt(const uint8_t *payload, unsigned int len) {
  if (s_task == NULL) return;
  (void)payload;
  (void)len;
}

// =====================================================================================
// TODO 12: download_and_flash (chạy trong task_ota)
// =====================================================================================
//   Vào : c = lệnh đã kiểm.
//   Ra  : OTA_ERR_NONE nếu tải đủ, sha256 đúng, Update.end() OK (phân vùng mới đã được đặt
//         làm phân vùng khởi động, CHƯA restart). Còn lại trả mã lỗi (đã dọn bằng fail()).
//   Gửi status: "downloading" theo ota_progress_next, "verifying" sau khi tải xong.
//
// API cần dùng (đúng tên trong core 2.0.17):
//   HTTPClient http; http.setTimeout(OTA_HTTP_TIMEOUT_MS); http.begin(c->url) -> bool
//   int code = http.GET();          200 = HTTP_CODE_OK
//   int len = http.getSize();       Content-Length; python http.server có gửi
//   WiFiClient *st = http.getStreamPtr();  st->available(), st->readBytes(buf, n)
//   http.connected(), http.end()
//   Update.begin(size) -> bool; Update.write(buf, n) -> số byte đã ghi; Update.end() -> bool;
//   Update.errorString() -> chuỗi lỗi để in.
//   mbedtls_sha256_context sha; mbedtls_sha256_init(&sha); mbedtls_sha256_starts_ret(&sha, 0);
//   mbedtls_sha256_update_ret(&sha, buf, n); mbedtls_sha256_finish_ret(&sha, digest32);
//   mbedtls_sha256_free(&sha);      (0 trong starts = SHA-256, 1 = SHA-224)
//   ota_digest_to_hex(digest32, hex65) rồi strcmp với c->sha256 (đều chữ thường).
//
// Khung (mỗi "điền" khoảng 1–4 dòng):
//   (1) http.begin, GET: điền: begin lỗi hoặc code != 200 -> in code, return fail(DOWNLOAD_FAILED, ...)
//   (2) điền: http.getSize() != c->size -> in cả 2 số, return fail(DOWNLOAD_FAILED, ...)
//       (lệnh nói 702157 mà server trả file khác cỡ = sai file, dừng sớm khỏi tải phí)
//   (3) điền: Update.begin(c->size) lỗi -> in Update.errorString(), return fail(WRITE_FAILED, ...)
//   (4) điền: init + starts sha
//   (5) got = 0, last = -1, last_data = millis(); gửi mốc 0% (ota_progress_next(0, ...))
//   (6) while (got < c->size):
//         avail = st->available()
//         ca avail == 0: điền: !http.connected() HOẶC millis() - last_data > OTA_NO_DATA_MS
//                        -> in "Dut ket noi o got/size", return fail(DOWNLOAD_FAILED, &http, &sha);
//                        còn lại vTaskDelay(1); continue;   (nhường CPU, chờ thêm dữ liệu)
//         ca có dữ liệu: điền: want = min(avail, sizeof(s_buf), c->size - got)
//                        n = st->readBytes(s_buf, want); n <= 0 -> continue
//                        last_data = millis(); cập nhật sha với n byte
//                        Update.write(s_buf, n) != n -> return fail(WRITE_FAILED, &http, &sha)
//                        got += n; m = ota_progress_next(got, c->size, last); m >= 0 -> send_status + last = m
//   (7) http.end(); send_status VERIFYING
//   (8) điền: finish + free sha, đổi hex, so với c->sha256:
//         khác -> in cả 2 chuỗi (log này là minh chứng ca checksum_mismatch trong PR),
//                 return fail(CHECKSUM_MISMATCH, NULL, NULL)   (Update.abort bên trong fail)
//   (9) điền: Update.end() lỗi -> return fail(WRITE_FAILED, NULL, NULL); OK -> return OTA_ERR_NONE
// Bẫy:
//   - Cập nhật sha TRƯỚC hoặc SAU Update.write đều được, nhưng phải đúng n byte vừa đọc
//     (không phải sizeof(s_buf)): khối cuối thường < 1024.
//   - Đừng đọc quá c->size (want <= c->size - got): server gửi thừa thì Update.write lỗi.
//   - Sai sha256 thì KHÔNG được gọi Update.end() (end() mới đặt phân vùng khởi động).
static OtaError download_and_flash(const OtaCmd *c) {
  (void)c;
  return OTA_ERR_DOWNLOAD_FAILED;  // sửa khi làm
}

// =====================================================================================
// TODO 13: boot_check (task_ota gọi 1 lần lúc khởi động)
// =====================================================================================
// Khung:
//   char pending[OTA_VERSION_MAX]; pending_load(pending, sizeof(pending));
//   const esp_partition_t *running = esp_ota_get_running_partition();
//   esp_ota_img_states_t st;
//   bool pv = esp_ota_get_state_partition(running, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
//   Serial.printf("[OTA] Khoi dong: fw=%s phan_vung=%s pending='%s' cho_xac_nhan=%d\n",
//                 FW_VERSION, running->label, pending, pv);   // log này cần cho PR
//   switch (ota_boot_case(pending, FW_VERSION, pv)):
//     NORMAL        : return
//     VERIFY_NEW    : start = millis(); lặp:
//                       r = ota_confirm_step(millis(), start, WiFi.status() == WL_CONNECTED,
//                                            net_mqtt_connected(), OTA_CONFIRM_LIMIT_MS)
//                       ca OK      : điền: esp_ota_mark_app_valid_cancel_rollback();
//                                          send_status(FW_VERSION, OTA_ST_SUCCESS, ...); pending_clear();
//                                          in "Ban 1.0.1 OK sau N ms"; return
//                       ca ROLLBACK: điền: in "60 s chua noi duoc WiFi+MQTT -> quay ve ban cu";
//                                          esp_ota_mark_app_invalid_rollback_and_reboot();
//                                          // hàm trên khởi động lại, KHÔNG quay về đây. Nếu quay về
//                                          // tức là không có bản cũ hợp lệ: in lỗi,
//                                          // esp_ota_mark_app_valid_cancel_rollback(); pending_clear(); return
//                       ca WAIT    : vTaskDelay(OTA_CONFIRM_POLL_MS)
//     REPORT_SUCCESS: điền: send_status(FW_VERSION, SUCCESS); pending_clear()
//     ROLLED_BACK   : điền: send_status(pending, FAILED, 0, OTA_ERR_ROLLED_BACK); pending_clear()
//                     (version = pending, tức bản HỎNG, không phải FW_VERSION)
// Vì sao chờ ở task_ota mà không chờ trong setup(): setup() phải chạy xong để task_net
// được tạo và nối MQTT; chờ trong setup() thì không bao giờ nối được -> luôn quay về.
static void boot_check() {
}

// VIẾT SẴN. task_ota: kiểm lúc khởi động, rồi ngủ chờ lệnh (ulTaskNotifyTake chặn tới khi
// ota_on_mqtt gọi xTaskNotifyGive, không tốn CPU).
//   Tải + ghi OK : ghi NVS pending -> gửi "rebooting" -> chờ 1,5 s cho task_net kịp gửi -> restart.
//                  Ghi NVS TRƯỚC khi restart: bản mới (hoặc bản cũ nếu bị quay về) cần biết
//                  "vừa OTA bản nào" để báo success / rolled_back.
//   Lỗi          : gửi failed + mã lỗi, s_busy = false để nhận lệnh sau.
static void task_ota(void *pvParameters) {
  (void)pvParameters;
  boot_check();
  while (1) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    uint32_t t0 = millis();
    OtaError e = download_and_flash(&s_cmd);
    if (e == OTA_ERR_NONE) {
      pending_save(s_cmd.version);
      send_status(s_cmd.version, OTA_ST_REBOOTING, 0, OTA_ERR_NONE);
      Serial.printf("[OTA] Ghi xong sau %lu ms, khoi dong lai\n", (unsigned long)(millis() - t0));
      vTaskDelay(OTA_REBOOT_DELAY_MS);
      ESP.restart();
    }
    send_status(s_cmd.version, OTA_ST_FAILED, 0, e);
    s_busy = false;
  }
}

// VIẾT SẴN. Xem ota.h.
void ota_start() {
  xTaskCreate(task_ota, "task_ota", OTA_TASK_STACK, NULL, OTA_TASK_PRIO, &s_task);
}
