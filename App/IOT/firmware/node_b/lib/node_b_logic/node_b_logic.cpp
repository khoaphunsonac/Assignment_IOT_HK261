// node_b_logic.cpp - thân các hàm của node_b_logic.h.
// Mô tả đầu vào / đầu ra / quy tắc của từng hàm: xem comment cùng tên trong node_b_logic.h.
// Test: test/test_logic/test_main.cpp, chạy `pio test -e native`.
#include "node_b_logic.h"

#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// Chép chuỗi an toàn (dài quá thì cắt, luôn có '\0' cuối).
// Không dùng strlcpy vì g++ trên Windows (MinGW) không có hàm này.
static void copy_str(char *dst, size_t cap, const char *src) {
  snprintf(dst, cap, "%s", src ? src : "");
}

// =====================================================================================
// 1. Đếm seq gói ESP-NOW
// =====================================================================================

void seq_init(SeqTracker *t) { memset(t, 0, sizeof(*t)); }

// Ví dụ, vừa nhận seq 10 (expected = 11), gói tới có seq:
//   11 -> gói mới bình thường     10 -> trùng (Node A gửi lại)
//   14 -> mất 11, 12, 13 (lost += 3)   0 -> Node A khởi động lại
SeqResult seq_on_packet(SeqTracker *t, uint32_t seq) {
  // Gói đầu tiên từ lúc Node B bật: không tính mất các seq trước nó
  if (!t->has_prev) {
    t->has_prev = true;
    t->rx = 1;
    t->expected = seq + 1;
    return SEQ_NEW;
  }
  // Trùng: bằng seq vừa nhận
  if (seq == t->expected - 1) {
    t->dup++;
    return SEQ_DUP;
  }
  // Node A khởi động lại: seq lùi về -> đếm lại từ đầu, gói này vẫn dùng được
  if (seq < t->expected - 1) {
    t->rx = 1;
    t->lost = 0;
    t->dup = 0;
    t->a_resets++;
    t->expected = seq + 1;
    return SEQ_RESTART;
  }
  // Nhảy cóc: các seq ở giữa bị mất
  if (seq > t->expected) {
    t->lost += seq - t->expected;
  }
  t->expected = seq + 1;
  t->rx++;
  return SEQ_NEW;
}

float seq_loss_pct(const SeqTracker *t) {
  uint32_t total = t->rx + t->lost;
  if (total == 0) {
    return 0;
  }
  return ((float) t->lost / (float) total) * 100;
}

// =====================================================================================
// 2. Nhiệt độ trung bình 2 lần đo
// =====================================================================================

void temp_avg_init(TempAvg *a) {
  a->has_prev = false;
}

float temp_avg_push(TempAvg *a, float t) {
  float result;
  if (!a->has_prev) {
    result = t;
  } else {
    result = (t + a->prev) / 2;
  }
  a->prev = t;
  a->has_prev = true;
  return result;
}

// =====================================================================================
// 3. Bộ ngưỡng
// =====================================================================================

// Cột "Mặc định" CONTRACT mục 7
void thresholds_default(Thresholds *th) {
  th->fan_on_1 = 31.0f;
  th->fan_off_1 = 29.0f;
  th->fan_on_2 = 33.0f;
  th->fan_off_2 = 31.0f;
  th->light_dark = 1500;
  th->light_bright = 2500;
  th->empty_timeout_s = 600;
  th->manual_timeout_s = 1800;
}

// Mỗi dòng kiểm 1 ràng buộc; viết dạng !(đúng) để NaN (so sánh luôn false) cũng bị loại.
bool thresholds_valid(const Thresholds *th) {

  if(!(th->fan_off_1 < th->fan_on_1)) return false;
  if(!(th->fan_on_1 <= th->fan_off_2)) return false;
  if(!(th->fan_off_2 < th->fan_on_2)) return false;
  if(!(th->light_dark < th->light_bright)) return false;
  if(!(th->light_bright <= 4095)) return false;
  if(!(th->empty_timeout_s >= 1)) return false;
  if(!(th->empty_timeout_s <= EMPTY_TIMEOUT_MAX_S)) return false;
  if(!(th->manual_timeout_s >= 1)) return false;
  return true;
}

