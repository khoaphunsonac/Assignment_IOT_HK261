// Testcase cho lib/node_a_logic (KHÔNG sửa file này khi làm bài; test sai thì báo).
// Chạy trên PC:     pio test -e native     (cần gcc/g++ trong PATH)
// Chạy trên board:  pio test -e yolo_uno   (cắm board, không cần gcc)
#include <unity.h>
#include <math.h>
#include <string.h>
#include "node_a_logic.h"
#include "espnow_packet.h"

void setUp(void) {}
void tearDown(void) {}

// ---------------- 1. reading_valid ----------------
void test_valid_normal(void) {
  TEST_ASSERT_TRUE(reading_valid(30.5f, 65.2f));
}

void test_valid_nan(void) {
  TEST_ASSERT_FALSE(reading_valid(NAN, 65.0f));
  TEST_ASSERT_FALSE(reading_valid(30.0f, NAN));
}

void test_valid_edges(void) {
  TEST_ASSERT_TRUE(reading_valid(-10.0f, 0.0f));
  TEST_ASSERT_TRUE(reading_valid(60.0f, 100.0f));
  TEST_ASSERT_FALSE(reading_valid(-10.1f, 50.0f));
  TEST_ASSERT_FALSE(reading_valid(60.1f, 50.0f));
  TEST_ASSERT_FALSE(reading_valid(25.0f, -0.1f));
  TEST_ASSERT_FALSE(reading_valid(25.0f, 100.5f));
}

// ---------------- 2. presence ----------------
#define T60 60000u

void test_presence_boot_assumes_occupied(void) {
  PresenceState s;
  presence_init(&s, 0);
  TEST_ASSERT_EQUAL_UINT8(1, presence_update(&s, false, 0, T60));
  TEST_ASSERT_EQUAL_UINT8(1, presence_update(&s, false, 59999, T60));
  TEST_ASSERT_EQUAL_UINT8(0, presence_update(&s, false, 60000, T60));  // đủ 60 s -> trống
}

void test_presence_motion_restarts_timer(void) {
  PresenceState s;
  presence_init(&s, 0);
  TEST_ASSERT_EQUAL_UINT8(0, presence_update(&s, false, 65000, T60));
  TEST_ASSERT_EQUAL_UINT8(1, presence_update(&s, true, 70000, T60));   // có người vào
  TEST_ASSERT_EQUAL_UINT8(1, presence_update(&s, false, 129999, T60));
  TEST_ASSERT_EQUAL_UINT8(0, presence_update(&s, false, 130000, T60));
}

void test_presence_millis_wrap(void) {
  PresenceState s;
  uint32_t t0 = 0xFFFFFFF0u;  // 16 ms trước khi millis() tràn
  presence_init(&s, t0);
  TEST_ASSERT_EQUAL_UINT8(1, presence_update(&s, true, t0, T60));
  TEST_ASSERT_EQUAL_UINT8(1, presence_update(&s, false, 0x10u, T60));         // mới 32 ms
  TEST_ASSERT_EQUAL_UINT8(1, presence_update(&s, false, t0 + 59999u, T60));   // đã tràn
  TEST_ASSERT_EQUAL_UINT8(0, presence_update(&s, false, t0 + 60000u, T60));
}

// ---------------- 3. tx_on_result ----------------
void test_tx_ok(void) {
  TxState s;
  tx_init(&s);
  TEST_ASSERT_EQUAL_INT(TX_DONE, tx_on_result(&s, true, 0));
}

void test_tx_resend_3_then_give_up(void) {
  TxState s;
  tx_init(&s);
  TEST_ASSERT_EQUAL_INT(TX_RESEND, tx_on_result(&s, false, 0));
  TEST_ASSERT_EQUAL_INT(TX_RESEND, tx_on_result(&s, false, 10));
  TEST_ASSERT_EQUAL_INT(TX_RESEND, tx_on_result(&s, false, 20));
  TEST_ASSERT_EQUAL_INT(TX_GIVE_UP, tx_on_result(&s, false, 30));   // lần gửi thứ 4 vẫn lỗi
  // Mẫu sau (2 s sau) được đủ 3 lượt gửi lại mới
  TEST_ASSERT_EQUAL_INT(TX_RESEND, tx_on_result(&s, false, 2000));
}

