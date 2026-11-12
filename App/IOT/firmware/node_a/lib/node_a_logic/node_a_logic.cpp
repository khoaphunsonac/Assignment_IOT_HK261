#include "node_a_logic.h"
#include <math.h>

bool reading_valid(float t, float h) {
  // TODO 1: trả true khi t, h không NaN và nằm trong [TEMP_MIN_C, TEMP_MAX_C], [HUMI_MIN, HUMI_MAX]
  return false;
}

void presence_init(PresenceState *s, uint32_t now_ms) {
  // TODO 2a
}

uint8_t presence_update(PresenceState *s, bool pir_high, uint32_t now_ms, uint32_t timeout_ms) {
  // TODO 2b: PIR HIGH -> cập nhật last_motion_ms.
  //          Trả 1 nếu từ last_motion_ms tới now_ms CHƯA đủ timeout_ms, ngược lại 0.
  //          Gợi ý: phép trừ uint32_t (now - last) vẫn đúng khi millis() tràn.
  return 0;
}

void tx_init(TxState *s) {
  // TODO 3a
}

TxAction tx_on_result(TxState *s, bool ok, uint32_t now_ms) {
  // TODO 3b: theo comment trong node_a_logic.h.
  //  - ok: xóa chuỗi lỗi, retry = 0 -> TX_DONE
  //  - lỗi: bắt đầu chuỗi nếu chưa có; đủ TX_LOST_MS -> reset state, TX_REPAIR;
  //         còn lượt -> retry++, TX_RESEND; hết lượt -> retry = 0, TX_GIVE_UP
  return TX_DONE;
}
