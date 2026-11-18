// Testcase cho lib/node_b_logic (38 test).
// Chạy trên PC:     pio test -e native     (cần gcc/g++ trong PATH, như #26)
// Chạy trên board:  pio test -e yolo_uno   (cắm board, không cần gcc)
//
// Nhóm test (tên hàm test bắt đầu bằng):
//   seq_   temp_avg_   thresholds_valid   config_parse   cmd_parse   ctrl_set_
//   ctrl_tick   telemetry_build   resp_build
// Test FAIL: Unity in tên hàm test + số dòng TEST_ASSERT bị sai (`pio test -e native -v`).
#include <unity.h>
#include <math.h>
#include <string.h>
#include <ArduinoJson.h>
#include "node_b_logic.h"

void setUp(void) {}
void tearDown(void) {}

// Tiện ích: chuỗi C -> (json, len) như payload MQTT (không có '\0' cuối).
#define J(s) (s), strlen(s)

// ================= 1. seq =================
void test_seq_first_packet_any_number(void) {
  SeqTracker t;
  seq_init(&t);
  // Node B bật sau Node A: gói đầu seq = 500, KHÔNG được tính mất 0..499
  TEST_ASSERT_EQUAL_INT(SEQ_NEW, seq_on_packet(&t, 500));
  TEST_ASSERT_EQUAL_UINT32(1, t.rx);
  TEST_ASSERT_EQUAL_UINT32(0, t.lost);
  TEST_ASSERT_EQUAL_INT(SEQ_NEW, seq_on_packet(&t, 501));
  TEST_ASSERT_EQUAL_UINT32(2, t.rx);
}

void test_seq_dup_dropped(void) {
  SeqTracker t;
  seq_init(&t);
  seq_on_packet(&t, 0);
  seq_on_packet(&t, 1);
  TEST_ASSERT_EQUAL_INT(SEQ_DUP, seq_on_packet(&t, 1));  // Node A gửi lại cùng seq
  TEST_ASSERT_EQUAL_UINT32(2, t.rx);
  TEST_ASSERT_EQUAL_UINT32(1, t.dup);
  TEST_ASSERT_EQUAL_UINT32(0, t.lost);
  TEST_ASSERT_EQUAL_INT(SEQ_NEW, seq_on_packet(&t, 2));
}

void test_seq_gap_counts_lost(void) {
  SeqTracker t;
  seq_init(&t);
  seq_on_packet(&t, 10);
  TEST_ASSERT_EQUAL_INT(SEQ_NEW, seq_on_packet(&t, 13));  // mất 11, 12
  TEST_ASSERT_EQUAL_UINT32(2, t.lost);
  TEST_ASSERT_EQUAL_UINT32(2, t.rx);
  TEST_ASSERT_EQUAL_FLOAT(50.0f, seq_loss_pct(&t));      // 2 / (2 + 2)
}

void test_seq_node_a_restart(void) {
  SeqTracker t;
  seq_init(&t);
  for (uint32_t s = 100; s <= 105; s++) seq_on_packet(&t, s);
  seq_on_packet(&t, 108);  // mất 2
  TEST_ASSERT_EQUAL_UINT32(2, t.lost);
  TEST_ASSERT_EQUAL_INT(SEQ_RESTART, seq_on_packet(&t, 0));  // Node A boot lại
  TEST_ASSERT_EQUAL_UINT32(1, t.rx);
  TEST_ASSERT_EQUAL_UINT32(0, t.lost);
  TEST_ASSERT_EQUAL_UINT32(0, t.dup);
  TEST_ASSERT_EQUAL_UINT32(1, t.a_resets);
  TEST_ASSERT_EQUAL_INT(SEQ_NEW, seq_on_packet(&t, 1));
  TEST_ASSERT_EQUAL_UINT32(0, t.lost);
}

void test_seq_loss_pct(void) {
  SeqTracker t;
  seq_init(&t);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, seq_loss_pct(&t));  // chưa có gói, không chia cho 0
  seq_on_packet(&t, 0);
  seq_on_packet(&t, 99);  // nhận 2, mất 98
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 98.0f, seq_loss_pct(&t));
}

// ================= 2. temp_avg =================
void test_temp_avg(void) {
  TempAvg a;
  temp_avg_init(&a);
  TEST_ASSERT_EQUAL_FLOAT(30.0f, temp_avg_push(&a, 30.0f));  // lần đầu = chính nó
  TEST_ASSERT_EQUAL_FLOAT(30.5f, temp_avg_push(&a, 31.0f));
  TEST_ASSERT_EQUAL_FLOAT(32.0f, temp_avg_push(&a, 33.0f));  // (31 + 33) / 2, không phải 3 số
  temp_avg_init(&a);
  TEST_ASSERT_EQUAL_FLOAT(25.0f, temp_avg_push(&a, 25.0f));
}

// ================= 3. thresholds_valid =================
static Thresholds def_th(void) {
  Thresholds th;
  thresholds_default(&th);
  return th;
}

