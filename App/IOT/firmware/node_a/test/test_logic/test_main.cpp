// Testcase cho lib/node_a_logic (KHÔNG sửa file này khi làm bài; test sai thì báo).
// Chạy trên PC:     pio test -e native     (cần gcc/g++ trong PATH)
// Chạy trên board:  pio test -e yolo_uno   (cắm board, không cần gcc)
#include <unity.h>
#include <math.h>
#include "node_a_logic.h"

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
