// node_b_logic: phần logic "thuần" của Node B (#42).
//
// Vì sao tách riêng thư viện này (giống node_a_logic ở #26):
//   - KHÔNG gọi Arduino/FreeRTOS/WiFi, KHÔNG tự đọc millis(). Thời gian luôn truyền vào
//     bằng tham số now_ms. Nhờ vậy chạy được trên PC: `pio test -e native`
//     (testcase trong test/test_logic/test_main.cpp), không cần board.
//   - Chỉ dùng thêm ArduinoJson (thư viện chỉ có header, chạy được cả trên PC lẫn ESP32)
//     để đọc/ghi JSON. Mọi chỗ dễ sai của Node B nằm ở đây: đếm seq, kiểm ngưỡng, đọc lệnh,
//     luật auto/manual/phòng trống, tạo JSON telemetry và trả lời lệnh.
//   - Các file trong src/ (control, espnow_rx, net, cmd, config_store, main) chỉ còn
//     "nối dây": lấy dữ liệu từ phần cứng/mạng, gọi hàm ở đây, rồi ghi ra phần cứng/mạng.
//
// Mẹo thời gian (giống node_a_logic): luôn so `now_ms - moc_cu` (uint32_t) với khoảng
// thời gian, KHÔNG so `now_ms >= moc_cu + khoang`. Phép trừ không dấu vẫn đúng khi
// millis() tràn sau ~49,7 ngày.
//
// Nguồn: CONTRACT mục 3 (quy tắc nhận), 5 (telemetry), 6 (lệnh), 7 (ngưỡng), 13 (chế độ).
// Không có code mẫu của thầy cho phần này.
#pragma once
#include <stddef.h>
#include <stdint.h>

// =====================================================================================
// 1. Đếm seq gói ESP-NOW (CONTRACT mục 3 "Quy tắc nhận", mục 5 key espnow_loss)
// =====================================================================================
// Node A đánh số mỗi mẫu bằng seq (0, 1, 2, ...). Node B nhìn seq để biết:
//   - Gói TRÙNG: seq bằng gói vừa nhận. Xảy ra khi Node A không nhận được ACK nên gửi lại
//     dù B đã nhận. Bỏ gói này (không đẩy lên MQTT lần 2, không đếm nhận).
//   - Gói MẤT: seq nhảy cóc (vừa nhận 10, giờ nhận 13 -> mất 11, 12 -> lost += 2).
//   - Node A KHỞI ĐỘNG LẠI: seq nhỏ hơn gói vừa nhận (trừ trường hợp trùng ở trên).
//     Node A đếm lại từ 0 nên B cũng đếm lại rx/lost/dup từ đầu (espnow_loss tính
//     "từ lúc Node A khởi động", CONTRACT mục 5).
// Quy tắc này giống hệt espnow_rx_test (#26, đã chạy đúng ở lab 13/11, bước L6), chỉ
// chuyển vào đây để có test.
typedef struct {
  bool     has_prev;  // đã nhận gói nào chưa (từ lúc Node B khởi động)
  uint32_t expected;  // seq mong đợi tiếp theo = seq gói vừa nhận + 1
  uint32_t rx;        // số gói nhận (không tính gói trùng)
  uint32_t lost;      // số gói mất (tính theo seq nhảy cóc)
  uint32_t dup;       // số gói trùng đã bỏ
  uint32_t a_resets;  // số lần thấy Node A khởi động lại
} SeqTracker;

typedef enum {
  SEQ_NEW,      // gói mới, dùng được (kể cả khi có mất gói trước nó)
  SEQ_DUP,      // gói gửi lại, BỎ
  SEQ_RESTART,  // Node A vừa khởi động lại, bộ đếm đã về đầu; gói này vẫn DÙNG được
} SeqResult;

// seq_init: đưa về trạng thái "chưa nhận gói nào", mọi bộ đếm = 0. Gọi 1 lần lúc boot.
void seq_init(SeqTracker *t);

