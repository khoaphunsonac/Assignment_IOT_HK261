// ota.cpp - xem ota.h (luồng 4 bước).
// Nguồn: API Arduino core 2.0.17 (Update, HTTPClient, Preferences) và ESP-IDF 4.4
// (esp_ota_ops.h, mbedtls), tên hàm đối chiếu mã nguồn core 2.0.17 trên GitHub (Update.h,
// HTTPClient.h, Preferences.h, mbedtls/sha256.h). Không có code mẫu của thầy; OTA ở lab 10/11 (B5) dùng ArduinoOTA/espota, KHÁC cách này (espota đẩy từ
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
// Tắt tự xác nhận của Arduino
// =====================================================================================
// Arduino core 2.0.17 (cores/esp32/esp32-hal-misc.c, hàm initArduino) chạy TRƯỚC setup():
// nếu bản đang chạy ở trạng thái "chờ xác nhận" thì nó TỰ đánh dấu bản này tốt ngay, trừ
// khi verifyRollbackLater() trả true. Hàm gốc là `weak` trả false; định nghĩa lại ở đây
// (trả true) thì Arduino để việc xác nhận cho boot_check() làm, sau khi nối được MQTT.
// Bắt buộc `extern "C"`: hàm gốc nằm trong file .c, tên ký hiệu kiểu C. Thiếu extern "C"
// thì C++ đổi tên (name mangling), linker không thay được -> rollback không bao giờ xảy ra.
extern "C" bool verifyRollbackLater() { return true; }

bool ota_busy() { return s_busy; }

// send_status: tạo JSON room1/ota/status rồi nhờ task_net gửi.
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

// 3 hàm NVS cho key "pending" (version vừa OTA, chờ biết kết quả sau khi khởi động lại).
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

// fail: dọn dẹp khi lỗi giữa chừng rồi trả lại mã lỗi, để viết gọn 1 dòng:
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
// ota_on_mqtt: chạy trong task_net (callback MQTT), phải NHANH, không chặn
//   Lệnh hỏng / trùng bản / đang bận: chỉ in Serial rồi bỏ (CONTRACT 9 không có mã lỗi cho
//   lệnh hỏng, và lệnh hỏng có thể không có version để gửi status).
//   Lệnh hợp lệ: chép vào s_cmd, đặt s_busy = true RỒI MỚI đánh thức task_ota. Từ lúc
//   s_busy = true, hàm này không ghi s_cmd nữa (ca BUSY), nên task_ota đọc s_cmd an toàn
//   mà không cần mutex.
// =====================================================================================
void ota_on_mqtt(const uint8_t *payload, unsigned int len) {
  if (s_task == NULL) return;
  OtaCmd c;
  OtaCmdResult r = ota_cmd_parse((const char *)payload, (size_t)len, &c);
  if (r != OTA_CMD_OK) {
    Serial.printf("[OTA] Bo lenh hong (%s)\n",
                  r == OTA_CMD_BAD_JSON ? "bad_json" : "bad_field");
    return;
  }
  OtaDecision d = ota_decide(&c, FW_VERSION, s_busy);
  if (d == OTA_DECIDE_BUSY) {
    Serial.printf("[OTA] Dang OTA, bo lenh %s\n", c.version);
    return;
  }
  if (d == OTA_DECIDE_SAME_VERSION) {
    Serial.printf("[OTA] Trung ban dang chay %s, bo qua\n", c.version);
    return;
  }
  s_cmd = c;
  s_busy = true;
  Serial.printf("[OTA] Nhan lenh %s -> %s (%lu byte)\n",
                FW_VERSION, c.version, (unsigned long)c.size);
  xTaskNotifyGive(s_task);
}