void test_thresholds_default_valid(void) {
  Thresholds th = def_th();
  TEST_ASSERT_TRUE(thresholds_valid(&th));
}

void test_thresholds_fan_order(void) {
  Thresholds th = def_th();
  th.fan_off_1 = 31.0f;  // = fan_on_1 -> sai (phải <)
  TEST_ASSERT_FALSE(thresholds_valid(&th));

  th = def_th();
  th.fan_on_1 = 31.0f;
  th.fan_off_2 = 31.0f;  // fan_on_1 <= fan_off_2: bằng nhau ĐƯỢC
  TEST_ASSERT_TRUE(thresholds_valid(&th));

  th.fan_off_2 = 30.0f;  // fan_on_1 > fan_off_2 -> sai
  TEST_ASSERT_FALSE(thresholds_valid(&th));

  th = def_th();
  th.fan_on_2 = 31.0f;  // = fan_off_2 -> sai
  TEST_ASSERT_FALSE(thresholds_valid(&th));

  th = def_th();
  th.fan_on_1 = NAN;
  TEST_ASSERT_FALSE(thresholds_valid(&th));
}

void test_thresholds_light_and_timeouts(void) {
  Thresholds th = def_th();
  th.light_dark = 2500;  // = light_bright -> sai
  TEST_ASSERT_FALSE(thresholds_valid(&th));

  th = def_th();
  th.light_bright = 4095;  // đúng giá trị ADC lớn nhất -> VẪN hợp lệ
  TEST_ASSERT_TRUE(thresholds_valid(&th));
  th.light_bright = 4096;  // quá ADC 12 bit
  TEST_ASSERT_FALSE(thresholds_valid(&th));

  th = def_th();
  th.empty_timeout_s = 0;
  TEST_ASSERT_FALSE(thresholds_valid(&th));
  th.empty_timeout_s = 65535;  // vừa uint16_t của PktConfig
  TEST_ASSERT_TRUE(thresholds_valid(&th));
  th.empty_timeout_s = 65536;
  TEST_ASSERT_FALSE(thresholds_valid(&th));

  th = def_th();
  th.manual_timeout_s = 0;
  TEST_ASSERT_FALSE(thresholds_valid(&th));
}

// ================= 4. config_parse =================
static const char *CFG_DEMO =
    "{\"fan_on_1\":30.5,\"fan_off_1\":29,\"fan_on_2\":33,\"fan_off_2\":31,"
    "\"light_dark\":1500,\"light_bright\":2500,\"empty_timeout_s\":60,\"manual_timeout_s\":120}";

void test_config_parse_ok(void) {
  Thresholds th = def_th();
  TEST_ASSERT_TRUE(config_parse(J(CFG_DEMO), &th));
  TEST_ASSERT_EQUAL_FLOAT(30.5f, th.fan_on_1);
  TEST_ASSERT_EQUAL_FLOAT(29.0f, th.fan_off_1);  // số nguyên 29 vẫn đọc được cho key float
  TEST_ASSERT_EQUAL_UINT16(1500, th.light_dark);
  TEST_ASSERT_EQUAL_UINT32(60, th.empty_timeout_s);
  TEST_ASSERT_EQUAL_UINT32(120, th.manual_timeout_s);
}

void test_config_parse_float_for_int_keys(void) {
  // Python/CoreIOT có thể gửi 600.0 cho key nguyên
  const char *s =
      "{\"fan_on_1\":31,\"fan_off_1\":29,\"fan_on_2\":33,\"fan_off_2\":31,"
      "\"light_dark\":1500.0,\"light_bright\":2500.0,\"empty_timeout_s\":600.0,"
      "\"manual_timeout_s\":1800.0}";
  Thresholds th = def_th();
  th.empty_timeout_s = 1;
  TEST_ASSERT_TRUE(config_parse(J(s), &th));
  TEST_ASSERT_EQUAL_UINT32(600, th.empty_timeout_s);
  TEST_ASSERT_EQUAL_UINT16(2500, th.light_bright);
}