void test_tx_ok_resets_chain(void) {
  TxState s;
  tx_init(&s);
  TEST_ASSERT_EQUAL_INT(TX_RESEND, tx_on_result(&s, false, 0));
  TEST_ASSERT_EQUAL_INT(TX_DONE, tx_on_result(&s, true, 5));
  // Chuỗi lỗi mới bắt đầu ở 9000, không tính từ 0
  TEST_ASSERT_EQUAL_INT(TX_RESEND, tx_on_result(&s, false, 9000));
  TEST_ASSERT_EQUAL_INT(TX_RESEND, tx_on_result(&s, false, 18999));
  TEST_ASSERT_EQUAL_INT(TX_REPAIR, tx_on_result(&s, false, 19000));
}

void test_tx_repair_after_10s_across_samples(void) {
  TxState s;
  tx_init(&s);
  // Mỗi mẫu 2 s: 4 lần gửi đều lỗi -> GIVE_UP, chuỗi lỗi vẫn tiếp tục
  for (uint32_t t = 0; t < 10000; t += 2000) {
    TEST_ASSERT_EQUAL_INT(TX_RESEND, tx_on_result(&s, false, t));
    TEST_ASSERT_EQUAL_INT(TX_RESEND, tx_on_result(&s, false, t + 1));
    TEST_ASSERT_EQUAL_INT(TX_RESEND, tx_on_result(&s, false, t + 2));
    TEST_ASSERT_EQUAL_INT(TX_GIVE_UP, tx_on_result(&s, false, t + 3));
  }
  TEST_ASSERT_EQUAL_INT(TX_REPAIR, tx_on_result(&s, false, 10000));
  // Sau REPAIR state về đầu: lỗi tiếp theo là chuỗi mới
  TEST_ASSERT_EQUAL_INT(TX_RESEND, tx_on_result(&s, false, 10500));
}

void test_tx_repair_has_priority(void) {
  TxState s;
  tx_init(&s);
  TEST_ASSERT_EQUAL_INT(TX_RESEND, tx_on_result(&s, false, 0));
  TEST_ASSERT_EQUAL_INT(TX_RESEND, tx_on_result(&s, false, 1));
  TEST_ASSERT_EQUAL_INT(TX_RESEND, tx_on_result(&s, false, 2));
  TEST_ASSERT_EQUAL_INT(TX_REPAIR, tx_on_result(&s, false, 10000));  // không phải GIVE_UP
}

void test_tx_millis_wrap(void) {
  TxState s;
  tx_init(&s);
  uint32_t t0 = 0xFFFFF000u;
  TEST_ASSERT_EQUAL_INT(TX_RESEND, tx_on_result(&s, false, t0));
  TEST_ASSERT_EQUAL_INT(TX_RESEND, tx_on_result(&s, false, t0 + 9999u));
  TEST_ASSERT_EQUAL_INT(TX_REPAIR, tx_on_result(&s, false, t0 + 10000u));
}

// ---------------- 4. config_pkt_parse (#43) ----------------
// Tạo 1 gói PKT_CONFIG đúng vào buf (8 byte), trả về độ dài.
static int make_cfg(uint8_t *buf, uint16_t timeout_s) {
  PktConfig pc;
  pc.hdr.magic = ESPNOW_MAGIC;
  pc.hdr.version = ESPNOW_VERSION;
  pc.hdr.type = PKT_CONFIG;
  pc.empty_timeout_s = timeout_s;
  memcpy(buf, &pc, sizeof(pc));
  return (int)sizeof(pc);
}

void test_cfgpkt_ok(void) {
  uint8_t buf[32];
  int len = make_cfg(buf, 30);
  uint16_t out = 999;
  TEST_ASSERT_TRUE(config_pkt_parse(buf, len, &out));
  TEST_ASSERT_EQUAL_UINT16(30, out);
}

