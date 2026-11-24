// ota_logic: phần logic "thuần" của OTA trên Node B (#58, T14).
//
// Vì sao tách thư viện riêng (giống node_b_logic ở #42):
//   - KHÔNG gọi Arduino/FreeRTOS/WiFi/HTTP/Update, KHÔNG tự đọc millis(). Thời gian luôn
//     truyền vào bằng tham số. Nhờ vậy chạy được trên PC: `pio test -e native -f test_ota`.
//   - Chỉ dùng ArduinoJson (đọc lệnh room1/ota, tạo JSON room1/ota/status).
//   - src/ota.cpp chỉ còn "nối dây": nhận lệnh MQTT -> gọi hàm ở đây để kiểm -> tải HTTP,
//     ghi flash (Update), tính sha256 -> gọi hàm ở đây để biết lúc nào gửi status.
//   - Tách thư viện riêng (không gộp vào node_b_logic) để PR #58 không đụng file của #42,
//     và Khoa (#59, #60) đang sửa src/ cũng không bị conflict.
//
// Nguồn: CONTRACT mục 9 (room1/ota, room1/ota/status), TASKS_W3 mục 3 (#58).
// Không có code mẫu của thầy cho phần này.
//
// Mẹo thời gian (giống #26, #42): luôn so `now_ms - moc_cu` (uint32_t) với khoảng thời
// gian, KHÔNG so `now_ms >= moc_cu + khoang`. Phép trừ không dấu vẫn đúng khi millis()
// tràn sau ~49,7 ngày.
#pragma once
#include <stddef.h>
#include <stdint.h>

// Giới hạn độ dài (tính cả '\0' cuối chuỗi).
#define OTA_VERSION_MAX 16   // "999.999.999" dài 11 ký tự, dư chỗ
#define OTA_URL_MAX 160      // "http://10.199.40.112:8000/fw/node_b_1.0.1.bin" ~45 ký tự
#define OTA_SHA_HEX_LEN 64   // sha256 = 32 byte = 64 ký tự hex
// Kích thước tối đa 1 file firmware: phân vùng app0/app1 trong default_8MB.csv
// (board yolo_uno.json) là 0x330000 byte = 3.342.336 byte. Firmware hiện ~0,8 MB.
#define OTA_MAX_FW_SIZE 0x330000UL

// =====================================================================================
// 1. Phiên bản "MAJOR.MINOR.PATCH" (CONTRACT mục 5: fw_version dạng "1.0.0")
// =====================================================================================
typedef struct {
  uint16_t major;
  uint16_t minor;
  uint16_t patch;
} OtaVersion;

// ota_version_parse: đọc chuỗi phiên bản.
//   Vào : s   = chuỗi C (có thể NULL).
//         out = nơi ghi kết quả (chỉ ghi khi hợp lệ).
//   Ra  : true nếu s đúng dạng ĐÚNG 3 số nguyên 0..999, ngăn bởi đúng 2 dấu '.',
//         không có ký tự nào khác (không dấu cách, không "v" đầu, không dấu '-').
//   Ví dụ:
//     "1.0.1"    -> true, {1,0,1}        "10.20.300" -> true, {10,20,300}
//     "1.0"      -> false (thiếu số)     "1.0.1.2"   -> false (thừa số)
//     "1..1"     -> false (số rỗng)      "1.0.x"     -> false (không phải số)
//     " 1.0.1"   -> false                "v1.0.1"    -> false
//     "1.0.1000" -> false (quá 999)      ""  / NULL  -> false
//   Vì sao tự viết, không dùng sscanf("%d.%d.%d"): sscanf chấp nhận " 1.0.1", "1.0.1abc",
//   "-1.0.1" mà không báo lỗi. Tự duyệt từng ký tự thì kiểm được hết.
bool ota_version_parse(const char *s, OtaVersion *out);

// =====================================================================================
// 2. Chuỗi sha256 hex
// =====================================================================================
// ota_sha256_normalize: kiểm chuỗi sha256 và đổi về chữ thường.
//   Vào : in  = chuỗi C (có thể NULL).
//         out = mảng ít nhất 65 byte (64 ký tự + '\0'). Chỉ ghi khi hợp lệ.
//   Ra  : true nếu in dài ĐÚNG 64 ký tự và mọi ký tự là 0-9, a-f, A-F.
//         Khi true: out = in nhưng 'A'..'F' đổi thành 'a'..'f'.
//   Vì sao nhận cả chữ HOA: CONTRACT 9 quy định chữ thường, nhưng lúc test tay ông lấy
//   sha256 bằng PowerShell `Get-FileHash` (ra chữ HOA). Nhận cả hai cho đỡ lỗi vặt; so sánh
//   bên trong luôn dùng chữ thường.
bool ota_sha256_normalize(const char *in, char out[OTA_SHA_HEX_LEN + 1]);