// Đọc 1 key là SỐ (29 hoặc 29.5 đều được). Thiếu key / chuỗi "29" / true -> false.
static bool read_number(JsonObjectConst o, const char *key, float *v) {
  JsonVariantConst x = o[key];
  if (x.isNull() || x.is<bool>() || !x.is<float>()) return false;
  *v = x.as<float>();
  return true;
}

// Đọc 1 key số nguyên KHÔNG ÂM, tối đa maxv (600.0 cũng nhận, làm tròn thành 600).
// Số âm -> false (nếu ép thẳng thì -1 thành 65535).
static bool read_uint(JsonObjectConst o, const char *key, uint32_t maxv, uint32_t *v) {
  float f;
  if (!read_number(o, key, &f)) return false;
  long r = lroundf(f);
  if (r < 0 || (unsigned long)r > maxv) return false;
  *v = (uint32_t)r;
  return true;
}

bool config_parse(const char *json, size_t len, Thresholds *out) {
  // Dùng len vì payload MQTT không có '\0' cuối.
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return false;   // JSON hỏng ("abc")
  if (!doc.is<JsonObjectConst>()) return false;        // không phải {...} ("[1,2]")
  JsonObjectConst o = doc.as<JsonObjectConst>();

  Thresholds tam;  // đọc vào biến TẠM, đủ + đúng hết mới chép sang *out
  uint32_t u;      // biến trung gian cho key nguyên

  if (!read_number(o, "fan_on_1", &tam.fan_on_1)) return false;
  if (!read_number(o, "fan_on_2", &tam.fan_on_2)) return false;
  if (!read_number(o, "fan_off_1", &tam.fan_off_1)) return false;
  if (!read_number(o, "fan_off_2", &tam.fan_off_2)) return false;

  if (!read_uint(o, "light_dark", 65535, &u)) return false;
  tam.light_dark = (uint16_t)u;
  if (!read_uint(o, "light_bright", 65535, &u)) return false;
  tam.light_bright = (uint16_t)u;
  if (!read_uint(o, "empty_timeout_s", 0xFFFFFFFFu, &u)) return false;
  tam.empty_timeout_s = u;
  if (!read_uint(o, "manual_timeout_s", 0xFFFFFFFFu, &u)) return false;
  tam.manual_timeout_s = u;

  if(!thresholds_valid(&tam)) return false;

  *out = tam;
  return true;
}

// =====================================================================================
// 4. Lệnh room1/cmd
// =====================================================================================

const char *cmd_err_str(CmdErr e) {
  switch (e) {
    case CMD_BAD_JSON: return "bad_json";
    case CMD_UNKNOWN_METHOD: return "unknown_method";
    case CMD_BAD_PARAMS: return "bad_params";
    case CMD_IGNORED_NOT_AUTO: return "ignored_not_auto";
    default: return NULL;  // CMD_OK: không có error
  }
}

const char *lmode_str(LMode m) {
  switch (m) {
    case LMODE_AUTO: return "auto";
    case LMODE_MANUAL: return "manual";
    default: return "offline";
  }
}