// seq_on_packet: gọi cho MỖI PKT_DATA hợp lệ (đã qua kiểm len/magic/version).
//   Vào : t = bộ đếm, seq = PktData.seq của gói vừa nhận.
//   Ra  : SEQ_NEW / SEQ_DUP / SEQ_RESTART (xem enum).
//   Các ca (ví dụ vừa nhận seq 10, tức expected = 11):
//     Gói đầu tiên (has_prev = false): rx = 1, SEQ_NEW. Không tính mất các seq trước nó
//                                      (Node B có thể bật sau Node A).
//     seq = 10 (trùng)               : dup++, SEQ_DUP, không đổi gì khác.
//     seq < 10 (Node A khởi động lại): rx = 1, lost = dup = 0, a_resets++, SEQ_RESTART.
//     seq = 11 (bình thường)         : rx++, SEQ_NEW.
//     seq = 14 (nhảy cóc)            : lost += 3 (mất 11, 12, 13), rx++, SEQ_NEW.
//   Sau mọi ca trừ trùng: expected = seq + 1.
//   Code so sánh trực tiếp seq với expected - 1 / expected, KHÔNG dùng hiệu seq - expected
//   (uint32_t: seq nhỏ hơn thì hiệu ra số rất lớn).
SeqResult seq_on_packet(SeqTracker *t, uint32_t seq);

// seq_loss_pct: % gói mất = lost / (rx + lost) * 100. Chưa có gói nào thì 0.
//   Ra: float 0..100. Ví dụ rx = 99, lost = 1 -> 1.0.
float seq_loss_pct(const SeqTracker *t);

// =====================================================================================
// 2. Nhiệt độ trung bình 2 lần đo (CONTRACT mục 5, key temperature)
// =====================================================================================
// Tài liệu dự án mục 6: telemetry gửi nhiệt độ là trung bình 2 lần đo gần nhất cho đỡ
// nhảy số. Node B tự tính (Node A gửi số đo thô).
typedef struct {
  bool  has_prev;  // đã có lần đo trước chưa
  float prev;      // nhiệt độ lần đo trước
} TempAvg;

// temp_avg_init: chưa có lần đo nào. Gọi lúc boot và khi Node A khởi động lại.
void temp_avg_init(TempAvg *a);

// temp_avg_push: thêm 1 lần đo mới, trả về trung bình (lần này + lần trước).
//   Lần đo đầu tiên (chưa có lần trước) trả về chính nó.
//   Ví dụ: push 30 -> 30; push 31 -> 30.5; push 33 -> 32.
//   Chỉ push gói SEQ_NEW / SEQ_RESTART, KHÔNG push gói trùng.
float temp_avg_push(TempAvg *a, float t);

// =====================================================================================
// 3. Bộ ngưỡng (CONTRACT mục 7)
// =====================================================================================
typedef struct {
  float    fan_on_1;          // >= thì bật quạt mức 1
  float    fan_off_1;         // <= thì tắt quạt
  float    fan_on_2;          // >= thì lên mức 2
  float    fan_off_2;         // <= thì về mức 1
  uint16_t light_dark;        // ADC, tối hơn và có người thì bật đèn
  uint16_t light_bright;      // ADC, sáng hơn thì tắt đèn
  uint32_t empty_timeout_s;   // Node A dùng (gửi qua PKT_CONFIG ở #43)
  uint32_t manual_timeout_s;  // Node B dùng: lệnh tay giữ bao lâu rồi về auto
} Thresholds;

// Giới hạn trên của empty_timeout_s: PktConfig.empty_timeout_s là uint16_t (CONTRACT
// mục 3), lớn hơn 65535 thì gửi xuống Node A bị cắt số -> coi là ngưỡng sai.
#define EMPTY_TIMEOUT_MAX_S 65535u

