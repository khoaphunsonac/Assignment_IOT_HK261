// Hiện thực node_a_logic.h. Mô tả vào/ra của từng hàm xem ở file .h.
// Test: `pio test -e native` (17 testcase, test/test_logic/test_main.cpp).
#include "node_a_logic.h"
#include <math.h>
#include <string.h>
#include "espnow_packet.h"  // PktConfig (firmware/common, cần -I ../common)

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

// =====================================================================================
// #43 config_pkt_parse: kiểm gói PKT_CONFIG (test: test_cfgpkt_*)
// =====================================================================================
// Ví dụ (gói đúng = magic 0x31544C42, version 1, type 4, empty_timeout_s 30, len 8):
//   gói đúng                  -> true, *out = 30
//   len = 7 (hoặc 28)         -> false, *out giữ nguyên
//   magic 0x12345678          -> false     (board nhóm khác trong phòng lab)
//   type = PKT_PAIR_RESP (2)  -> false     (len đúng 8 vẫn bị loại)
//   empty_timeout_s = 0       -> false
//   empty_timeout_s = 65535   -> true      (lớn nhất uint16_t, hợp lệ)
// Kiểm len trước memcpy: gói ngắn mà vẫn chép 8 byte là đọc tràn buffer.
// memcpy vào biến PktConfig rồi mới đọc, không ép kiểu (const PktConfig *)data:
// data có thể lệch căn lề, đọc uint32_t magic ở địa chỉ lệch là undefined behavior.
bool config_pkt_parse(const uint8_t *data, int len, uint16_t *out_timeout_s) {
  if(len != (int)sizeof(PktConfig)) return false;
  PktConfig pc;
  memcpy(&pc, data, sizeof(pc));
  if(pc.hdr.magic != ESPNOW_MAGIC) return false;
  if(pc.hdr.version != ESPNOW_VERSION) return false;
  if (pc.hdr.type != PKT_CONFIG) return false;
  if(pc.empty_timeout_s < EMPTY_TIMEOUT_MIN_S) return false;
  *out_timeout_s = pc.empty_timeout_s;
  return true;
}