void test_config_parse_keeps_old_on_error(void) {
  Thresholds th = def_th();
  // thiếu manual_timeout_s
  const char *missing =
      "{\"fan_on_1\":30.5,\"fan_off_1\":29,\"fan_on_2\":33,\"fan_off_2\":31,"
      "\"light_dark\":1500,\"light_bright\":2500,\"empty_timeout_s\":60}";
  TEST_ASSERT_FALSE(config_parse(J(missing), &th));
  TEST_ASSERT_EQUAL_FLOAT(31.0f, th.fan_on_1);  // vẫn là bộ cũ

  // sai ràng buộc: fan_off_1 > fan_on_1
  const char *bad_order =
      "{\"fan_on_1\":29,\"fan_off_1\":31,\"fan_on_2\":33,\"fan_off_2\":31,"
      "\"light_dark\":1500,\"light_bright\":2500,\"empty_timeout_s\":60,\"manual_timeout_s\":120}";
  TEST_ASSERT_FALSE(config_parse(J(bad_order), &th));
  TEST_ASSERT_EQUAL_FLOAT(31.0f, th.fan_on_1);
  TEST_ASSERT_EQUAL_UINT32(600, th.empty_timeout_s);  // không ghi được NỬA bộ

  // số để trong dấu nháy
  const char *str_num =
      "{\"fan_on_1\":\"31\",\"fan_off_1\":29,\"fan_on_2\":33,\"fan_off_2\":31,"
      "\"light_dark\":1500,\"light_bright\":2500,\"empty_timeout_s\":60,\"manual_timeout_s\":120}";
  TEST_ASSERT_FALSE(config_parse(J(str_num), &th));

  // số âm ở key nguyên
  const char *neg =
      "{\"fan_on_1\":31,\"fan_off_1\":29,\"fan_on_2\":33,\"fan_off_2\":31,"
      "\"light_dark\":-1,\"light_bright\":2500,\"empty_timeout_s\":60,\"manual_timeout_s\":120}";
  TEST_ASSERT_FALSE(config_parse(J(neg), &th));

  TEST_ASSERT_FALSE(config_parse(J("abc"), &th));
  TEST_ASSERT_FALSE(config_parse(J("[1,2]"), &th));
  TEST_ASSERT_EQUAL_FLOAT(31.0f, th.fan_on_1);
}

void test_config_parse_big_timeouts(void) {
  // Key timeout là uint32_t: không được ép về uint16_t (70000 -> 4464 sai)
  const char *big_manual =
      "{\"fan_on_1\":31,\"fan_off_1\":29,\"fan_on_2\":33,\"fan_off_2\":31,"
      "\"light_dark\":1500,\"light_bright\":2500,\"empty_timeout_s\":600,\"manual_timeout_s\":100000}";
  Thresholds th = def_th();
  TEST_ASSERT_TRUE(config_parse(J(big_manual), &th));
  TEST_ASSERT_EQUAL_UINT32(100000, th.manual_timeout_s);
  // empty_timeout_s > 65535 không gửi xuống Node A được -> phải bị từ chối
  const char *big_empty =
      "{\"fan_on_1\":31,\"fan_off_1\":29,\"fan_on_2\":33,\"fan_off_2\":31,"
      "\"light_dark\":1500,\"light_bright\":2500,\"empty_timeout_s\":70000,\"manual_timeout_s\":120}";
  TEST_ASSERT_FALSE(config_parse(J(big_empty), &th));
  TEST_ASSERT_EQUAL_UINT32(600, th.empty_timeout_s);
}

void test_config_parse_uses_len(void) {
  // Payload MQTT không có '\0': phần sau len là rác, phải bỏ qua
  char buf[300];
  snprintf(buf, sizeof(buf), "%sRAC_RAC", CFG_DEMO);
  Thresholds th = def_th();
  TEST_ASSERT_TRUE(config_parse(buf, strlen(CFG_DEMO), &th));
  TEST_ASSERT_EQUAL_UINT32(60, th.empty_timeout_s);
}

// ================= 5. cmd_parse =================
void test_cmd_set_fan_ok(void) {
  ParsedCmd c;
  TEST_ASSERT_EQUAL_INT(CMD_OK, cmd_parse(J("{\"id\":\"tb-17\",\"method\":\"setFan\",\"params\":2}"), &c));
  TEST_ASSERT_TRUE(c.has_id);
  TEST_ASSERT_EQUAL_STRING("tb-17", c.id);
  TEST_ASSERT_EQUAL_STRING("setFan", c.method_str);
  TEST_ASSERT_EQUAL_INT(M_SET_FAN, c.method);
  TEST_ASSERT_EQUAL_INT(2, c.level);
}

void test_cmd_fan_bad_params(void) {
  ParsedCmd c;
  TEST_ASSERT_EQUAL_INT(CMD_BAD_PARAMS, cmd_parse(J("{\"id\":\"t\",\"method\":\"setFan\",\"params\":5}"), &c));
  TEST_ASSERT_EQUAL_STRING("t", c.id);  // lỗi vẫn giữ id để trả lời
  TEST_ASSERT_EQUAL_INT(CMD_BAD_PARAMS, cmd_parse(J("{\"id\":\"t\",\"method\":\"setFan\",\"params\":-1}"), &c));
  TEST_ASSERT_EQUAL_INT(CMD_BAD_PARAMS, cmd_parse(J("{\"id\":\"t\",\"method\":\"setFan\",\"params\":1.5}"), &c));
  TEST_ASSERT_EQUAL_INT(CMD_BAD_PARAMS, cmd_parse(J("{\"id\":\"t\",\"method\":\"setFan\",\"params\":\"2\"}"), &c));
  TEST_ASSERT_EQUAL_INT(CMD_BAD_PARAMS, cmd_parse(J("{\"id\":\"t\",\"method\":\"setFan\",\"params\":true}"), &c));
  TEST_ASSERT_EQUAL_INT(CMD_BAD_PARAMS, cmd_parse(J("{\"id\":\"t\",\"method\":\"autoFan\"}"), &c));  // thiếu params
  TEST_ASSERT_EQUAL_INT(CMD_OK, cmd_parse(J("{\"id\":\"t\",\"method\":\"autoFan\",\"params\":0}"), &c));
  TEST_ASSERT_EQUAL_INT(M_AUTO_FAN, c.method);
}

