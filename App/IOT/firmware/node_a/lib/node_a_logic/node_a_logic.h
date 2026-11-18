// node_a_logic: phần logic "thuần" của Node A.
//
// Vì sao tách riêng thư viện này:
//   - KHÔNG gọi Arduino/FreeRTOS, KHÔNG tự đọc millis(). Thời gian luôn truyền vào bằng
//     tham số now_ms. Nhờ vậy chạy được trên PC: `pio test -e native` (17 testcase trong
//     test/test_logic/test_main.cpp), không cần board.
//   - Phần dễ sai nhất (kiểm số đo, presence có thời gian chờ, quyết định gửi lại) được
//     test kỹ ở đây; phần gọi phần cứng (sensors.cpp, espnow_link.cpp) chỉ còn "nối dây".
//
// Mẹo thời gian dùng chung cho cả file: luôn so `now_ms - moc_cu` (uint32_t) với khoảng
// thời gian, KHÔNG so `now_ms >= moc_cu + khoang`. millis() là uint32_t, khoảng 49,7 ngày
// thì tràn về 0; phép trừ không dấu vẫn ra đúng số ms đã trôi qua kể cả khi tràn
// (vd moc_cu = 0xFFFFFFF0, now = 0x10 -> now - moc_cu = 32), còn phép cộng thì sai.
#pragma once
#include <stdint.h>

// ---------- 1. Kiểm số đo hợp lệ (CONTRACT mục 2, bảng Đơn vị) ----------
// Khoảng đo hợp lý trong phòng học. Ngoài khoảng này coi như cảm biến đọc sai.
#define TEMP_MIN_C  -10.0f
#define TEMP_MAX_C   60.0f
#define HUMI_MIN      0.0f
#define HUMI_MAX    100.0f

// reading_valid: kiểm 1 cặp số đo DHT20 có dùng được không.
//   Vào : t = nhiệt độ (°C), h = độ ẩm (%).
//   Ra  : true khi t và h đều KHÔNG phải NaN và nằm trong khoảng (tính cả 2 đầu mút).
//   Lưu ý: phải kiểm isnan() riêng. Mọi phép so sánh với NaN đều ra false, nên nếu chỉ
//   viết "t < min || t > max thì loại" thì NaN lọt qua và bị coi là hợp lệ.
bool reading_valid(float t, float h);

// ---------- 2. Presence có thời gian chờ "phòng trống" ----------
// PIR chỉ báo HIGH lúc có CHUYỂN ĐỘNG. Người ngồi yên học bài thì PIR về LOW dù phòng
// vẫn có người. Nên presence được định nghĩa là "có chuyển động trong timeout_ms gần nhất":
// mỗi lần thấy HIGH thì "đặt lại đồng hồ", hết timeout_ms mà không thấy ai mới là trống.
typedef struct {
  uint32_t last_motion_ms;  // lần cuối thấy PIR HIGH (theo millis)
} PresenceState;

// presence_init: gọi 1 lần lúc khởi động.
//   Vào : s = trạng thái presence, now_ms = millis() lúc boot.
//   Làm : coi như vừa thấy chuyển động (giả sử phòng có người), để Node B không tắt
//         quạt/đèn ngay khi Node A vừa bật.
void presence_init(PresenceState *s, uint32_t now_ms);

// presence_update: gọi mỗi lần đọc PIR (2 s/lần).
//   Vào : s          = trạng thái presence (giữ last_motion_ms giữa các lần gọi)
//         pir_high   = kết quả digitalRead(PIN_PIR) == HIGH
//         now_ms     = millis() hiện tại
//         timeout_ms = thời gian chờ trước khi kết luận phòng trống (empty_timeout_s * 1000, từ #43 Node B đổi được)
//   Ra  : 1 = có người, 0 = phòng trống.
//   Làm : (1) PIR HIGH -> ghi last_motion_ms = now_ms.
//         (2) Chưa đủ timeout_ms kể từ last_motion_ms -> 1, đủ rồi -> 0.
//   Đúng cả khi millis() tràn (xem mẹo thời gian ở đầu file, test_presence_millis_wrap).
uint8_t presence_update(PresenceState *s, bool pir_high, uint32_t now_ms, uint32_t timeout_ms);

// ---------- 3. Gửi lại / ghép lại (CONTRACT mục 3, "Quy tắc gửi") ----------
#define TX_MAX_RETRY 3       // gửi lại tối đa 3 lần (tổng 4 lần gửi một mẫu)
#define TX_LOST_MS   10000   // lỗi liên tiếp 10 s -> coi như mất Node B