// Ví dụ:
//   {"id":"tb-17","method":"setFan","params":2}     -> CMD_OK, method M_SET_FAN, level 2
//   {"id":"t","method":"setFan","params":5}         -> CMD_BAD_PARAMS (ngoài 0..2)
//   {"id":"t","method":"setLight","params":1}       -> CMD_BAD_PARAMS (phải true/false)
//   {"id":"t","method":"reboot"}                    -> CMD_UNKNOWN_METHOD
//   abc                                             -> CMD_BAD_JSON
// Kể cả khi lỗi, id và method_str vẫn phải được điền (để trả lời đúng người gửi).
CmdErr cmd_parse(const char *json, size_t len, ParsedCmd *out) {
  // Xóa out, parse, lấy id
  memset(out, 0, sizeof(*out));
  out->method = M_UNKNOWN;
  JsonDocument doc;
  if (deserializeJson(doc, json, len) || !doc.is<JsonObjectConst>()) return CMD_BAD_JSON;
  JsonObjectConst o = doc.as<JsonObjectConst>();
  if (o["id"].is<const char *>()) {  // id phải là chuỗi; số 17 coi như không có id
    out->has_id = true;
    copy_str(out->id, sizeof(out->id), o["id"].as<const char *>());
  }

  // Lấy method, rồi kiểm params theo từng method
  if (!o["method"].is<const char *>()) return CMD_UNKNOWN_METHOD;  // thiếu hoặc không phải chuỗi
  const char *m = o["method"].as<const char *>();
  copy_str(out->method_str, sizeof(out->method_str), m);
  JsonVariantConst p = o["params"];

  if (strcmp(m, "setFan") == 0 || strcmp(m, "autoFan") == 0) {
    out->method = (strcmp(m, "setFan") == 0) ? M_SET_FAN : M_AUTO_FAN;
    if(!p.is<int>()) return CMD_BAD_PARAMS;
    int v = p.as<int>();
    if(v < 0 || v > 2) return CMD_BAD_PARAMS;
    out->level = v;
    return CMD_OK;
  }
  if (strcmp(m, "setLight") == 0 || strcmp(m, "autoLight") == 0) {
    out->method = (strcmp(m, "setLight") == 0) ? M_SET_LIGHT : M_AUTO_LIGHT;
    if(!p.is<bool>()) return CMD_BAD_PARAMS;
    out->on = p.as<bool>();
    return CMD_OK;
  }
  if (strcmp(m, "setMode") == 0) {
    out->method = M_SET_MODE;
    if(!p.is<const char *>()) return CMD_BAD_PARAMS;
    const char *s = p.as<const char *>();
    if(strcmp(s, "auto") == 0) {
      out->mode = LMODE_AUTO;
      return CMD_OK;
    }
    if(strcmp(s, "manual") == 0) {
      out->mode = LMODE_MANUAL;
      return CMD_OK;
    }
    return CMD_BAD_PARAMS;
  }
  if (strcmp(m, "getState") == 0) {
    out->method = M_GET_STATE;
    return CMD_OK;
  }
  return CMD_UNKNOWN_METHOD;  // tên method lạ ("reboot")
}

// =====================================================================================
// 5. Luật điều khiển quạt/đèn
// =====================================================================================

void ctrl_init(CtrlLogic *c) {
  c->fan = 0;
  c->light = false;
  c->mode = LMODE_AUTO;
  c->manual_since_ms = 0;
}

CmdErr ctrl_set_fan(CtrlLogic *c, int level, bool is_rule, uint32_t now_ms) {
  if (level < 0 || level > 2) {
    return CMD_BAD_PARAMS;  // kiểm TRƯỚC, lệnh sai không được đổi chế độ
  }
  if (is_rule) {
    if(c->mode != LMODE_AUTO) return CMD_IGNORED_NOT_AUTO;
    c->fan = level;
    return CMD_OK;
  } else {
    c->fan = level;
    c->mode = LMODE_MANUAL;
    c->manual_since_ms = now_ms;
    return CMD_OK;
  }
}

CmdErr ctrl_set_light(CtrlLogic *c, bool on, bool is_rule, uint32_t now_ms) {
  if(is_rule) {
    if(c->mode != LMODE_AUTO) return CMD_IGNORED_NOT_AUTO;
    c->light = on;
    return CMD_OK;
  } else {
    c->light = on;
    c->mode = LMODE_MANUAL;
    c->manual_since_ms = now_ms;
    return CMD_OK;
  }
}