void test_cmd_light(void) {
  ParsedCmd c;
  TEST_ASSERT_EQUAL_INT(CMD_OK, cmd_parse(J("{\"id\":\"t\",\"method\":\"setLight\",\"params\":true}"), &c));
  TEST_ASSERT_EQUAL_INT(M_SET_LIGHT, c.method);
  TEST_ASSERT_TRUE(c.on);
  TEST_ASSERT_EQUAL_INT(CMD_OK, cmd_parse(J("{\"id\":\"t\",\"method\":\"autoLight\",\"params\":false}"), &c));
  TEST_ASSERT_EQUAL_INT(M_AUTO_LIGHT, c.method);
  TEST_ASSERT_FALSE(c.on);
  TEST_ASSERT_EQUAL_INT(CMD_BAD_PARAMS, cmd_parse(J("{\"id\":\"t\",\"method\":\"setLight\",\"params\":1}"), &c));
  TEST_ASSERT_EQUAL_INT(CMD_BAD_PARAMS, cmd_parse(J("{\"id\":\"t\",\"method\":\"setLight\",\"params\":\"true\"}"), &c));
}

void test_cmd_mode_and_get_state(void) {
  ParsedCmd c;
  TEST_ASSERT_EQUAL_INT(CMD_OK, cmd_parse(J("{\"id\":\"t\",\"method\":\"setMode\",\"params\":\"manual\"}"), &c));
  TEST_ASSERT_EQUAL_INT(M_SET_MODE, c.method);
  TEST_ASSERT_EQUAL_INT(LMODE_MANUAL, c.mode);
  TEST_ASSERT_EQUAL_INT(CMD_OK, cmd_parse(J("{\"id\":\"t\",\"method\":\"setMode\",\"params\":\"auto\"}"), &c));
  TEST_ASSERT_EQUAL_INT(LMODE_AUTO, c.mode);
  TEST_ASSERT_EQUAL_INT(CMD_BAD_PARAMS, cmd_parse(J("{\"id\":\"t\",\"method\":\"setMode\",\"params\":\"offline\"}"), &c));
  TEST_ASSERT_EQUAL_INT(CMD_BAD_PARAMS, cmd_parse(J("{\"id\":\"t\",\"method\":\"setMode\",\"params\":1}"), &c));
  TEST_ASSERT_EQUAL_INT(CMD_OK, cmd_parse(J("{\"id\":\"t\",\"method\":\"getState\",\"params\":null}"), &c));
  TEST_ASSERT_EQUAL_INT(M_GET_STATE, c.method);
  TEST_ASSERT_EQUAL_INT(CMD_OK, cmd_parse(J("{\"id\":\"t\",\"method\":\"getState\"}"), &c));
}

void test_cmd_unknown_and_bad_json(void) {
  ParsedCmd c;
  TEST_ASSERT_EQUAL_INT(CMD_UNKNOWN_METHOD, cmd_parse(J("{\"id\":\"t9\",\"method\":\"reboot\"}"), &c));
  TEST_ASSERT_EQUAL_STRING("t9", c.id);
  TEST_ASSERT_EQUAL_STRING("reboot", c.method_str);
  TEST_ASSERT_EQUAL_INT(CMD_UNKNOWN_METHOD, cmd_parse(J("{\"id\":\"t9\"}"), &c));  // thiếu method
  TEST_ASSERT_EQUAL_INT(CMD_UNKNOWN_METHOD, cmd_parse(J("{\"id\":\"t9\",\"method\":3}"), &c));
  TEST_ASSERT_EQUAL_INT(CMD_BAD_JSON, cmd_parse(J("abc"), &c));
  TEST_ASSERT_FALSE(c.has_id);
  TEST_ASSERT_EQUAL_INT(CMD_BAD_JSON, cmd_parse(J("[1,2]"), &c));
  TEST_ASSERT_FALSE(c.has_id);
  // Không có id (hoặc id không phải chuỗi) vẫn làm lệnh, trả lời "id": null
  TEST_ASSERT_EQUAL_INT(CMD_OK, cmd_parse(J("{\"id\":17,\"method\":\"getState\"}"), &c));
  TEST_ASSERT_FALSE(c.has_id);
}

void test_cmd_long_id_truncated(void) {
  ParsedCmd c;
  const char *s =
      "{\"id\":\"0123456789012345678901234567890123456789XYZ\",\"method\":\"getState\"}";
  TEST_ASSERT_EQUAL_INT(CMD_OK, cmd_parse(J(s), &c));
  TEST_ASSERT_EQUAL_size_t(CMD_ID_MAX - 1, strlen(c.id));  // cắt, không tràn bộ đệm
}

// ================= 6. ctrl_set_* =================
void test_ctrl_init(void) {
  CtrlLogic c;
  ctrl_init(&c);
  TEST_ASSERT_EQUAL_UINT8(0, c.fan);
  TEST_ASSERT_FALSE(c.light);
  TEST_ASSERT_EQUAL_INT(LMODE_AUTO, c.mode);
}

