// ota_logic.cpp - xem ota_logic.h (đọc .h trước, mỗi hàm có bảng ví dụ số).
// TODO 1–8 của Minh. Mỗi TODO xong chạy:  pio test -e native -f test_ota
// Hàm có ghi "VIẾT SẴN" thì không cần sửa.
#include "ota_logic.h"

#include <ArduinoJson.h>
#include <string.h>

// =====================================================================================
// TODO 1: ota_version_parse  (test_version_*, 5 test)
// =====================================================================================
bool ota_version_parse(const char *s, OtaVersion *out) {
  if (s == NULL || *s == '\0') return false;
  uint32_t part[3] = {0, 0, 0};
  int idx = 0;     // đang đọc số thứ mấy (0, 1, 2)
  int digits = 0;  // số chữ số của số đang đọc
  for (const char *p = s;; p++) {
    char c = *p;
    if (c >= '0' && c <= '9') {
      part[idx] = part[idx] * 10 + (uint32_t)(c - '0');
      digits++;
      if (part[idx] > 999U) return false;
    } else if (c == '.' || c == '\0') {
      if (digits == 0) return false;
      if (c == '\0') break;
      idx++;
      if (idx > 2) return false;
      digits = 0;
    } else {
      return false;
    }
  }
  if (idx != 2) return false;
  if (out != NULL) {
    out->major = (uint16_t)part[0];
    out->minor = (uint16_t)part[1];
    out->patch = (uint16_t)part[2];
  }
  return true;
}

// =====================================================================================
// TODO 2: ota_sha256_normalize  (test_sha_lower_ok, test_sha_upper_to_lower, test_sha_bad)
// =====================================================================================
bool ota_sha256_normalize(const char *in, char out[OTA_SHA_HEX_LEN + 1]) {
  if (in == NULL) return false;
  char tmp[OTA_SHA_HEX_LEN + 1];
  for (int i = 0; i < OTA_SHA_HEX_LEN; i++) {
    char c = in[i];
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) {
      tmp[i] = c;
    } else if (c >= 'A' && c <= 'F') {
      tmp[i] = (char)(c - 'A' + 'a');
    } else {
      return false;
    }
  }
  if (in[OTA_SHA_HEX_LEN] != '\0') return false;
  tmp[OTA_SHA_HEX_LEN] = '\0';
  if (out != NULL) {
    memcpy(out, tmp, sizeof(tmp));
  }
  return true;
}

// VIẾT SẴN (Claude). Mỗi byte -> 2 ký tự: 4 bit cao (d >> 4) và 4 bit thấp (d & 0x0F),
// tra bảng "0123456789abcdef". Ví dụ 0xAB -> 'a','b'; 0x0F -> '0','f'.
void ota_digest_to_hex(const uint8_t d[32], char out[OTA_SHA_HEX_LEN + 1]) {
  static const char HEX_CHARS[] = "0123456789abcdef";
  for (int i = 0; i < 32; i++) {
    out[2 * i] = HEX_CHARS[d[i] >> 4];
    out[2 * i + 1] = HEX_CHARS[d[i] & 0x0F];
  }
  out[OTA_SHA_HEX_LEN] = '\0';
}

// =====================================================================================
// TODO 3: ota_cmd_parse  (test_cmd_*, 10 test)
// =====================================================================================
OtaCmdResult ota_cmd_parse(const char *json, size_t len, OtaCmd *out) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len) || !doc.is<JsonObjectConst>()) return OTA_CMD_BAD_JSON;
  JsonObjectConst o = doc.as<JsonObjectConst>();
  OtaCmd tmp;
  memset(&tmp, 0, sizeof(tmp));

  // (a) version
  JsonVariantConst v_ver = o["version"];
  if (!v_ver.is<const char *>()) return OTA_CMD_BAD_FIELD;
  const char *ver = v_ver.as<const char *>();
  OtaVersion parsed_ver;
  if (strlen(ver) >= OTA_VERSION_MAX || !ota_version_parse(ver, &parsed_ver)) {
    return OTA_CMD_BAD_FIELD;
  }
  strcpy(tmp.version, ver);

  // (b) url
  JsonVariantConst v_url = o["url"];
  if (!v_url.is<const char *>()) return OTA_CMD_BAD_FIELD;
  const char *u = v_url.as<const char *>();
  size_t ulen = strlen(u);
  if (ulen >= OTA_URL_MAX || ulen <= 7 || strncmp(u, "http://", 7) != 0) {
    return OTA_CMD_BAD_FIELD;
  }
  strcpy(tmp.url, u);

  // (c) size
  JsonVariantConst v_size = o["size"];
  if (v_size.isNull() || v_size.is<bool>() || !v_size.is<uint32_t>()) {
    return OTA_CMD_BAD_FIELD;
  }
  uint32_t sz = v_size.as<uint32_t>();
  if (sz < 1U || sz > OTA_MAX_FW_SIZE) return OTA_CMD_BAD_FIELD;
  tmp.size = sz;

  // (d) sha256
  JsonVariantConst v_sha = o["sha256"];
  if (!v_sha.is<const char *>()) return OTA_CMD_BAD_FIELD;
  const char *sha = v_sha.as<const char *>();
  if (!ota_sha256_normalize(sha, tmp.sha256)) return OTA_CMD_BAD_FIELD;

  if (out != NULL) {
    *out = tmp;
  }
  return OTA_CMD_OK;
}