CmdErr ctrl_set_mode(CtrlLogic *c, LMode m, uint32_t now_ms) {
  if (m == LMODE_AUTO) {
    c->mode = LMODE_AUTO;
    return CMD_OK;
  }
  if (m == LMODE_MANUAL) {
    c->mode = LMODE_MANUAL;
    c->manual_since_ms = now_ms;
    return CMD_OK;
  }
  return CMD_BAD_PARAMS;  // offline hoặc giá trị lạ: người dùng không được đặt
}

bool ctrl_tick(CtrlLogic *c, bool presence_known, bool presence, uint32_t now_ms,
               uint32_t manual_timeout_ms) {
  // Ca 1: phòng trống (ưu tiên cao nhất, thắng cả lệnh tay)
  if (presence_known && !presence) {
    if(c->fan == 0 && c->light == false && c->mode == LMODE_AUTO) return false;
    c->fan = 0;
    c->light = false;
    c->mode = LMODE_AUTO;
    return true;
  }
  // Ca 2: đang manual và đã hết giờ
  if (c->mode == LMODE_MANUAL && now_ms - c->manual_since_ms >= manual_timeout_ms) {
    c->mode = LMODE_AUTO;
    return true;
  }
  return false;             // Ca 3: không có gì đổi
}

// =====================================================================================
// 6. Tạo JSON gửi đi
// =====================================================================================

size_t telemetry_build(const TelemetryIn *in, char *buf, size_t cap) {
  JsonDocument doc;

  if (in->has_ts) {
    doc["ts"] = in->ts_ms;
  }
  if (in->node_a_online) {  // Node A mất thì bỏ hết key cảm biến
    doc["seq"] = in->seq;
    doc["temperature"] = round(in->temperature * 10.0) / 10.0;  // làm tròn 1 số lẻ: 30.54 -> 30.5
    doc["humidity"] = round(in->humidity * 10.0) / 10.0;
    doc["light"] = in->light;
    doc["presence"] = in->presence;
    doc["anomaly"] = in->anomaly;
    doc["dht_fail"] = in->dht_fail;
    if(in->anomaly_score >= 0) doc["anomaly_score"] = round(in->anomaly_score * 1000.0) / 1000.0;
    doc["espnow_loss"] = round(in->espnow_loss * 10.0) / 10.0;
  }
  doc["node_a_online"] = in->node_a_online;  // LUÔN có, kể cả khi false
  doc["fan_level"] = in->fan_level;
  doc["light_on"] = in->light_on;
  doc["rssi"] = in->rssi;
  doc["fw_version"] = in->fw_version;
  doc["uptime_s"] = in->uptime_s;
  doc["mode"] = lmode_str(in->mode); 

  // Không đủ chỗ thì trả 0 (không gửi JSON bị cắt dở)
  if (measureJson(doc) + 1 > cap) return 0;
  return serializeJson(doc, buf, cap);
}

size_t resp_build(const ParsedCmd *cmd, CmdErr err, uint8_t fan, bool light, LMode mode,
                  char *buf, size_t cap) {
  JsonDocument doc;

  if (cmd->has_id) {
    doc["id"] = cmd->id;
  } else {
    doc["id"] = nullptr;  // ra "id":null
  }
  if(cmd->method_str[0] != '\0') doc["method"] = cmd->method_str;
  doc["ok"] = (err == CMD_OK);
  if(err != CMD_OK) {
    doc["error"] = cmd_err_str(err);
  }

  JsonObject st = doc["state"].to<JsonObject>();  // tạo object con "state"
  st["fan_level"] = fan;
  st["light_on"] = light;
  st["mode"] = lmode_str(mode);

  if (measureJson(doc) + 1 > cap) return 0;  // không đủ chỗ
  return serializeJson(doc, buf, cap);
}