void test_ctrl_manual_fan_switches_mode(void) {
  CtrlLogic c;
  ctrl_init(&c);
  TEST_ASSERT_EQUAL_INT(CMD_OK, ctrl_set_fan(&c, 2, false, 1000));
  TEST_ASSERT_EQUAL_UINT8(2, c.fan);
  TEST_ASSERT_EQUAL_INT(LMODE_MANUAL, c.mode);
  TEST_ASSERT_EQUAL_UINT32(1000, c.manual_since_ms);
  TEST_ASSERT_EQUAL_INT(CMD_OK, ctrl_set_light(&c, true, false, 5000));  // lệnh tay mới -> đặt lại đồng hồ
  TEST_ASSERT_TRUE(c.light);
  TEST_ASSERT_EQUAL_UINT32(5000, c.manual_since_ms);
}

void test_ctrl_rule_only_in_auto(void) {
  CtrlLogic c;
  ctrl_init(&c);
  TEST_ASSERT_EQUAL_INT(CMD_OK, ctrl_set_fan(&c, 1, true, 0));
  TEST_ASSERT_EQUAL_UINT8(1, c.fan);
  TEST_ASSERT_EQUAL_INT(LMODE_AUTO, c.mode);  // rule KHÔNG đổi chế độ
  TEST_ASSERT_EQUAL_INT(CMD_OK, ctrl_set_light(&c, true, true, 0));
  TEST_ASSERT_TRUE(c.light);

  ctrl_set_fan(&c, 2, false, 100);  // vào manual
  TEST_ASSERT_EQUAL_INT(CMD_IGNORED_NOT_AUTO, ctrl_set_fan(&c, 0, true, 200));
  TEST_ASSERT_EQUAL_UINT8(2, c.fan);  // không đổi
  TEST_ASSERT_EQUAL_INT(CMD_IGNORED_NOT_AUTO, ctrl_set_light(&c, false, true, 200));
  TEST_ASSERT_TRUE(c.light);
}

void test_ctrl_bad_level(void) {
  CtrlLogic c;
  ctrl_init(&c);
  TEST_ASSERT_EQUAL_INT(CMD_BAD_PARAMS, ctrl_set_fan(&c, 3, false, 0));
  TEST_ASSERT_EQUAL_INT(CMD_BAD_PARAMS, ctrl_set_fan(&c, -1, true, 0));
  TEST_ASSERT_EQUAL_UINT8(0, c.fan);
  TEST_ASSERT_EQUAL_INT(LMODE_AUTO, c.mode);  // lệnh sai không được đổi sang manual
}

void test_ctrl_set_mode(void) {
  CtrlLogic c;
  ctrl_init(&c);
  TEST_ASSERT_EQUAL_INT(CMD_OK, ctrl_set_mode(&c, LMODE_MANUAL, 700));
  TEST_ASSERT_EQUAL_INT(LMODE_MANUAL, c.mode);
  TEST_ASSERT_EQUAL_UINT32(700, c.manual_since_ms);
  TEST_ASSERT_EQUAL_INT(CMD_BAD_PARAMS, ctrl_set_mode(&c, LMODE_OFFLINE, 800));
  TEST_ASSERT_EQUAL_INT(LMODE_MANUAL, c.mode);
  ctrl_set_fan(&c, 2, false, 900);
  TEST_ASSERT_EQUAL_INT(CMD_OK, ctrl_set_mode(&c, LMODE_AUTO, 1000));
  TEST_ASSERT_EQUAL_INT(LMODE_AUTO, c.mode);
  TEST_ASSERT_EQUAL_UINT8(2, c.fan);  // về auto giữ nguyên quạt
}

// ================= 7. ctrl_tick =================
#define MT 120000u  // manual_timeout 120 s (demo)

void test_tick_empty_room_turns_off(void) {
  CtrlLogic c;
  ctrl_init(&c);
  ctrl_set_fan(&c, 2, false, 0);
  ctrl_set_light(&c, true, false, 0);
  TEST_ASSERT_TRUE(ctrl_tick(&c, true, false, 1000, MT));
  TEST_ASSERT_EQUAL_UINT8(0, c.fan);
  TEST_ASSERT_FALSE(c.light);
  TEST_ASSERT_EQUAL_INT(LMODE_AUTO, c.mode);
  TEST_ASSERT_FALSE(ctrl_tick(&c, true, false, 1200, MT));  // đã tắt hết -> không báo đổi nữa
}

void test_tick_node_a_lost_keeps_state(void) {
  CtrlLogic c;
  ctrl_init(&c);
  ctrl_set_fan(&c, 1, true, 0);
  // presence_known = false: không biết phòng có người -> không tắt
  TEST_ASSERT_FALSE(ctrl_tick(&c, false, false, 1000, MT));
  TEST_ASSERT_EQUAL_UINT8(1, c.fan);
}

