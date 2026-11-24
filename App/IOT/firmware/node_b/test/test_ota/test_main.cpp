// Testcase cho lib/ota_logic (#58), 32 test.
// Chạy trên PC:  pio test -e native -f test_ota     (chỉ chạy nhóm OTA)
//                pio test -e native                 (chạy cả test_logic của #42)
//
// Nhóm test (tên hàm test bắt đầu bằng):
//   version_   sha_   cmd_   decide_   progress_   status_   boot_   confirm_
// Test FAIL: Unity in tên hàm test + số dòng TEST_ASSERT bị sai (`pio test -e native -f test_ota -v`).
#include <unity.h>
#include <string.h>
#include <ArduinoJson.h>
#include "ota_logic.h"

void setUp(void) {}
void tearDown(void) {}

// Chuỗi C -> (json, len) như payload MQTT.
#define J(s) (s), strlen(s)

// sha256 mẫu hợp lệ (64 ký tự): sha256 của chuỗi rỗng.
#define SHA_LOW "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
#define SHA_UP  "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855"

// Lệnh hợp lệ mẫu (CONTRACT 9).
#define CMD_OK "{\"version\":\"1.0.1\",\"url\":\"http://10.199.40.112:8000/fw/node_b_1.0.1.bin\"," \
               "\"size\":702157,\"sha256\":\"" SHA_LOW "\"}"

// ================= 1. version =================
void test_version_ok(void) {
  OtaVersion v;
  TEST_ASSERT_TRUE(ota_version_parse("1.0.1", &v));
  TEST_ASSERT_EQUAL_UINT16(1, v.major);
  TEST_ASSERT_EQUAL_UINT16(0, v.minor);
  TEST_ASSERT_EQUAL_UINT16(1, v.patch);
  TEST_ASSERT_TRUE(ota_version_parse("10.20.300", &v));
  TEST_ASSERT_EQUAL_UINT16(10, v.major);
  TEST_ASSERT_EQUAL_UINT16(20, v.minor);
  TEST_ASSERT_EQUAL_UINT16(300, v.patch);
  TEST_ASSERT_TRUE(ota_version_parse("0.0.0", &v));
  TEST_ASSERT_TRUE(ota_version_parse("999.999.999", &v));
}

void test_version_wrong_count(void) {
  OtaVersion v;
  TEST_ASSERT_FALSE(ota_version_parse("1.0", &v));
  TEST_ASSERT_FALSE(ota_version_parse("1", &v));
  TEST_ASSERT_FALSE(ota_version_parse("1.0.1.2", &v));
  TEST_ASSERT_FALSE(ota_version_parse("1.0.1.", &v));
}

void test_version_bad_chars(void) {
  OtaVersion v;
  TEST_ASSERT_FALSE(ota_version_parse("1..1", &v));
  TEST_ASSERT_FALSE(ota_version_parse(".1.1", &v));
  TEST_ASSERT_FALSE(ota_version_parse("1.0.x", &v));
  TEST_ASSERT_FALSE(ota_version_parse(" 1.0.1", &v));
  TEST_ASSERT_FALSE(ota_version_parse("1.0.1 ", &v));
  TEST_ASSERT_FALSE(ota_version_parse("v1.0.1", &v));
  TEST_ASSERT_FALSE(ota_version_parse("-1.0.1", &v));
  TEST_ASSERT_FALSE(ota_version_parse("1.0.1-beta", &v));
}

void test_version_empty_null_too_big(void) {
  OtaVersion v;
  TEST_ASSERT_FALSE(ota_version_parse("", &v));
  TEST_ASSERT_FALSE(ota_version_parse(NULL, &v));
  TEST_ASSERT_FALSE(ota_version_parse("1.0.1000", &v));
  TEST_ASSERT_FALSE(ota_version_parse("1.0.99999999999", &v));  // không được tràn số rồi lọt
}

void test_version_out_untouched_on_error(void) {
  OtaVersion v = {7, 8, 9};
  TEST_ASSERT_FALSE(ota_version_parse("1.2", &v));
  TEST_ASSERT_EQUAL_UINT16(7, v.major);
  TEST_ASSERT_EQUAL_UINT16(8, v.minor);
  TEST_ASSERT_EQUAL_UINT16(9, v.patch);
}

// ================= 2. sha256 =================
void test_sha_lower_ok(void) {
  char out[OTA_SHA_HEX_LEN + 1];
  TEST_ASSERT_TRUE(ota_sha256_normalize(SHA_LOW, out));
  TEST_ASSERT_EQUAL_STRING(SHA_LOW, out);
}

