// Hiện thực node_a_logic.h. Mô tả vào/ra của từng hàm xem ở file .h.
// Test: `pio test -e native` (12 testcase, test/test_logic/test_main.cpp).
#include "node_a_logic.h"
#include <math.h>

// Kiểm NaN trước (NaN so sánh gì cũng false), rồi mới kiểm khoảng.
bool reading_valid(float t, float h) {
  if (isnan(t)) return false;
  if (isnan(h)) return false;
  if (t < TEMP_MIN_C || t > TEMP_MAX_C) return false;
  if (h < HUMI_MIN || h > HUMI_MAX) return false;
  return true;
}

// Boot = coi như vừa thấy người -> 60 s đầu presence luôn là 1.
void presence_init(PresenceState *s, uint32_t now_ms) {
  s->last_motion_ms = now_ms;
}

uint8_t presence_update(PresenceState *s, bool pir_high, uint32_t now_ms, uint32_t timeout_ms) {
  // Thấy chuyển động -> "đặt lại đồng hồ" đếm thời gian phòng trống.
  if(pir_high) s->last_motion_ms = now_ms;
  // Phép trừ uint32_t: đúng cả khi millis() tràn về 0.
  if(now_ms - s->last_motion_ms < timeout_ms) return 1;
  return 0;
}

void tx_init(TxState *s) {
  s->fail_start_ms = 0;  // chỉ được đọc khi failing == true, lần lỗi đầu sẽ ghi đè
  s->failing = false;
  s->retry = 0;
}

TxAction tx_on_result(TxState *s, bool ok, uint32_t now_ms) {
  // Gửi được: kết thúc chuỗi lỗi (nếu có), mẫu sau được đủ lượt gửi lại.
  if(ok) {
    s->retry = 0;
    s->failing = false;
    return TX_DONE;
  }
  // Lỗi đầu tiên của chuỗi: ghi mốc thời gian (chưa thể đủ 10 s nên đi tiếp xuống dưới).
  // Đã đang lỗi: kiểm đủ TX_LOST_MS chưa -> REPAIR được ưu tiên hơn RESEND/GIVE_UP.
  if(s->failing == false) {
    s->failing = true;
    s->fail_start_ms = now_ms;
  } else if (now_ms - s->fail_start_ms >= TX_LOST_MS){
    tx_init(s);
    return TX_REPAIR;
  }
  // Còn lượt: gửi lại cùng gói (cùng seq).
  if(s->retry < TX_MAX_RETRY) {
    s->retry++;
    return TX_RESEND;
  }
  // Hết lượt: bỏ mẫu này. KHÔNG xóa chuỗi lỗi, để lỗi kéo dài qua nhiều mẫu vẫn
  // tính đủ 10 s và dẫn tới REPAIR (test_tx_repair_after_10s_across_samples).
  s->retry = 0;
  return TX_GIVE_UP;
}