void test_tick_manual_timeout(void) {
  CtrlLogic c;
  ctrl_init(&c);
  ctrl_set_fan(&c, 2, false, 1000);
  TEST_ASSERT_FALSE(ctrl_tick(&c, true, true, 1000 + MT - 1, MT));
  TEST_ASSERT_EQUAL_INT(LMODE_MANUAL, c.mode);
  TEST_ASSERT_TRUE(ctrl_tick(&c, true, true, 1000 + MT, MT));
  TEST_ASSERT_EQUAL_INT(LMODE_AUTO, c.mode);
  TEST_ASSERT_EQUAL_UINT8(2, c.fan);  // về auto, giữ quạt; RuleChain chỉnh tiếp
  TEST_ASSERT_FALSE(ctrl_tick(&c, true, true, 1000 + MT + 200, MT));
}

void test_tick_manual_timeout_also_when_node_a_lost(void) {
  CtrlLogic c;
  ctrl_init(&c);
  ctrl_set_light(&c, true, false, 0);
  TEST_ASSERT_TRUE(ctrl_tick(&c, false, false, MT, MT));
  TEST_ASSERT_EQUAL_INT(LMODE_AUTO, c.mode);
}

void test_tick_manual_timeout_millis_wrap(void) {
  CtrlLogic c;
  ctrl_init(&c);
  uint32_t t0 = 0xFFFFFF00u;
  ctrl_set_fan(&c, 1, false, t0);
  TEST_ASSERT_FALSE(ctrl_tick(&c, true, true, t0 + MT - 1u, MT));  // đã tràn qua 0
  TEST_ASSERT_TRUE(ctrl_tick(&c, true, true, t0 + MT, MT));
}

// ================= 8. telemetry_build =================
static TelemetryIn full_in(void) {
  TelemetryIn in;
  memset(&in, 0, sizeof(in));
  in.has_ts = true;
  in.ts_ms = 1762761600123ULL;
  in.node_a_online = true;
  in.seq = 1532;
  in.temperature = 30.54f;
  in.humidity = 65.16f;
  in.light = 1830;
  in.presence = true;
  in.anomaly = false;
  in.anomaly_score = 0.1236f;
  in.dht_fail = 0;
  in.espnow_loss = 0.8f;
  in.fan_level = 1;
  in.light_on = true;
  in.mode = LMODE_AUTO;
  in.rssi = -45;
  in.fw_version = "1.0.0";
  in.uptime_s = 3605;
  return in;
}

void test_telemetry_full(void) {
  TelemetryIn in = full_in();
  char buf[512];
  size_t n = telemetry_build(&in, buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, buf));
  TEST_ASSERT_TRUE(d["ts"].as<uint64_t>() == 1762761600123ULL);
  TEST_ASSERT_EQUAL_UINT32(1532, d["seq"].as<uint32_t>());
  TEST_ASSERT_TRUE(d["presence"].is<bool>());      // bool, không phải 0/1
  TEST_ASSERT_TRUE(d["node_a_online"].as<bool>());
  TEST_ASSERT_TRUE(d["light_on"].is<bool>());
  TEST_ASSERT_EQUAL_STRING("auto", d["mode"].as<const char *>());
  TEST_ASSERT_EQUAL_STRING("1.0.0", d["fw_version"].as<const char *>());
  TEST_ASSERT_EQUAL_INT(-45, d["rssi"].as<int>());
  TEST_ASSERT_EQUAL_UINT32(3605, d["uptime_s"].as<uint32_t>());
  TEST_ASSERT_EQUAL_INT(1, d["fan_level"].as<int>());
  TEST_ASSERT_TRUE(d["espnow_loss"].is<float>());
  // Làm tròn: in đúng chuỗi ngắn, không có đuôi 30.54000091
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"temperature\":30.5"));
  TEST_ASSERT_NULL(strstr(buf, "\"temperature\":30.54"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"humidity\":65.2"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"anomaly_score\":0.124"));
  TEST_ASSERT_NULL(strstr(buf, "null"));
  TEST_ASSERT_NULL(strstr(buf, "NaN"));
}

void test_telemetry_node_a_offline(void) {
  TelemetryIn in = full_in();
  in.node_a_online = false;
  char buf[512];
  TEST_ASSERT_TRUE(telemetry_build(&in, buf, sizeof(buf)) > 0);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, buf));
  const char *gone[] = {"seq", "temperature", "humidity", "light", "presence",
                        "anomaly", "anomaly_score", "dht_fail", "espnow_loss"};
  for (size_t i = 0; i < sizeof(gone) / sizeof(gone[0]); i++) {
    TEST_ASSERT_TRUE_MESSAGE(d[gone[i]].isNull(), gone[i]);  // key phải KHÔNG có
  }
  TEST_ASSERT_FALSE(d["node_a_online"].as<bool>());  // key này VẪN phải có, = false
  TEST_ASSERT_TRUE(d["node_a_online"].is<bool>());
  TEST_ASSERT_TRUE(d["fan_level"].is<int>());
  TEST_ASSERT_TRUE(d["uptime_s"].is<int>());
}