// thresholds_default: điền giá trị cột "Mặc định" CONTRACT mục 7 (31/29/33/31,
// 1500/2500, 600 s, 1800 s). Dùng khi NVS chưa có gì (board mới, lần đầu chạy).
void thresholds_default(Thresholds *th);

// thresholds_valid: kiểm ràng buộc CONTRACT mục 7.
//   Ra: true khi ĐỦ tất cả:
//       fan_off_1 < fan_on_1 <= fan_off_2 < fan_on_2
//       light_dark < light_bright, light_bright <= 4095 (ADC 12 bit)
//       1 <= empty_timeout_s <= EMPTY_TIMEOUT_MAX_S
//       manual_timeout_s >= 1
//   Ngưỡng có NaN bị loại (mọi so sánh với NaN đều false).
bool thresholds_valid(const Thresholds *th);

// config_parse: đọc JSON của topic room1/config.
//   Vào : json, len = payload MQTT (KHÔNG có ký tự '\0' ở cuối, phải dùng len).
//         out = nơi ghi bộ ngưỡng mới.
//   Ra  : true  = JSON đúng, có ĐỦ 8 key, key nào cũng là SỐ, và bộ mới qua
//                 thresholds_valid -> đã ghi vào *out.
//         false = sai bất cứ điều gì ở trên -> *out GIỮ NGUYÊN (CONTRACT 7: "sai thì giữ
//                 bộ cũ"). Gateway luôn gửi bộ đầy đủ, nên thiếu key là lỗi.
//   Kiểu số: chấp nhận cả số nguyên lẫn số thực cho mọi key (Python có thể gửi 600.0).
//            Key kiểu nguyên (light_*, *_timeout_s): làm tròn (lround) rồi mới kiểm.
//            Số âm ở key nguyên: coi là sai (ép thẳng sang uint thì -1 thành 65535).
//            Chuỗi "31" KHÔNG nhận (CONTRACT mục 2: số phải là kiểu số JSON).
bool config_parse(const char *json, size_t len, Thresholds *out);

// =====================================================================================
// 4. Lệnh room1/cmd (CONTRACT mục 6)
// =====================================================================================
typedef enum {
  M_SET_FAN,     // params int 0..2, lệnh tay
  M_SET_LIGHT,   // params bool, lệnh tay
  M_SET_MODE,    // params "auto" / "manual"
  M_AUTO_FAN,    // params int 0..2, chỉ RuleChain
  M_AUTO_LIGHT,  // params bool, chỉ RuleChain
  M_GET_STATE,   // params null hoặc bỏ
  M_UNKNOWN,
} CmdMethod;

typedef enum {
  CMD_OK,
  CMD_BAD_JSON,          // "bad_json"
  CMD_UNKNOWN_METHOD,    // "unknown_method"
  CMD_BAD_PARAMS,        // "bad_params"
  CMD_IGNORED_NOT_AUTO,  // "ignored_not_auto"
} CmdErr;

// Chế độ trong logic. Thứ tự GIỐNG enum class Mode trong control.h (Auto, Manual,
// Offline) để control.cpp đổi qua lại bằng static_cast. Logic không include control.h
// vì control.h nằm trong src/ (test native không thấy).
typedef enum : uint8_t { LMODE_AUTO = 0, LMODE_MANUAL = 1, LMODE_OFFLINE = 2 } LMode;

#define CMD_ID_MAX 40      // id dài hơn thì cắt (vd "tb-123456" chỉ ~10 ký tự)
#define CMD_METHOD_MAX 24  // tên method dài hơn thì cắt

typedef struct {
  bool      has_id;                  // có key "id" kiểu chuỗi không
  char      id[CMD_ID_MAX];          // id (chuỗi kết thúc '\0'), "" nếu không có
  char      method_str[CMD_METHOD_MAX];  // method gốc để trả lời lại, "" nếu không có
  CmdMethod method;
  int       level;                   // params của setFan / autoFan (0..2)
  bool      on;                      // params của setLight / autoLight
  LMode     mode;                    // params của setMode (chỉ AUTO / MANUAL)
} ParsedCmd;

