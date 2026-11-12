#include "node_a_logic.h"
#include <math.h>

bool reading_valid(float t, float h) {
  if (isnan(t)) return false;
  if (isnan(h)) return false;
  if (t < TEMP_MIN_C || t > TEMP_MAX_C) return false;
  if (h < HUMI_MIN || h > HUMI_MAX) return false;
  return true;
}

void presence_init(PresenceState *s, uint32_t now_ms) {
  s->last_motion_ms = now_ms;
}

uint8_t presence_update(PresenceState *s, bool pir_high, uint32_t now_ms, uint32_t timeout_ms) {
  if(pir_high) s->last_motion_ms = now_ms;
  if(now_ms - s->last_motion_ms < timeout_ms) return 1;
  return 0;
}

void tx_init(TxState *s) {
  s->fail_start_ms = 0;
  s->failing = false;
  s->retry = 0;
}

TxAction tx_on_result(TxState *s, bool ok, uint32_t now_ms) {
  if(ok) {
    s->retry = 0;
    s->failing = false;
    return TX_DONE;
  }
  if(s->failing == false) {
    s->failing = true;
    s->fail_start_ms = now_ms;
  } else if (now_ms - s->fail_start_ms >= TX_LOST_MS){
    tx_init(s);
    return TX_REPAIR;
  }
  if(s->retry < TX_MAX_RETRY) {
    s->retry++;
    return TX_RESEND;
  }
  s->retry = 0;
  return TX_GIVE_UP;
}