// ota_digest_to_hex: đổi 32 byte sha256 (mbedtls trả ra) thành 64 ký tự hex thường.
//   Vào : d = 32 byte. out = mảng >= 65 byte.
//   Ra  : out = "9f2c...e81a" + '\0'.
//   Ví dụ: d = {0x00, 0xAB, 0x0F, ...} -> out = "00ab0f..."
void ota_digest_to_hex(const uint8_t d[32], char out[OTA_SHA_HEX_LEN + 1]);

// =====================================================================================
// 3. Lệnh room1/ota (CONTRACT mục 9)
// =====================================================================================
// {"version":"1.0.1","url":"http://10.199.40.112:8000/fw/node_b_1.0.1.bin",
//  "size":702157,"sha256":"9f2c...e81a"}
typedef struct {
  char     version[OTA_VERSION_MAX];  // "1.0.1" (đã kiểm bằng ota_version_parse)
  char     url[OTA_URL_MAX];          // bắt đầu bằng "http://"
  uint32_t size;                      // 1..OTA_MAX_FW_SIZE byte
  char     sha256[OTA_SHA_HEX_LEN + 1];  // 64 ký tự hex THƯỜNG (đã normalize)
} OtaCmd;

typedef enum {
  OTA_CMD_OK,         // đủ 4 key, đúng kiểu, đúng giới hạn
  OTA_CMD_BAD_JSON,   // không đọc được JSON, hoặc không phải object {...}
  OTA_CMD_BAD_FIELD,  // JSON đọc được nhưng thiếu key / sai kiểu / sai giới hạn
} OtaCmdResult;

// ota_cmd_parse: đọc payload room1/ota.
//   Vào : json, len = payload MQTT (KHÔNG có '\0' cuối, phải dùng len).
//         out = nơi ghi lệnh.
//   Ra  : OTA_CMD_OK / OTA_CMD_BAD_JSON / OTA_CMD_BAD_FIELD.
//         *out CHỈ bị ghi khi OK (lỗi thì *out giữ nguyên như trước khi gọi).
//   Điều kiện từng key (thiếu 1 key hoặc sai 1 điều kiện -> BAD_FIELD):
//     version: chuỗi, ota_version_parse được, độ dài < OTA_VERSION_MAX.
//     url    : chuỗi, bắt đầu "http://" (Node B không làm https: không có chứng chỉ),
//              độ dài < OTA_URL_MAX, phải dài hơn "http://" (không rỗng phần sau).
//     size   : số NGUYÊN (không nhận 1.5, "702157", true), 1 <= size <= OTA_MAX_FW_SIZE.
//     sha256 : chuỗi, ota_sha256_normalize được (lưu bản chữ thường).
//   Key thừa (vd "fw_title") thì bỏ qua, không lỗi.
//   Vì sao "lỗi thì giữ nguyên *out": giống config_parse #42. ota.cpp có thể đang giữ lệnh
//   cũ trong biến toàn cục; lệnh hỏng không được ghi đè dở dang lên nó.
OtaCmdResult ota_cmd_parse(const char *json, size_t len, OtaCmd *out);

// =====================================================================================
// 4. Có bắt đầu OTA không
// =====================================================================================
typedef enum {
  OTA_DECIDE_START,         // bắt đầu tải
  OTA_DECIDE_SAME_VERSION,  // version trong lệnh = bản đang chạy -> bỏ (CONTRACT 9)
  OTA_DECIDE_BUSY,          // đang có 1 OTA chạy dở -> bỏ lệnh mới
} OtaDecision;

// ota_decide: quyết định với 1 lệnh đã parse OK.
//   Vào : cmd = lệnh (đã qua ota_cmd_parse OK).
//         running_version = FW_VERSION của bản đang chạy, vd "1.0.0".
//         busy = true nếu task OTA đang tải/ghi.
//   Ra  : thứ tự kiểm: busy trước (BUSY), rồi version (SAME_VERSION), còn lại START.
//   "Bằng nhau" so theo SỐ (ota_version_parse cả hai rồi so major/minor/patch), không so
//   chuỗi. Nếu running_version không parse được (lỗi build) thì coi như KHÁC -> START,
//   để còn cứu được board bằng OTA.
//   Bản cũ hơn (1.0.1 -> 1.0.0) vẫn START: CONTRACT 9 chỉ cấm trùng; hạ bản có ích khi
//   bản mới bị lỗi logic mà vẫn nối được MQTT (rollback tự động không bắt được ca này).
//   Ví dụ (running "1.0.0"): cmd "1.0.1" busy=false -> START; cmd "1.0.0" -> SAME_VERSION;
//   cmd "1.0.1" busy=true -> BUSY; cmd "1.0.0" busy=true -> BUSY.
OtaDecision ota_decide(const OtaCmd *cmd, const char *running_version, bool busy);