// cmd_parse: đọc JSON lệnh và kiểm params theo bảng 6.2.
//   Vào : json, len = payload MQTT (không có '\0' cuối). out = kết quả.
//   Ra  : CMD_BAD_JSON       nếu không parse được hoặc gốc JSON không phải object
//                            (vd "abc", "[1,2]"). Khi đó has_id = false.
//         CMD_UNKNOWN_METHOD nếu thiếu "method", method không phải chuỗi, hoặc không có
//                            trong 6 method. id và method_str vẫn được điền để trả lời.
//         CMD_BAD_PARAMS     nếu params sai kiểu hoặc ngoài khoảng:
//                              setFan/autoFan: phải là số NGUYÊN 0..2 (2.5, "2", true đều sai)
//                              setLight/autoLight: phải là bool (1, "true" đều sai)
//                              setMode: chuỗi "auto" hoặc "manual" ("offline" sai)
//                              getState: không kiểm params
//         CMD_OK             còn lại; level / on / mode đã điền tùy method.
//   Lưu ý: luôn điền out->has_id, out->id, out->method_str TRƯỚC khi kiểm method/params,
//   để cả khi lỗi vẫn trả lời đúng id (CONTRACT 6.3: trả lời MỌI lệnh có id).
//   Hàm này KHÔNG xét chế độ auto/manual (ignored_not_auto là việc của ctrl_* ở mục 5).
CmdErr cmd_parse(const char *json, size_t len, ParsedCmd *out);

// cmd_err_str: CmdErr -> chuỗi "error" trong room1/cmd/resp. CMD_OK -> NULL.
const char *cmd_err_str(CmdErr e);

// lmode_str: LMode -> "auto" / "manual" / "offline".
const char *lmode_str(LMode m);

// =====================================================================================
// 5. Luật điều khiển quạt/đèn (CONTRACT mục 6.2 + mục 13)
// =====================================================================================
// Trạng thái điều khiển "thuần": chỉ là số, chưa đụng tới chân PWM/LED. control.cpp
// giữ một CtrlLogic, khóa mutex, gọi các hàm dưới đây, rồi xuất ra phần cứng.
typedef struct {
  uint8_t  fan;              // 0..2
  bool     light;            // đèn bật?
  LMode    mode;             // auto / manual (offline làm ở W4)
  uint32_t manual_since_ms;  // lúc có lệnh tay GẦN NHẤT (để tính manual_timeout_s)
} CtrlLogic;

// ctrl_init: quạt 0, đèn tắt, auto (CONTRACT 13: mặc định khi khởi động là auto).
void ctrl_init(CtrlLogic *c);

// ctrl_set_fan: đặt mức quạt.
//   Vào : level   = mức muốn đặt (int để bắt được số âm / > 2 từ web)
//         is_rule = true nếu là autoFan (RuleChain), false nếu là lệnh tay (setFan)
//         now_ms  = millis()
//   Ra  : CMD_BAD_PARAMS nếu level ngoài 0..2 (không đổi gì).
//         Lệnh rule: mode khác AUTO -> CMD_IGNORED_NOT_AUTO (không đổi gì);
//                    mode AUTO -> fan = level, KHÔNG đổi mode, CMD_OK.
//         Lệnh tay : fan = level, mode = MANUAL, manual_since_ms = now_ms, CMD_OK.
//                    (Mỗi lệnh tay đặt lại đồng hồ manual, kể cả khi đã ở manual.)
CmdErr ctrl_set_fan(CtrlLogic *c, int level, bool is_rule, uint32_t now_ms);