// =====================================================================================
// download_and_flash: tải firmware, vừa ghi flash vừa tính sha256 (chạy trong task_ota)
//   Vào : c = lệnh đã kiểm.
//   Ra  : OTA_ERR_NONE nếu tải đủ, sha256 đúng và Update.end() OK (phân vùng mới đã thành
//         phân vùng khởi động, CHƯA restart). Lỗi thì trả mã lỗi, mọi thứ đã dọn qua fail().
//   Các bước (1)-(9) ghi trong thân hàm. Điểm chính:
//   - Size server trả (Content-Length) phải bằng size trong lệnh: khác là sai file, dừng sớm.
//   - Mỗi khối đọc tối đa 1 KB và không vượt quá phần còn lại (size - got); sha256 và
//     Update.write cùng dùng đúng n byte vừa đọc (khối cuối thường < 1 KB).
//   - 10 s không có byte mới, hoặc server đóng kết nối giữa chừng -> download_failed.
//   - Sai sha256 thì KHÔNG gọi Update.end() (end() mới đổi phân vùng khởi động); fail() gọi
//     Update.abort() nên board vẫn chạy bản cũ.
// =====================================================================================
static OtaError download_and_flash(const OtaCmd *c) {
  // (1) Mở kết nối HTTP và gửi GET
  HTTPClient http;
  http.setTimeout(OTA_HTTP_TIMEOUT_MS);
  if (!http.begin(c->url)) {
    Serial.println("[OTA] LOI http.begin");
    return fail(OTA_ERR_DOWNLOAD_FAILED, NULL, NULL);
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[OTA] LOI HTTP code=%d\n", code);
    return fail(OTA_ERR_DOWNLOAD_FAILED, &http, NULL);
  }

  // (2) Kiểm tra Content-Length khớp với size trong lệnh
  int len = http.getSize();
  if (len < 0 || (uint32_t)len != c->size) {
    Serial.printf("[OTA] LOI kich thuoc: server=%d lenh=%lu\n", len, (unsigned long)c->size);
    return fail(OTA_ERR_DOWNLOAD_FAILED, &http, NULL);
  }

  // (3) Chuẩn bị phân vùng flash OTA
  if (!Update.begin(c->size)) {
    Serial.printf("[OTA] LOI Update.begin: %s\n", Update.errorString());
    return fail(OTA_ERR_WRITE_FAILED, &http, NULL);
  }

  // (4) Khởi tạo ngữ cảnh SHA-256
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts_ret(&sha, 0);

  // (5) Khởi tạo biến theo dõi tiến độ và gửi mốc 0%
  WiFiClient *st = http.getStreamPtr();
  if (st == NULL) {
    return fail(OTA_ERR_DOWNLOAD_FAILED, &http, &sha);
  }
  uint32_t got = 0;
  int last = -1;
  uint32_t last_data = millis();
  int m0 = ota_progress_next(0, c->size, last);
  if (m0 >= 0) {
    send_status(c->version, OTA_ST_DOWNLOADING, m0, OTA_ERR_NONE);
    last = m0;
  }

  // (6) Vòng lặp tải từng khối, tính SHA-256 và ghi flash
  while (got < c->size) {
    int avail = st->available();
    if (avail <= 0) {
      if (!http.connected() || (uint32_t)(millis() - last_data) > OTA_NO_DATA_MS) {
        Serial.printf("[OTA] Dut ket noi o %lu/%lu\n",
                      (unsigned long)got, (unsigned long)c->size);
        return fail(OTA_ERR_DOWNLOAD_FAILED, &http, &sha);
      }
      vTaskDelay(1);
      continue;
    }

    size_t want = (size_t)avail;
    if (want > sizeof(s_buf)) want = sizeof(s_buf);
    if (want > (size_t)(c->size - got)) want = (size_t)(c->size - got);

    size_t n = st->readBytes(s_buf, want);
    if (n == 0) {
      vTaskDelay(1);
      continue;
    }

    last_data = millis();
    mbedtls_sha256_update_ret(&sha, s_buf, n);
    if (Update.write(s_buf, n) != n) {
      Serial.printf("[OTA] LOI Update.write: %s\n", Update.errorString());
      return fail(OTA_ERR_WRITE_FAILED, &http, &sha);
    }

    got += (uint32_t)n;
    int m = ota_progress_next(got, c->size, last);
    if (m >= 0) {
      send_status(c->version, OTA_ST_DOWNLOADING, m, OTA_ERR_NONE);
      last = m;
    }
  }

  // (7) Đóng HTTP và báo trạng thái đang kiểm tra checksum
  http.end();
  send_status(c->version, OTA_ST_VERIFYING, 0, OTA_ERR_NONE);

  // (8) Hoàn tất SHA-256 và đối chiếu với lệnh
  uint8_t digest32[32];
  char hex65[OTA_SHA_HEX_LEN + 1];
  mbedtls_sha256_finish_ret(&sha, digest32);
  mbedtls_sha256_free(&sha);
  ota_digest_to_hex(digest32, hex65);

  if (strcmp(hex65, c->sha256) != 0) {
    Serial.printf("[OTA] LOI sha256: tinh=%s lenh=%s\n", hex65, c->sha256);
    return fail(OTA_ERR_CHECKSUM_MISMATCH, NULL, NULL);
  }

  // (9) Chốt ghi flash và chuyển phân vùng khởi động
  if (!Update.end()) {
    Serial.printf("[OTA] LOI Update.end: %s\n", Update.errorString());
    return fail(OTA_ERR_WRITE_FAILED, NULL, NULL);
  }
  return OTA_ERR_NONE;
}