void test_sha_upper_to_lower(void) {
  char out[OTA_SHA_HEX_LEN + 1];
  TEST_ASSERT_TRUE(ota_sha256_normalize(SHA_UP, out));  // Get-FileHash ra chữ HOA
  TEST_ASSERT_EQUAL_STRING(SHA_LOW, out);
}

void test_sha_bad(void) {
  char out[OTA_SHA_HEX_LEN + 1] = "giu_nguyen";
  // 63 ký tự
  TEST_ASSERT_FALSE(ota_sha256_normalize(
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b85", out));
  // 65 ký tự
  TEST_ASSERT_FALSE(ota_sha256_normalize(SHA_LOW "0", out));
  // có 'g'
  TEST_ASSERT_FALSE(ota_sha256_normalize(
      "g3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", out));
  TEST_ASSERT_FALSE(ota_sha256_normalize("", out));
  TEST_ASSERT_FALSE(ota_sha256_normalize(NULL, out));
  TEST_ASSERT_EQUAL_STRING("giu_nguyen", out);  // lỗi thì không ghi out
}

void test_sha_digest_to_hex(void) {
  uint8_t d[32];
  for (int i = 0; i < 32; i++) d[i] = (uint8_t)(i * 8 + 0x0F);
  char out[OTA_SHA_HEX_LEN + 1];
  ota_digest_to_hex(d, out);
  TEST_ASSERT_EQUAL_size_t(64, strlen(out));
  TEST_ASSERT_EQUAL_STRING_LEN("0f171f27", out, 8);
}

// ================= 3. lệnh room1/ota =================
void test_cmd_ok(void) {
  OtaCmd c;
  TEST_ASSERT_EQUAL_INT(OTA_CMD_OK, ota_cmd_parse(J(CMD_OK), &c));
  TEST_ASSERT_EQUAL_STRING("1.0.1", c.version);
  TEST_ASSERT_EQUAL_STRING("http://10.199.40.112:8000/fw/node_b_1.0.1.bin", c.url);
  TEST_ASSERT_EQUAL_UINT32(702157, c.size);
  TEST_ASSERT_EQUAL_STRING(SHA_LOW, c.sha256);
}

void test_cmd_upper_sha_and_extra_keys(void) {
  OtaCmd c;
  const char *j = "{\"fw_title\":\"node-b-fw\",\"version\":\"1.0.2\",\"url\":\"http://1.2.3.4/a.bin\","
                  "\"size\":1,\"sha256\":\"" SHA_UP "\"}";
  TEST_ASSERT_EQUAL_INT(OTA_CMD_OK, ota_cmd_parse(J(j), &c));
  TEST_ASSERT_EQUAL_STRING(SHA_LOW, c.sha256);
  TEST_ASSERT_EQUAL_UINT32(1, c.size);
}

void test_cmd_bad_json(void) {
  OtaCmd c;
  TEST_ASSERT_EQUAL_INT(OTA_CMD_BAD_JSON, ota_cmd_parse(J("abc"), &c));
  TEST_ASSERT_EQUAL_INT(OTA_CMD_BAD_JSON, ota_cmd_parse(J("[1,2]"), &c));
  TEST_ASSERT_EQUAL_INT(OTA_CMD_BAD_JSON, ota_cmd_parse(J("{\"version\":"), &c));
}

void test_cmd_missing_key(void) {
  OtaCmd c;
  TEST_ASSERT_EQUAL_INT(OTA_CMD_BAD_FIELD, ota_cmd_parse(J(
      "{\"url\":\"http://a/b\",\"size\":10,\"sha256\":\"" SHA_LOW "\"}"), &c));
  TEST_ASSERT_EQUAL_INT(OTA_CMD_BAD_FIELD, ota_cmd_parse(J(
      "{\"version\":\"1.0.1\",\"size\":10,\"sha256\":\"" SHA_LOW "\"}"), &c));
  TEST_ASSERT_EQUAL_INT(OTA_CMD_BAD_FIELD, ota_cmd_parse(J(
      "{\"version\":\"1.0.1\",\"url\":\"http://a/b\",\"sha256\":\"" SHA_LOW "\"}"), &c));
  TEST_ASSERT_EQUAL_INT(OTA_CMD_BAD_FIELD, ota_cmd_parse(J(
      "{\"version\":\"1.0.1\",\"url\":\"http://a/b\",\"size\":10}"), &c));
}

void test_cmd_bad_version_url(void) {
  OtaCmd c;
  TEST_ASSERT_EQUAL_INT(OTA_CMD_BAD_FIELD, ota_cmd_parse(J(
      "{\"version\":101,\"url\":\"http://a/b\",\"size\":10,\"sha256\":\"" SHA_LOW "\"}"), &c));
  TEST_ASSERT_EQUAL_INT(OTA_CMD_BAD_FIELD, ota_cmd_parse(J(
      "{\"version\":\"1.0\",\"url\":\"http://a/b\",\"size\":10,\"sha256\":\"" SHA_LOW "\"}"), &c));
  TEST_ASSERT_EQUAL_INT(OTA_CMD_BAD_FIELD, ota_cmd_parse(J(
      "{\"version\":\"1.0.1\",\"url\":\"https://a/b\",\"size\":10,\"sha256\":\"" SHA_LOW "\"}"), &c));
  TEST_ASSERT_EQUAL_INT(OTA_CMD_BAD_FIELD, ota_cmd_parse(J(
      "{\"version\":\"1.0.1\",\"url\":\"http://\",\"size\":10,\"sha256\":\"" SHA_LOW "\"}"), &c));
  TEST_ASSERT_EQUAL_INT(OTA_CMD_BAD_FIELD, ota_cmd_parse(J(
      "{\"version\":\"1.0.1\",\"url\":\"ftp://a/b\",\"size\":10,\"sha256\":\"" SHA_LOW "\"}"), &c));
}

void test_cmd_url_too_long(void) {
  OtaCmd c;
  // url dài đúng OTA_URL_MAX ký tự (không còn chỗ cho '\0') -> lỗi; ngắn hơn 1 -> OK
  char url[OTA_URL_MAX + 1];
  strcpy(url, "http://");
  memset(url + 7, 'a', OTA_URL_MAX - 7);
  url[OTA_URL_MAX] = '\0';
  char j[512];
  snprintf(j, sizeof(j), "{\"version\":\"1.0.1\",\"url\":\"%s\",\"size\":10,\"sha256\":\"%s\"}", url, SHA_LOW);
  TEST_ASSERT_EQUAL_INT(OTA_CMD_BAD_FIELD, ota_cmd_parse(J(j), &c));
  url[OTA_URL_MAX - 1] = '\0';
  snprintf(j, sizeof(j), "{\"version\":\"1.0.1\",\"url\":\"%s\",\"size\":10,\"sha256\":\"%s\"}", url, SHA_LOW);
  TEST_ASSERT_EQUAL_INT(OTA_CMD_OK, ota_cmd_parse(J(j), &c));
  TEST_ASSERT_EQUAL_size_t(OTA_URL_MAX - 1, strlen(c.url));
}

void test_cmd_bad_size(void) {
  OtaCmd c;
  const char *bad[] = {
      "{\"version\":\"1.0.1\",\"url\":\"http://a/b\",\"size\":0,\"sha256\":\"" SHA_LOW "\"}",
      "{\"version\":\"1.0.1\",\"url\":\"http://a/b\",\"size\":-5,\"sha256\":\"" SHA_LOW "\"}",
      "{\"version\":\"1.0.1\",\"url\":\"http://a/b\",\"size\":1.5,\"sha256\":\"" SHA_LOW "\"}",
      "{\"version\":\"1.0.1\",\"url\":\"http://a/b\",\"size\":\"702157\",\"sha256\":\"" SHA_LOW "\"}",
      "{\"version\":\"1.0.1\",\"url\":\"http://a/b\",\"size\":true,\"sha256\":\"" SHA_LOW "\"}",
      "{\"version\":\"1.0.1\",\"url\":\"http://a/b\",\"size\":3342337,\"sha256\":\"" SHA_LOW "\"}",
      "{\"version\":\"1.0.1\",\"url\":\"http://a/b\",\"size\":5000000000,\"sha256\":\"" SHA_LOW "\"}",
  };
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(OTA_CMD_BAD_FIELD, ota_cmd_parse(J(bad[i]), &c), bad[i]);
  }
  // biên trên đúng bằng OTA_MAX_FW_SIZE (3342336) -> OK
  TEST_ASSERT_EQUAL_INT(OTA_CMD_OK, ota_cmd_parse(J(
      "{\"version\":\"1.0.1\",\"url\":\"http://a/b\",\"size\":3342336,\"sha256\":\"" SHA_LOW "\"}"), &c));
}

void test_cmd_bad_sha(void) {
  OtaCmd c;
  TEST_ASSERT_EQUAL_INT(OTA_CMD_BAD_FIELD, ota_cmd_parse(J(
      "{\"version\":\"1.0.1\",\"url\":\"http://a/b\",\"size\":10,\"sha256\":\"9f2c\"}"), &c));
  TEST_ASSERT_EQUAL_INT(OTA_CMD_BAD_FIELD, ota_cmd_parse(J(
      "{\"version\":\"1.0.1\",\"url\":\"http://a/b\",\"size\":10,\"sha256\":123}"), &c));
}

void test_cmd_keeps_old_on_error(void) {
  OtaCmd c;
  TEST_ASSERT_EQUAL_INT(OTA_CMD_OK, ota_cmd_parse(J(CMD_OK), &c));
  // lệnh hỏng ở key CUỐI (sha256): các key trước đúng, vẫn không được ghi đè dở dang
  TEST_ASSERT_EQUAL_INT(OTA_CMD_BAD_FIELD, ota_cmd_parse(J(
      "{\"version\":\"9.9.9\",\"url\":\"http://x/y\",\"size\":5,\"sha256\":\"zz\"}"), &c));
  TEST_ASSERT_EQUAL_STRING("1.0.1", c.version);
  TEST_ASSERT_EQUAL_STRING("http://10.199.40.112:8000/fw/node_b_1.0.1.bin", c.url);
  TEST_ASSERT_EQUAL_UINT32(702157, c.size);
}

void test_cmd_uses_len(void) {
  OtaCmd c;
  // payload MQTT không có '\0': phía sau lệnh còn rác. Phải đọc đúng len byte.
  char buf[400];
  strcpy(buf, CMD_OK);
  size_t n = strlen(buf);
  strcat(buf, "RAC_RAC");
  TEST_ASSERT_EQUAL_INT(OTA_CMD_OK, ota_cmd_parse(buf, n, &c));
}

// ================= 4. decide =================
void test_decide(void) {
  OtaCmd c;
  ota_cmd_parse(J(CMD_OK), &c);  // version 1.0.1
  TEST_ASSERT_EQUAL_INT(OTA_DECIDE_START, ota_decide(&c, "1.0.0", false));
  TEST_ASSERT_EQUAL_INT(OTA_DECIDE_SAME_VERSION, ota_decide(&c, "1.0.1", false));
  TEST_ASSERT_EQUAL_INT(OTA_DECIDE_BUSY, ota_decide(&c, "1.0.0", true));
  TEST_ASSERT_EQUAL_INT(OTA_DECIDE_BUSY, ota_decide(&c, "1.0.1", true));  // busy kiểm trước
  TEST_ASSERT_EQUAL_INT(OTA_DECIDE_START, ota_decide(&c, "1.0.2", false));  // hạ bản vẫn cho
}

void test_decide_compare_by_number(void) {
  OtaCmd c;
  ota_cmd_parse(J(CMD_OK), &c);
  strcpy(c.version, "1.0.01");  // số giống 1.0.1
  TEST_ASSERT_EQUAL_INT(OTA_DECIDE_SAME_VERSION, ota_decide(&c, "1.0.1", false));
  strcpy(c.version, "1.0.10");
  TEST_ASSERT_EQUAL_INT(OTA_DECIDE_START, ota_decide(&c, "1.0.1", false));
  strcpy(c.version, "1.0.1");
  TEST_ASSERT_EQUAL_INT(OTA_DECIDE_START, ota_decide(&c, "abc", false));  // running hỏng -> START
}

// ================= 5. progress =================
void test_progress_steps(void) {
  TEST_ASSERT_EQUAL_INT(0, ota_progress_next(0, 1000, -1));
  TEST_ASSERT_EQUAL_INT(-1, ota_progress_next(50, 1000, 0));
  TEST_ASSERT_EQUAL_INT(10, ota_progress_next(100, 1000, 0));
  TEST_ASSERT_EQUAL_INT(-1, ota_progress_next(150, 1000, 10));
  TEST_ASSERT_EQUAL_INT(30, ota_progress_next(350, 1000, 10));  // nhảy qua 20
  TEST_ASSERT_EQUAL_INT(-1, ota_progress_next(999, 1000, 90));
  TEST_ASSERT_EQUAL_INT(100, ota_progress_next(1000, 1000, 90));
  TEST_ASSERT_EQUAL_INT(-1, ota_progress_next(1000, 1000, 100));
}

void test_progress_edges(void) {
  TEST_ASSERT_EQUAL_INT(-1, ota_progress_next(0, 0, -1));       // total 0
  TEST_ASSERT_EQUAL_INT(-1, ota_progress_next(1200, 1000, 100)); // vượt total
  TEST_ASSERT_EQUAL_INT(100, ota_progress_next(1200, 1000, 50));
  // file lớn: 3 MB * 100 tràn uint32_t nếu nhân thẳng
  TEST_ASSERT_EQUAL_INT(50, ota_progress_next(1600000000UL, 3200000000UL, 40));
  TEST_ASSERT_EQUAL_INT(90, ota_progress_next(3000000, 3300000, 80));
}

void test_progress_count_full_run(void) {
  // Tải file 702157 byte theo khối 1024: phải gửi đúng 11 lần (0,10,...,100), tăng dần.
  uint32_t total = 702157, got = 0;
  int last = -1, count = 0, prev = -1;
  while (got < total) {
    uint32_t chunk = total - got < 1024 ? total - got : 1024;
    got += chunk;
    int m = ota_progress_next(got, total, last);
    if (m >= 0) {
      TEST_ASSERT_TRUE(m > prev);
      prev = m;
      last = m;
      count++;
    }
  }
  // khối đầu 1024/702157 = 0% -> gửi 0
  TEST_ASSERT_EQUAL_INT(11, count);
  TEST_ASSERT_EQUAL_INT(100, last);
}

// ================= 6. status JSON =================
void test_status_downloading(void) {
  char buf[128];
  size_t n = ota_status_build("1.0.1", OTA_ST_DOWNLOADING, 45, OTA_ERR_NONE, buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
  TEST_ASSERT_EQUAL_STRING("{\"version\":\"1.0.1\",\"state\":\"downloading\",\"progress\":45}", buf);
}

void test_status_failed_errors(void) {
  char buf[128];
  ota_status_build("1.0.1", OTA_ST_FAILED, 0, OTA_ERR_CHECKSUM_MISMATCH, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("{\"version\":\"1.0.1\",\"state\":\"failed\",\"error\":\"checksum_mismatch\"}", buf);
  ota_status_build("1.0.1", OTA_ST_FAILED, 0, OTA_ERR_DOWNLOAD_FAILED, buf, sizeof(buf));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"error\":\"download_failed\""));
  ota_status_build("1.0.1", OTA_ST_FAILED, 0, OTA_ERR_WRITE_FAILED, buf, sizeof(buf));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"error\":\"write_failed\""));
  ota_status_build("1.0.1", OTA_ST_FAILED, 0, OTA_ERR_ROLLED_BACK, buf, sizeof(buf));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"error\":\"rolled_back\""));
  TEST_ASSERT_NULL(strstr(buf, "progress"));
}