// Việc main.cpp phải làm sau mỗi lần gửi (main KHÔNG tự đếm, chỉ làm theo).
typedef enum {
  TX_DONE,     // gửi được -> mẫu tiếp theo dùng seq + 1
  TX_RESEND,   // gửi lại NGAY, CÙNG seq
  TX_GIVE_UP,  // hết lượt gửi lại -> bỏ mẫu này (seq vẫn + 1, Node B tự đếm là mất)
  TX_REPAIR,   // lỗi liên tiếp >= TX_LOST_MS -> bỏ peer, quét channel, ghép cặp lại
} TxAction;

// Có 2 bộ đếm khác nhau, đừng lẫn:
//   - retry: số lần gửi lại CỦA MẪU HIỆN TẠI, mẫu mới đếm lại từ 0.
//   - failing + fail_start_ms: CHUỖI LỖI LIÊN TIẾP tính theo thời gian, có thể kéo dài
//     qua nhiều mẫu (Node B tắt thì mẫu nào cũng hụt), chỉ hết khi có 1 lần ok.
typedef struct {
  uint8_t  retry;          // số lần đã gửi lại của mẫu hiện tại
  bool     failing;        // đang trong chuỗi lỗi liên tiếp?
  uint32_t fail_start_ms;  // thời điểm lỗi ĐẦU TIÊN của chuỗi
} TxState;

// tx_init: đưa về trạng thái "chưa gửi lại lần nào, không có chuỗi lỗi".
// Gọi lúc khởi động, sau mỗi lần ghép cặp thành công, và khi trả TX_REPAIR.
void tx_init(TxState *s);

// tx_on_result: quyết định việc tiếp theo sau MỖI lần gửi (gọi đúng 1 lần cho 1 lần gửi).
//   Vào : s = trạng thái gửi, ok = Node B có ACK không (từ callback onSent),
//         now_ms = millis() lúc có kết quả.
//   Ra  : TxAction (xem enum ở trên).
//   Làm : - ok: xóa chuỗi lỗi, retry = 0 -> TX_DONE.
//         - lỗi: chưa có chuỗi thì bắt đầu chuỗi (ghi fail_start_ms);
//                lỗi liền >= TX_LOST_MS -> tx_init, TX_REPAIR;
//                còn lượt (retry < TX_MAX_RETRY) -> retry++, TX_RESEND;
//                hết lượt -> retry = 0 (mẫu sau được đủ lượt), TX_GIVE_UP.
//   Thứ tự ưu tiên khi lỗi: TX_REPAIR > TX_RESEND > TX_GIVE_UP
//   (đã mất Node B 10 s thì ghép lại luôn, không phí thêm lượt gửi lại).
//   Luật "gửi mấy lần" CHỈ nằm ở hàm này; main.cpp không viết for(i < 4) để khỏi lệch
//   khi đổi TX_MAX_RETRY.
TxAction tx_on_result(TxState *s, bool ok, uint32_t now_ms);

// ---------- 4. Nhận PKT_CONFIG từ Node B (#43, CONTRACT mục 3) ----------
// Node B gửi PktConfig (8 byte) sau mỗi lần ghép cặp, khi empty_timeout_s đổi, và gửi
// lại định kỳ 30 s (phòng gói đầu bị mất). Node A dùng số này thay cho hằng 60 s.
// Mất điện thì Node A quay về EMPTY_TIMEOUT_DEFAULT_S tới khi nhận gói đầu tiên
// (Node A không lưu flash: Node B luôn gửi lại ngay khi ghép cặp, lưu là thừa).
#define EMPTY_TIMEOUT_DEFAULT_S 60   // giá trị demo CONTRACT mục 7, dùng từ lúc boot
#define EMPTY_TIMEOUT_MIN_S     1    // 0 s nghĩa là "luôn trống" -> vô lý, loại

// config_pkt_parse: kiểm 1 gói ESP-NOW có phải PKT_CONFIG hợp lệ không.
//   Vào : data, len = gói thô đúng như callback on_recv nhận (data có thể không căn lề 4).
//         out_timeout_s = nơi ghi kết quả.
//   Ra  : true  -> gói hợp lệ, *out_timeout_s = empty_timeout_s trong gói.
//         false -> gói không dùng được, *out_timeout_s GIỮ NGUYÊN (không ghi gì).
//   Hợp lệ khi đủ 5 điều kiện: len == sizeof(PktConfig), magic == ESPNOW_MAGIC,
//   version == ESPNOW_VERSION, type == PKT_CONFIG, empty_timeout_s >= EMPTY_TIMEOUT_MIN_S.
//   KHÔNG kiểm MAC người gửi ở đây (hàm thuần, không biết MAC Node B). Kiểm MAC nằm ở
//   espnow_link_poll_config().
bool config_pkt_parse(const uint8_t *data, int len, uint16_t *out_timeout_s);