// =====================================================================================
// 5. Khi nào gửi "downloading" (CONTRACT 9: gửi mỗi 10%)
// =====================================================================================
// ota_progress_next: gọi SAU MỖI lần ghi 1 khối dữ liệu.
//   Vào : received  = tổng số byte đã nhận tới giờ.
//         total     = kích thước file (cmd.size).
//         last_sent = mốc % đã gửi gần nhất; chưa gửi lần nào thì -1.
//   Ra  : mốc % CẦN GỬI BÂY GIỜ (0, 10, 20, ..., 100), hoặc -1 nếu chưa tới mốc mới.
//   Cách tính: pct = received * 100 / total (làm tròn XUỐNG, tính bằng uint64_t để không
//   tràn), giới hạn tối đa 100; mốc = pct / 10 * 10. Mốc > last_sent thì trả mốc, không
//   thì -1. total = 0 thì luôn -1 (tránh chia 0).
//   Ví dụ (total = 1000):
//     received   0, last -1 -> 0     (gửi "đã bắt đầu tải", progress 0)
//     received  50, last  0 -> -1    (5%, chưa tới 10)
//     received 100, last  0 -> 10
//     received 350, last 10 -> 30    (nhảy 1 khối lớn qua 20: CHỈ gửi 30, không gửi bù 20)
//     received 999, last 90 -> -1    (99,9% làm tròn xuống 99 -> mốc 90, đã gửi)
//     received 1000, last 90 -> 100
//     received 1200, last 100 -> -1  (vượt total: coi là 100, đã gửi)
//   Vì sao không gửi mỗi khối: 1 khối 1 KB, file 700 KB = 700 tin MQTT, nghẽn broker và
//   làm chậm tải. 11 tin là đủ cho dashboard.
int ota_progress_next(uint32_t received, uint32_t total, int last_sent);

// =====================================================================================
// 6. JSON room1/ota/status (CONTRACT 9)
// =====================================================================================
typedef enum {
  OTA_ST_DOWNLOADING,  // "downloading" + progress
  OTA_ST_VERIFYING,    // "verifying"
  OTA_ST_REBOOTING,    // "rebooting"
  OTA_ST_SUCCESS,      // "success" (gửi SAU khi khởi động lại bằng bản mới + nối MQTT)
  OTA_ST_FAILED,       // "failed" + error
} OtaState;

typedef enum {
  OTA_ERR_NONE,
  OTA_ERR_DOWNLOAD_FAILED,    // "download_failed": HTTP lỗi, sai size, đứt giữa chừng
  OTA_ERR_CHECKSUM_MISMATCH,  // "checksum_mismatch": sha256 tính ra khác lệnh
  OTA_ERR_WRITE_FAILED,       // "write_failed": Update.begin/write/end lỗi
  OTA_ERR_ROLLED_BACK,        // "rolled_back": bản mới không nối được MQTT trong 60 s
} OtaError;

// Đổi enum ra chuỗi đúng CONTRACT 9. Giá trị lạ -> "unknown".
const char *ota_state_str(OtaState s);
const char *ota_error_str(OtaError e);

// ota_status_build: tạo JSON room1/ota/status.
//   Vào : version = phiên bản ĐANG OTA (bản mới trong lệnh, không phải bản đang chạy).
//         state, progress (chỉ dùng khi DOWNLOADING), err (chỉ dùng khi FAILED).
//         buf, cap = bộ đệm ra.
//   Ra  : số byte đã ghi (không tính '\0'), 0 nếu buf không đủ chỗ (kể cả '\0').
//   JSON đúng thứ tự key version, state, rồi (nếu có) progress hoặc error:
//     DOWNLOADING: {"version":"1.0.1","state":"downloading","progress":45}
//     FAILED     : {"version":"1.0.1","state":"failed","error":"checksum_mismatch"}
//     còn lại    : {"version":"1.0.1","state":"verifying"}
//   Không có key progress khi không phải DOWNLOADING; không có key error khi không phải FAILED.
size_t ota_status_build(const char *version, OtaState state, int progress, OtaError err,
                        char *buf, size_t cap);