// ctrl_set_light: như ctrl_set_fan nhưng cho đèn (không có lỗi bad_params vì bool).
CmdErr ctrl_set_light(CtrlLogic *c, bool on, bool is_rule, uint32_t now_ms);

// ctrl_set_mode: lệnh setMode.
//   LMODE_AUTO   -> mode = AUTO, CMD_OK (giữ nguyên quạt/đèn, RuleChain sẽ chỉnh tiếp).
//   LMODE_MANUAL -> mode = MANUAL, manual_since_ms = now_ms, CMD_OK.
//   LMODE_OFFLINE hoặc giá trị khác -> CMD_BAD_PARAMS (Node B tự vào offline, người
//   không đặt được; CONTRACT 6.2).
CmdErr ctrl_set_mode(CtrlLogic *c, LMode m, uint32_t now_ms);

// ctrl_tick: áp luật ưu tiên CONTRACT 13. control_tick (control.cpp) gọi ~200 ms/lần.
//   Vào : presence_known = có số liệu presence còn hiệu lực không (Node A đang online)
//         presence       = có người (chỉ có nghĩa khi presence_known)
//         now_ms, manual_timeout_ms = manual_timeout_s * 1000
//   Ra  : true nếu fan/light/mode bị đổi (để main gửi telemetry ngay), false nếu không.
//   Làm theo thứ tự ưu tiên:
//     (1) presence_known && !presence (phòng trống): nếu chưa "tắt hết" thì
//         fan = 0, light = false, mode = AUTO, trả true. Đã tắt hết rồi thì trả false
//         (tránh gửi telemetry mỗi 200 ms). Xong (1) thì RETURN, không xét (2).
//     (2) mode == MANUAL và đã qua manual_timeout_ms kể từ manual_since_ms:
//         mode = AUTO (giữ quạt/đèn), trả true.
//     (3) còn lại: false.
//   Node A mất (presence_known = false): KHÔNG tắt gì (không biết phòng có người hay
//   không thì giữ nguyên, an toàn hơn tắt quạt khi đang có người).
bool ctrl_tick(CtrlLogic *c, bool presence_known, bool presence, uint32_t now_ms,
               uint32_t manual_timeout_ms);

// =====================================================================================
// 6. Tạo JSON gửi đi (CONTRACT mục 5 và 6.3)
// =====================================================================================
// Dữ liệu đầu vào của 1 bản telemetry. net.cpp (publish_telemetry) gom từ control + WiFi.
typedef struct {
  bool     has_ts;          // đã đồng bộ NTP chưa (chưa thì BỎ key ts)
  uint64_t ts_ms;           // epoch ms
  bool     node_a_online;   // false -> BỎ HẾT các key cảm biến bên dưới (tới espnow_loss)
  // ---- key cảm biến, chỉ gửi khi node_a_online ----
  uint32_t seq;
  float    temperature;     // đã là trung bình 2 lần đo
  float    humidity;
  uint16_t light;
  bool     presence;
  bool     anomaly;
  float    anomaly_score;   // < 0 (Node A gửi -1 = chưa chạy TinyML) -> BỎ key này
  uint16_t dht_fail;
  float    espnow_loss;     // %
  // ---- key của Node B, luôn gửi ----
  uint8_t     fan_level;
  bool        light_on;
  LMode       mode;
  int         rssi;
  const char *fw_version;   // vd "1.0.0"
  uint32_t    uptime_s;
} TelemetryIn;

// telemetry_build: tạo chuỗi JSON room1/telemetry.
//   Vào : in = dữ liệu, buf/cap = bộ đệm ra.
//   Ra  : số byte đã ghi (không tính '\0'), 0 nếu bộ đệm không đủ.
//   Quy tắc (CONTRACT mục 2, 5):
//     - temperature, humidity, espnow_loss: làm tròn 1 chữ số thập phân; anomaly_score: 3.
//       Làm tròn bằng round(x * 10) / 10.0 (kiểu double): gán thẳng float 30.1 vào
//       ArduinoJson có thể in ra 30.10000038.
//     - bool gửi true/false (KHÔNG 0/1), mode là chuỗi.
//     - Không bao giờ có null / NaN trong JSON.
//     - Thứ tự key không quan trọng (Python đọc theo tên).
size_t telemetry_build(const TelemetryIn *in, char *buf, size_t cap);