// =====================================================================================
// boot_check: task_ota gọi 1 lần lúc khởi động, xử lý 4 ca của ota_boot_case
//   NORMAL        : không làm gì.
//   VERIFY_NEW    : 500 ms hỏi 1 lần, trong 60 s nối được WiFi + MQTT thì đánh dấu bản mới
//                   tốt + gửi success; quá 60 s thì yêu cầu bootloader quay về bản cũ và
//                   khởi động lại (hàm đó không quay về; nếu quay về tức không có bản cũ
//                   hợp lệ, đành giữ bản hiện tại).
//   REPORT_SUCCESS: đã xác nhận từ trước nhưng chưa kịp gửi success -> gửi lại.
//   ROLLED_BACK   : gửi failed + rolled_back với version = pending (bản HỎNG, không phải
//                   FW_VERSION đang chạy).
//   Chờ ở task_ota, không chờ trong setup(): setup() phải xong để task_net được tạo và nối
//   MQTT; chờ trong setup() thì không bao giờ nối được, bản mới luôn bị quay về.
// =====================================================================================
static void boot_check() {
  char pending[OTA_VERSION_MAX];
  pending_load(pending, sizeof(pending));
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t st;
  bool pv = (running != NULL) &&
            (esp_ota_get_state_partition(running, &st) == ESP_OK) &&
            (st == ESP_OTA_IMG_PENDING_VERIFY);
  Serial.printf("[OTA] Khoi dong: fw=%s phan_vung=%s pending='%s' cho_xac_nhan=%d\n",
                FW_VERSION, running ? running->label : "?", pending, pv);

  switch (ota_boot_case(pending, FW_VERSION, pv)) {
    case OTA_BOOT_NORMAL:
      return;

    case OTA_BOOT_VERIFY_NEW: {
      uint32_t start = millis();
      while (1) {
        OtaConfirm r = ota_confirm_step(millis(), start, WiFi.status() == WL_CONNECTED,
                                        net_mqtt_connected(), OTA_CONFIRM_LIMIT_MS);
        if (r == OTA_CONFIRM_OK) {
          esp_ota_mark_app_valid_cancel_rollback();
          send_status(FW_VERSION, OTA_ST_SUCCESS, 0, OTA_ERR_NONE);
          pending_clear();
          Serial.printf("[OTA] Ban %s OK sau %lu ms\n",
                        FW_VERSION, (unsigned long)(millis() - start));
          return;
        }
        if (r == OTA_CONFIRM_ROLLBACK) {
          Serial.println("[OTA] 60 s chua noi duoc WiFi+MQTT -> quay ve ban cu");
          esp_ota_mark_app_invalid_rollback_and_reboot();
          Serial.println("[OTA] LOI khong co ban cu hop le de rollback, giu ban hien tai");
          esp_ota_mark_app_valid_cancel_rollback();
          pending_clear();
          return;
        }
        vTaskDelay(OTA_CONFIRM_POLL_MS);
      }
    }

    case OTA_BOOT_REPORT_SUCCESS:
      send_status(FW_VERSION, OTA_ST_SUCCESS, 0, OTA_ERR_NONE);
      pending_clear();
      break;

    case OTA_BOOT_ROLLED_BACK:
      send_status(pending, OTA_ST_FAILED, 0, OTA_ERR_ROLLED_BACK);
      pending_clear();
      break;
  }
}

// task_ota: kiểm lúc khởi động, rồi ngủ chờ lệnh (ulTaskNotifyTake chặn tới khi
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

// ota_start: xem ota.h.
void ota_start() {
  xTaskCreate(task_ota, "task_ota", OTA_TASK_STACK, NULL, OTA_TASK_PRIO, &s_task);
}