// =====================================================================================
// TODO 4: ota_decide  (test_decide, test_decide_compare_by_number)
// =====================================================================================
OtaDecision ota_decide(const OtaCmd *cmd, const char *running_version, bool busy) {
  if (busy) return OTA_DECIDE_BUSY;

  OtaVersion v_cmd, v_run;
  if (cmd != NULL &&
      ota_version_parse(cmd->version, &v_cmd) &&
      ota_version_parse(running_version, &v_run) &&
      v_cmd.major == v_run.major &&
      v_cmd.minor == v_run.minor &&
      v_cmd.patch == v_run.patch) {
    return OTA_DECIDE_SAME_VERSION;
  }

  return OTA_DECIDE_START;
}

// =====================================================================================
// TODO 5: ota_progress_next  (test_progress_*, 3 test)
// =====================================================================================
int ota_progress_next(uint32_t received, uint32_t total, int last_sent) {
  if (total == 0) return -1;
  uint64_t pct = (uint64_t)received * 100 / total;
  if (pct > 100) pct = 100;
  int mark = (int)(pct / 10 * 10);
  return (mark > last_sent) ? mark : -1;
}

// VIẾT SẴN (Claude). Chuỗi đúng bảng state / error của CONTRACT 9. switch không có
// default để trình biên dịch cảnh báo nếu sau này thêm giá trị enum mà quên thêm chuỗi.
const char *ota_state_str(OtaState s) {
  switch (s) {
    case OTA_ST_DOWNLOADING: return "downloading";
    case OTA_ST_VERIFYING: return "verifying";
    case OTA_ST_REBOOTING: return "rebooting";
    case OTA_ST_SUCCESS: return "success";
    case OTA_ST_FAILED: return "failed";
  }
  return "unknown";
}

const char *ota_error_str(OtaError e) {
  switch (e) {
    case OTA_ERR_NONE: return "none";
    case OTA_ERR_DOWNLOAD_FAILED: return "download_failed";
    case OTA_ERR_CHECKSUM_MISMATCH: return "checksum_mismatch";
    case OTA_ERR_WRITE_FAILED: return "write_failed";
    case OTA_ERR_ROLLED_BACK: return "rolled_back";
  }
  return "unknown";
}

// =====================================================================================
// TODO 6: ota_status_build  (test_status_*, 4 test)
// =====================================================================================
size_t ota_status_build(const char *version, OtaState state, int progress, OtaError err,
                        char *buf, size_t cap) {
  JsonDocument doc;
  doc["version"] = (version != NULL) ? version : "";
  doc["state"] = ota_state_str(state);
  if (state == OTA_ST_DOWNLOADING) {
    doc["progress"] = progress;
  }
  if (state == OTA_ST_FAILED) {
    doc["error"] = ota_error_str(err);
  }

  size_t needed = measureJson(doc);
  if (buf == NULL || needed + 1 > cap) return 0;
  return serializeJson(doc, buf, cap);
}

// =====================================================================================
// TODO 7: ota_boot_case  (test_boot_case_table)
// =====================================================================================
OtaBootCase ota_boot_case(const char *pending, const char *running, bool img_pending_verify) {
  if (img_pending_verify) {
    return OTA_BOOT_VERIFY_NEW;
  }
  if (pending == NULL || pending[0] == '\0') {
    return OTA_BOOT_NORMAL;
  }
  if (running != NULL && strcmp(pending, running) == 0) {
    return OTA_BOOT_REPORT_SUCCESS;
  }
  return OTA_BOOT_ROLLED_BACK;
}

// =====================================================================================
// TODO 8: ota_confirm_step  (test_confirm_basic, test_confirm_millis_wrap)
// =====================================================================================
OtaConfirm ota_confirm_step(uint32_t now_ms, uint32_t start_ms, bool wifi_ok, bool mqtt_ok,
                            uint32_t limit_ms) {
  if (wifi_ok && mqtt_ok) {
    return OTA_CONFIRM_OK;
  }
  if ((uint32_t)(now_ms - start_ms) >= limit_ms) {
    return OTA_CONFIRM_ROLLBACK;
  }
  return OTA_CONFIRM_WAIT;
}