void test_telemetry_omit_ts_and_score(void) {
  TelemetryIn in = full_in();
  in.has_ts = false;          // chưa có NTP
  in.anomaly_score = -1.0f;   // Node A chưa chạy TinyML
  char buf[512];
  TEST_ASSERT_TRUE(telemetry_build(&in, buf, sizeof(buf)) > 0);
  TEST_ASSERT_NULL(strstr(buf, "\"ts\""));
  TEST_ASSERT_NULL(strstr(buf, "anomaly_score"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"anomaly\":false"));  // anomaly vẫn gửi
}

void test_telemetry_small_buffer(void) {
  TelemetryIn in = full_in();
  char buf[40];
  TEST_ASSERT_EQUAL_size_t(0, telemetry_build(&in, buf, sizeof(buf)));
}

// ================= 9. resp_build =================
void test_resp_ok(void) {
  ParsedCmd c;
  cmd_parse(J("{\"id\":\"tb-17\",\"method\":\"setFan\",\"params\":2}"), &c);
  char buf[256];
  TEST_ASSERT_TRUE(resp_build(&c, CMD_OK, 2, false, LMODE_MANUAL, buf, sizeof(buf)) > 0);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, buf));
  TEST_ASSERT_EQUAL_STRING("tb-17", d["id"].as<const char *>());
  TEST_ASSERT_EQUAL_STRING("setFan", d["method"].as<const char *>());
  TEST_ASSERT_TRUE(d["ok"].as<bool>());
  TEST_ASSERT_TRUE(d["error"].isNull());  // thành công thì không có "error"
  TEST_ASSERT_NULL(strstr(buf, "error"));
  TEST_ASSERT_EQUAL_INT(2, d["state"]["fan_level"].as<int>());
  TEST_ASSERT_TRUE(d["state"]["light_on"].is<bool>());
  TEST_ASSERT_EQUAL_STRING("manual", d["state"]["mode"].as<const char *>());
}

void test_resp_ignored(void) {
  ParsedCmd c;
  cmd_parse(J("{\"id\":\"tb-18\",\"method\":\"autoFan\",\"params\":0}"), &c);
  char buf[256];
  TEST_ASSERT_TRUE(resp_build(&c, CMD_IGNORED_NOT_AUTO, 2, false, LMODE_MANUAL, buf, sizeof(buf)) > 0);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, buf));
  TEST_ASSERT_FALSE(d["ok"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("ignored_not_auto", d["error"].as<const char *>());
  TEST_ASSERT_EQUAL_INT(2, d["state"]["fan_level"].as<int>());
}

void test_resp_bad_json_id_null(void) {
  ParsedCmd c;
  TEST_ASSERT_EQUAL_INT(CMD_BAD_JSON, cmd_parse(J("abc"), &c));
  char buf[256];
  TEST_ASSERT_TRUE(resp_build(&c, CMD_BAD_JSON, 0, false, LMODE_AUTO, buf, sizeof(buf)) > 0);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, buf));
  TEST_ASSERT_TRUE(d["id"].isNull());
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"id\":null"));  // key id có mặt, giá trị null
  TEST_ASSERT_NULL(strstr(buf, "method"));             // không có method thì bỏ key
  TEST_ASSERT_EQUAL_STRING("bad_json", d["error"].as<const char *>());
  TEST_ASSERT_EQUAL_STRING("auto", d["state"]["mode"].as<const char *>());
}

void test_resp_small_buffer(void) {
  ParsedCmd c;
  cmd_parse(J("{\"id\":\"tb-17\",\"method\":\"setFan\",\"params\":2}"), &c);
  char buf[20];
  TEST_ASSERT_EQUAL_size_t(0, resp_build(&c, CMD_OK, 2, false, LMODE_MANUAL, buf, sizeof(buf)));
}

// ---------------- 7. cfg_tx_should_send (#43) ----------------
void test_cfgtx_no_peer(void) {
  CfgTxState s;
  cfg_tx_init(&s);
  TEST_ASSERT_FALSE(cfg_tx_should_send(&s, 600, 1000));
  TEST_ASSERT_FALSE(cfg_tx_should_send(&s, 600, 100000));  // 100 s vẫn không có Node A
}

void test_cfgtx_send_after_pair_once(void) {
  CfgTxState s;
  cfg_tx_init(&s);
  cfg_tx_on_pair(&s);
  TEST_ASSERT_TRUE(cfg_tx_should_send(&s, 600, 1000));
  TEST_ASSERT_FALSE(cfg_tx_should_send(&s, 600, 2000));
  TEST_ASSERT_FALSE(cfg_tx_should_send(&s, 600, 30999));
}

void test_cfgtx_send_on_change(void) {
  CfgTxState s;
  cfg_tx_init(&s);
  cfg_tx_on_pair(&s);
  TEST_ASSERT_TRUE(cfg_tx_should_send(&s, 600, 1000));
  TEST_ASSERT_TRUE(cfg_tx_should_send(&s, 30, 3000));    // đổi -> gửi ngay
  TEST_ASSERT_FALSE(cfg_tx_should_send(&s, 30, 4000));   // đã nhớ 30
  TEST_ASSERT_TRUE(cfg_tx_should_send(&s, 600, 5000));   // đổi lại cũng gửi
}