void test_status_other_states(void) {
  char buf[128];
  ota_status_build("1.0.1", OTA_ST_VERIFYING, 77, OTA_ERR_WRITE_FAILED, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("{\"version\":\"1.0.1\",\"state\":\"verifying\"}", buf);  // bỏ progress, error
  ota_status_build("1.0.1", OTA_ST_REBOOTING, 0, OTA_ERR_NONE, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("{\"version\":\"1.0.1\",\"state\":\"rebooting\"}", buf);
  ota_status_build("1.0.1", OTA_ST_SUCCESS, 0, OTA_ERR_NONE, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("{\"version\":\"1.0.1\",\"state\":\"success\"}", buf);
}

void test_status_small_buffer(void) {
  // {"version":"1.0.1","state":"success"} = 37 ký tự, cần 38 byte cả '\0'
  char buf[38];
  TEST_ASSERT_EQUAL_size_t(37, ota_status_build("1.0.1", OTA_ST_SUCCESS, 0, OTA_ERR_NONE, buf, 38));
  TEST_ASSERT_EQUAL_size_t(0, ota_status_build("1.0.1", OTA_ST_SUCCESS, 0, OTA_ERR_NONE, buf, 37));
  TEST_ASSERT_EQUAL_size_t(0, ota_status_build("1.0.1", OTA_ST_SUCCESS, 0, OTA_ERR_NONE, buf, 10));
}

void test_status_str_helpers(void) {
  TEST_ASSERT_EQUAL_STRING("downloading", ota_state_str(OTA_ST_DOWNLOADING));
  TEST_ASSERT_EQUAL_STRING("rolled_back", ota_error_str(OTA_ERR_ROLLED_BACK));
}

// ================= 7. boot case =================
void test_boot_case_table(void) {
  TEST_ASSERT_EQUAL_INT(OTA_BOOT_VERIFY_NEW, ota_boot_case("1.0.1", "1.0.1", true));
  TEST_ASSERT_EQUAL_INT(OTA_BOOT_VERIFY_NEW, ota_boot_case("", "1.0.1", true));  // nạp espota
  TEST_ASSERT_EQUAL_INT(OTA_BOOT_NORMAL, ota_boot_case("", "1.0.0", false));
  TEST_ASSERT_EQUAL_INT(OTA_BOOT_REPORT_SUCCESS, ota_boot_case("1.0.1", "1.0.1", false));
  TEST_ASSERT_EQUAL_INT(OTA_BOOT_ROLLED_BACK, ota_boot_case("1.0.1", "1.0.0", false));
}

// ================= 8. confirm =================
void test_confirm_basic(void) {
  TEST_ASSERT_EQUAL_INT(OTA_CONFIRM_WAIT, ota_confirm_step(30000, 1000, true, false, 60000));
  TEST_ASSERT_EQUAL_INT(OTA_CONFIRM_WAIT, ota_confirm_step(60999, 1000, false, false, 60000));
  TEST_ASSERT_EQUAL_INT(OTA_CONFIRM_ROLLBACK, ota_confirm_step(61000, 1000, true, false, 60000));
  TEST_ASSERT_EQUAL_INT(OTA_CONFIRM_OK, ota_confirm_step(5000, 1000, true, true, 60000));
  TEST_ASSERT_EQUAL_INT(OTA_CONFIRM_OK, ota_confirm_step(61000, 1000, true, true, 60000));  // OK ưu tiên
  TEST_ASSERT_EQUAL_INT(OTA_CONFIRM_WAIT, ota_confirm_step(5000, 1000, false, true, 60000));  // mqtt mà mất wifi
}

void test_confirm_millis_wrap(void) {
  // start ngay trước khi millis() tràn, now sau khi tràn: thực tế mới trôi 0x2000 ms = 8,2 s
  TEST_ASSERT_EQUAL_INT(OTA_CONFIRM_WAIT, ota_confirm_step(0x00001000UL, 0xFFFFF000UL, false, false, 60000));
  // trôi 0x10000 ms = 65,5 s -> quá hạn
  TEST_ASSERT_EQUAL_INT(OTA_CONFIRM_ROLLBACK, ota_confirm_step(0x00008000UL, 0xFFFF8000UL, false, false, 60000));
}

int run_all(void) {
  UNITY_BEGIN();
  // 1
  RUN_TEST(test_version_ok);
  RUN_TEST(test_version_wrong_count);
  RUN_TEST(test_version_bad_chars);
  RUN_TEST(test_version_empty_null_too_big);
  RUN_TEST(test_version_out_untouched_on_error);
  // 2
  RUN_TEST(test_sha_lower_ok);
  RUN_TEST(test_sha_upper_to_lower);
  RUN_TEST(test_sha_bad);
  RUN_TEST(test_sha_digest_to_hex);
  // 3
  RUN_TEST(test_cmd_ok);
  RUN_TEST(test_cmd_upper_sha_and_extra_keys);
  RUN_TEST(test_cmd_bad_json);
  RUN_TEST(test_cmd_missing_key);
  RUN_TEST(test_cmd_bad_version_url);
  RUN_TEST(test_cmd_url_too_long);
  RUN_TEST(test_cmd_bad_size);
  RUN_TEST(test_cmd_bad_sha);
  RUN_TEST(test_cmd_keeps_old_on_error);
  RUN_TEST(test_cmd_uses_len);
  // 4
  RUN_TEST(test_decide);
  RUN_TEST(test_decide_compare_by_number);
  // 5
  RUN_TEST(test_progress_steps);
  RUN_TEST(test_progress_edges);
  RUN_TEST(test_progress_count_full_run);
  // 6
  RUN_TEST(test_status_downloading);
  RUN_TEST(test_status_failed_errors);
  RUN_TEST(test_status_other_states);
  RUN_TEST(test_status_small_buffer);
  RUN_TEST(test_status_str_helpers);
  // 7
  RUN_TEST(test_boot_case_table);
  // 8
  RUN_TEST(test_confirm_basic);
  RUN_TEST(test_confirm_millis_wrap);
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