// resp_build: tạo chuỗi JSON room1/cmd/resp (CONTRACT 6.3).
//   Vào : cmd = lệnh đã parse (dùng has_id/id/method_str); err = kết quả cuối cùng
//         (sau cả cmd_parse lẫn ctrl_*); fan/light/mode = trạng thái SAU khi làm lệnh.
//   Ra  : số byte đã ghi, 0 nếu không đủ chỗ.
//   Quy tắc:
//     - "id": chuỗi id nếu has_id, ngược lại null (bad_json: "id": null).
//     - "method": method_str nếu khác rỗng; rỗng thì BỎ key (vd bad_json).
//     - "ok": err == CMD_OK.
//     - "error": cmd_err_str(err), CHỈ có khi ok = false.
//     - "state": luôn đủ 3 key fan_level, light_on, mode (kể cả khi lỗi).
size_t resp_build(const ParsedCmd *cmd, CmdErr err, uint8_t fan, bool light, LMode mode,
                  char *buf, size_t cap);

// =====================================================================================
// 7. Khi nào gửi PKT_CONFIG xuống Node A (#43, CONTRACT mục 3 và 7)
// =====================================================================================
// Node A không có WiFi nên không thấy room1/config. Node B chuyển empty_timeout_s xuống
// bằng gói ESP-NOW PktConfig. Gửi khi:
//   (a) vừa ghép cặp với Node A (Node A vừa boot thì đang dùng mặc định 60 s),
//   (b) empty_timeout_s đổi (dashboard đổi ngưỡng -> room1/config -> config_store),
//   (c) đã CFG_RESEND_MS kể từ lần gửi trước (gói (a)/(b) có thể mất; gửi lại 8 byte
//       mỗi 30 s rẻ hơn nhiều so với viết cơ chế ACK riêng).
// Hàm thuần: KHÔNG gửi gì, chỉ trả lời "giờ có nên gửi không" và ghi nhớ đã gửi.
#define CFG_RESEND_MS 30000u

typedef struct {
  bool     have_peer;     // đã có Node A (đã ghép cặp ít nhất 1 lần từ lúc boot)
  bool     sent_once;     // đã gửi lần nào cho Node A HIỆN TẠI chưa (ghép lại -> false)
  uint32_t last_sent_s;   // empty_timeout_s của lần gửi trước
  uint32_t last_send_ms;  // millis() lần gửi trước
} CfgTxState;

// cfg_tx_init: lúc boot, chưa có Node A -> không gửi gì.
void cfg_tx_init(CfgTxState *s);

// cfg_tx_on_pair: gọi mỗi lần Node A xin ghép cặp (kể cả Node A cũ ghép lại sau reset).
//   Làm: have_peer = true, sent_once = false -> lần hỏi kế tiếp chắc chắn gửi (ca a).
void cfg_tx_on_pair(CfgTxState *s);

// cfg_tx_should_send: gọi ~1 s/lần trong task_espnow.
//   Vào : timeout_s = empty_timeout_s đang dùng (config_store_get()), now_ms = millis().
//   Ra  : true  -> NÊN gửi ngay PktConfig với timeout_s; hàm ĐÃ ghi nhớ lần gửi này
//                  (sent_once = true, last_sent_s = timeout_s, last_send_ms = now_ms).
//         false -> không gửi, KHÔNG đổi gì trong *s.
//   Đúng cả khi millis() tràn.
bool cfg_tx_should_send(CfgTxState *s, uint32_t timeout_s, uint32_t now_ms);
