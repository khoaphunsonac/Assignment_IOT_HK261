// Logic thuần của Node A: KHÔNG gọi Arduino/FreeRTOS, KHÔNG đọc millis() bên trong.
// Thời gian truyền vào bằng tham số now_ms -> test được trên PC (pio test -e native).
// Testcase: test/test_logic/test_main.cpp
#pragma once
#include <stdint.h>

// ---------- 1. Kiểm số đo hợp lệ (CONTRACT mục 2, bảng Đơn vị) ----------
#define TEMP_MIN_C  -10.0f
#define TEMP_MAX_C   60.0f
#define HUMI_MIN      0.0f
#define HUMI_MAX    100.0f

// true khi t và h đều không phải NaN và nằm trong khoảng (tính cả 2 đầu mút).
bool reading_valid(float t, float h);

// ---------- 2. Presence có thời gian chờ "phòng trống" ----------
// PIR chỉ báo HIGH lúc có chuyển động. Người ngồi yên thì PIR về LOW,
// nên presence = "có chuyển động trong timeout_ms gần nhất".
typedef struct {
  uint32_t last_motion_ms;  // lần cuối thấy PIR HIGH
} PresenceState;

// Lúc khởi động: coi như vừa thấy chuyển động (giả sử phòng có người),
// để Node B không tắt quạt/đèn ngay khi Node A vừa bật.
void presence_init(PresenceState *s, uint32_t now_ms);

// Gọi mỗi lần đọc PIR. Trả 1 = có người, 0 = phòng trống.
// Phải đúng cả khi millis() tràn (khoảng 49,7 ngày quay về 0).
uint8_t presence_update(PresenceState *s, bool pir_high, uint32_t now_ms, uint32_t timeout_ms);

// ---------- 3. Gửi lại / ghép lại (CONTRACT mục 3, "Quy tắc gửi") ----------
#define TX_MAX_RETRY 3       // gửi lại tối đa 3 lần (tổng 4 lần gửi một mẫu)
#define TX_LOST_MS   10000   // lỗi liên tiếp 10 s -> coi như mất Node B

typedef enum {
  TX_DONE,     // gửi được -> mẫu tiếp theo dùng seq + 1
  TX_RESEND,   // gửi lại NGAY, CÙNG seq
  TX_GIVE_UP,  // hết lượt gửi lại -> bỏ mẫu này (seq vẫn + 1, Node B tự đếm là mất)
  TX_REPAIR,   // lỗi liên tiếp >= TX_LOST_MS -> bỏ peer, quét channel, ghép cặp lại
} TxAction;

typedef struct {
  uint8_t  retry;          // số lần đã gửi lại của mẫu hiện tại
  bool     failing;        // đang trong chuỗi lỗi liên tiếp?
  uint32_t fail_start_ms;  // thời điểm lỗi ĐẦU TIÊN của chuỗi
} TxState;

void tx_init(TxState *s);

// Gọi sau MỖI lần gửi, ok = kết quả ACK (callback onSent). Trả việc phải làm tiếp.
// Thứ tự ưu tiên khi lỗi: TX_REPAIR > TX_RESEND > TX_GIVE_UP.
// Chuỗi lỗi kéo dài qua nhiều mẫu, chỉ hết khi có 1 lần ok hoặc khi trả TX_REPAIR.
TxAction tx_on_result(TxState *s, bool ok, uint32_t now_ms);