void test_cfgpkt_bad_len_keeps_out(void) {
  uint8_t buf[32] = {0};
  make_cfg(buf, 30);
  uint16_t out = 999;
  TEST_ASSERT_FALSE(config_pkt_parse(buf, 7, &out));
  TEST_ASSERT_FALSE(config_pkt_parse(buf, 9, &out));
  TEST_ASSERT_FALSE(config_pkt_parse(buf, (int)sizeof(PktData), &out));
  TEST_ASSERT_EQUAL_UINT16(999, out);  // sai thì không ghi *out
}

void test_cfgpkt_bad_header(void) {
  uint8_t buf[32];
  uint16_t out = 999;
  PktConfig pc;
  int len = make_cfg(buf, 30);
  memcpy(&pc, buf, sizeof(pc)); pc.hdr.magic = 0x12345678u; memcpy(buf, &pc, sizeof(pc));
  TEST_ASSERT_FALSE(config_pkt_parse(buf, len, &out));
  make_cfg(buf, 30);
  memcpy(&pc, buf, sizeof(pc)); pc.hdr.version = ESPNOW_VERSION + 1; memcpy(buf, &pc, sizeof(pc));
  TEST_ASSERT_FALSE(config_pkt_parse(buf, len, &out));
  make_cfg(buf, 30);
  memcpy(&pc, buf, sizeof(pc)); pc.hdr.type = PKT_PAIR_RESP; memcpy(buf, &pc, sizeof(pc));
  TEST_ASSERT_FALSE(config_pkt_parse(buf, len, &out));
  TEST_ASSERT_EQUAL_UINT16(999, out);
}

void test_cfgpkt_timeout_range(void) {
  uint8_t buf[32];
  uint16_t out = 999;
  int len = make_cfg(buf, 0);
  TEST_ASSERT_FALSE(config_pkt_parse(buf, len, &out));
  TEST_ASSERT_EQUAL_UINT16(999, out);
  len = make_cfg(buf, 1);
  TEST_ASSERT_TRUE(config_pkt_parse(buf, len, &out));
  TEST_ASSERT_EQUAL_UINT16(1, out);
  len = make_cfg(buf, 65535);
  TEST_ASSERT_TRUE(config_pkt_parse(buf, len, &out));
  TEST_ASSERT_EQUAL_UINT16(65535, out);
}

// Gói nằm lệch căn lề (bắt đầu ở byte lẻ) vẫn phải đọc đúng: phải dùng memcpy.
void test_cfgpkt_unaligned(void) {
  uint8_t raw[33];
  make_cfg(raw + 1, 600);
  uint16_t out = 0;
  TEST_ASSERT_TRUE(config_pkt_parse(raw + 1, (int)sizeof(PktConfig), &out));
  TEST_ASSERT_EQUAL_UINT16(600, out);
}

static int run_all(void) {
  UNITY_BEGIN();
  RUN_TEST(test_valid_normal);
  RUN_TEST(test_valid_nan);
  RUN_TEST(test_valid_edges);
  RUN_TEST(test_presence_boot_assumes_occupied);
  RUN_TEST(test_presence_motion_restarts_timer);
  RUN_TEST(test_presence_millis_wrap);
  RUN_TEST(test_tx_ok);
  RUN_TEST(test_tx_resend_3_then_give_up);
  RUN_TEST(test_tx_ok_resets_chain);
  RUN_TEST(test_tx_repair_after_10s_across_samples);
  RUN_TEST(test_tx_repair_has_priority);
  RUN_TEST(test_tx_millis_wrap);
  // 4 (#43)
  RUN_TEST(test_cfgpkt_ok);
  RUN_TEST(test_cfgpkt_bad_len_keeps_out);
  RUN_TEST(test_cfgpkt_bad_header);
  RUN_TEST(test_cfgpkt_timeout_range);
  RUN_TEST(test_cfgpkt_unaligned);
  return UNITY_END();
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() {
  delay(2000);  // chờ USB CDC lên, không thì mất dòng đầu
  run_all();
}
void loop() {}
#else
int main(void) { return run_all(); }
#endif