void test_cfgtx_resend_every_30s(void) {
  CfgTxState s;
  cfg_tx_init(&s);
  cfg_tx_on_pair(&s);
  TEST_ASSERT_TRUE(cfg_tx_should_send(&s, 30, 3000));
  TEST_ASSERT_FALSE(cfg_tx_should_send(&s, 30, 32999));
  TEST_ASSERT_TRUE(cfg_tx_should_send(&s, 30, 33000));
  TEST_ASSERT_FALSE(cfg_tx_should_send(&s, 30, 33001));  // mốc mới là 33000
  TEST_ASSERT_TRUE(cfg_tx_should_send(&s, 30, 63000));
}

void test_cfgtx_repair_sends_again(void) {
  CfgTxState s;
  cfg_tx_init(&s);
  cfg_tx_on_pair(&s);
  TEST_ASSERT_TRUE(cfg_tx_should_send(&s, 30, 3000));
  cfg_tx_on_pair(&s);                                    // Node A reset, ghép lại
  TEST_ASSERT_TRUE(cfg_tx_should_send(&s, 30, 4000));    // gửi dù cùng số, mới 1 s
  TEST_ASSERT_FALSE(cfg_tx_should_send(&s, 30, 5000));
}

void test_cfgtx_millis_wrap(void) {
  CfgTxState s;
  cfg_tx_init(&s);
  cfg_tx_on_pair(&s);
  uint32_t t0 = 0xFFFFC000u;  // 16,4 s trước khi millis() tràn
  TEST_ASSERT_TRUE(cfg_tx_should_send(&s, 30, t0));
  TEST_ASSERT_FALSE(cfg_tx_should_send(&s, 30, t0 + 29999u));  // đã tràn về số nhỏ
  TEST_ASSERT_TRUE(cfg_tx_should_send(&s, 30, t0 + 30000u));
}

static int run_all(void) {
  UNITY_BEGIN();
  // 1
  RUN_TEST(test_seq_first_packet_any_number);
  RUN_TEST(test_seq_dup_dropped);
  RUN_TEST(test_seq_gap_counts_lost);
  RUN_TEST(test_seq_node_a_restart);
  RUN_TEST(test_seq_loss_pct);
  // 2
  RUN_TEST(test_temp_avg);
  // 3
  RUN_TEST(test_thresholds_default_valid);
  RUN_TEST(test_thresholds_fan_order);
  RUN_TEST(test_thresholds_light_and_timeouts);
  // 4
  RUN_TEST(test_config_parse_ok);
  RUN_TEST(test_config_parse_float_for_int_keys);
  RUN_TEST(test_config_parse_keeps_old_on_error);
  RUN_TEST(test_config_parse_big_timeouts);
  RUN_TEST(test_config_parse_uses_len);
  // 5
  RUN_TEST(test_cmd_set_fan_ok);
  RUN_TEST(test_cmd_fan_bad_params);
  RUN_TEST(test_cmd_light);
  RUN_TEST(test_cmd_mode_and_get_state);
  RUN_TEST(test_cmd_unknown_and_bad_json);
  RUN_TEST(test_cmd_long_id_truncated);
  // 6
  RUN_TEST(test_ctrl_init);
  RUN_TEST(test_ctrl_manual_fan_switches_mode);
  RUN_TEST(test_ctrl_rule_only_in_auto);
  RUN_TEST(test_ctrl_bad_level);
  RUN_TEST(test_ctrl_set_mode);
  // 7
  RUN_TEST(test_tick_empty_room_turns_off);
  RUN_TEST(test_tick_node_a_lost_keeps_state);
  RUN_TEST(test_tick_manual_timeout);
  RUN_TEST(test_tick_manual_timeout_also_when_node_a_lost);
  RUN_TEST(test_tick_manual_timeout_millis_wrap);
  // 8
  RUN_TEST(test_telemetry_full);
  RUN_TEST(test_telemetry_node_a_offline);
  RUN_TEST(test_telemetry_omit_ts_and_score);
  RUN_TEST(test_telemetry_small_buffer);
  // 9
  RUN_TEST(test_resp_ok);
  RUN_TEST(test_resp_ignored);
  RUN_TEST(test_resp_bad_json_id_null);
  RUN_TEST(test_resp_small_buffer);
  // 7 (#43)
  RUN_TEST(test_cfgtx_no_peer);
  RUN_TEST(test_cfgtx_send_after_pair_once);
  RUN_TEST(test_cfgtx_send_on_change);
  RUN_TEST(test_cfgtx_resend_every_30s);
  RUN_TEST(test_cfgtx_repair_sends_again);
  RUN_TEST(test_cfgtx_millis_wrap);
  return UNITY_END();
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() {
  delay(2000);  // chờ USB CDC
  run_all();
}
void loop() {}
#else
int main(void) { return run_all(); }
#endif