// =====================================================================================
// 7. Lúc khởi động: vừa OTA xong hay không (TASKS_W3 #58: rollback)
// =====================================================================================
// Trước khi khởi động lại vào bản mới, ota.cpp ghi version mới vào NVS (key "pending").
// Lúc khởi động, ota.cpp đọc lại key đó + hỏi ESP-IDF xem bản đang chạy có đang ở trạng
// thái "chờ xác nhận" (ESP_OTA_IMG_PENDING_VERIFY) không, rồi gọi hàm này.
typedef enum {
  OTA_BOOT_NORMAL,         // khởi động bình thường, không làm gì
  OTA_BOOT_VERIFY_NEW,     // bản mới, chờ xác nhận: chờ WiFi + MQTT trong 60 s
  OTA_BOOT_REPORT_SUCCESS, // bản mới ĐÃ được xác nhận từ trước nhưng chưa kịp báo success
  OTA_BOOT_ROLLED_BACK,    // bản mới hỏng, bootloader đã quay về bản cũ -> báo failed rolled_back
} OtaBootCase;

// ota_boot_case:
//   Vào : pending = version lưu trong NVS ("" nếu không có).
//         running = FW_VERSION bản đang chạy.
//         img_pending_verify = esp_ota_get_state_partition(running) == ESP_OTA_IMG_PENDING_VERIFY.
//   Ra  : bảng quyết định (so chuỗi pending với running bằng strcmp):
//     | pending          | img_pending_verify | Kết quả          |
//     |------------------|--------------------|------------------|
//     | bất kỳ           | true               | VERIFY_NEW       |  <- ưu tiên 1
//     | ""               | false              | NORMAL           |
//     | == running       | false              | REPORT_SUCCESS   |
//     | != running       | false              | ROLLED_BACK      |
//   Vì sao img_pending_verify=true luôn là VERIFY_NEW, kể cả khi NVS rỗng: ảnh ở trạng thái
//   chờ xác nhận mà không xác nhận thì lần reset sau bootloader TỰ quay về bản cũ. Nên dù
//   không rõ nguồn (vd nạp bằng espota), vẫn phải chạy bước xác nhận.
//   Ca ROLLED_BACK: đang chạy bản cũ "1.0.0" mà NVS ghi "1.0.1" -> lần OTA 1.0.1 đã bị quay
//   về. Báo failed + rolled_back với version "1.0.1" (bản hỏng), rồi xóa NVS.
//   Ví dụ: (pending "1.0.1", running "1.0.1", true)  -> VERIFY_NEW
//          (pending "1.0.1", running "1.0.0", false) -> ROLLED_BACK
//          (pending "",      running "1.0.0", false) -> NORMAL
OtaBootCase ota_boot_case(const char *pending, const char *running, bool img_pending_verify);

// =====================================================================================
// 8. Xác nhận bản mới trong 60 s
// =====================================================================================
typedef enum {
  OTA_CONFIRM_WAIT,      // chưa đủ, chờ tiếp
  OTA_CONFIRM_OK,        // WiFi + MQTT đều nối -> đánh dấu bản mới tốt, báo success
  OTA_CONFIRM_ROLLBACK,  // hết hạn mà chưa nối đủ -> quay về bản cũ
} OtaConfirm;

#define OTA_CONFIRM_LIMIT_MS 60000UL  // TASKS_W3 #58: 60 s

// ota_confirm_step: gọi định kỳ (vd 500 ms/lần) trong lúc chờ xác nhận.
//   Vào : now_ms, start_ms = millis() lúc này và lúc bắt đầu chờ.
//         wifi_ok, mqtt_ok = trạng thái hiện tại.
//         limit_ms = thời hạn (OTA_CONFIRM_LIMIT_MS).
//   Ra  : wifi_ok && mqtt_ok -> OK (ưu tiên, kể cả khi vừa quá hạn);
//         không thì (now_ms - start_ms) >= limit_ms -> ROLLBACK; còn lại WAIT.
//   Ví dụ (limit 60000, start 1000): now 30000, wifi ok, mqtt chưa -> WAIT;
//         now 61000, wifi ok, mqtt chưa -> ROLLBACK; now 61000, cả hai ok -> OK.
//   Nhớ: phép trừ uint32_t đúng cả khi millis() tràn (start 0xFFFFF000, now 0x00001000).
OtaConfirm ota_confirm_step(uint32_t now_ms, uint32_t start_ms, bool wifi_ok, bool mqtt_ok,
                            uint32_t limit_ms);
