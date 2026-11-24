// ota_logic.cpp - xem ota_logic.h: mỗi hàm có mô tả vào/ra và bảng ví dụ số.
// Test: pio test -e native -f test_ota (32 test, test/test_ota/test_main.cpp).
#include "ota_logic.h"

#include <ArduinoJson.h>
#include <string.h>

// =====================================================================================
// 1. ota_version_parse: "1.0.1" -> {1, 0, 1}
//   Duyệt từng ký tự, giữ 3 ô số part[0..2]; idx = ô đang đọc, digits = số chữ số của ô đó.
//   - Chữ số: cộng dồn vào part[idx], kiểm > 999 NGAY (không đợi hết chuỗi) để chuỗi rất
//     dài như "1.0.99999999999" không tràn uint32_t rồi lọt.
//   - '.' hoặc '\0': ô vừa xong phải có ít nhất 1 chữ số ("1..1", "1.0." sai); gặp '\0' thì
//     dừng; gặp '.' thì sang ô sau, quá 3 ô là sai.
//   - Ký tự khác (' ', 'v', '-'): sai.
//   Cuối cùng phải đủ đúng 3 ô (idx == 2) mới ghi *out.
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
// 2. ota_sha256_normalize: kiểm 64 ký tự hex, đổi A-F thành a-f
//   Chép từng ký tự vào tmp (A-F đổi thành chữ thường), gặp ký tự không phải hex là sai.
//   Chuỗi ngắn hơn 64 tự bị loại vì '\0' không phải hex; chuỗi dài hơn 64 bị loại nhờ kiểm
//   in[64] == '\0'. Đúng hết mới chép tmp sang out, nên lỗi thì out giữ nguyên.
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

// ota_digest_to_hex: mỗi byte -> 2 ký tự: 4 bit cao (d >> 4) và 4 bit thấp (d & 0x0F),
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
// 3. ota_cmd_parse: đọc lệnh room1/ota
//   deserializeJson với len (payload MQTT không có '\0'); không đọc được hoặc không phải
//   object -> BAD_JSON. Sau đó kiểm lần lượt version, url, size, sha256 vào biến tạm tmp,
//   sai chỗ nào trả BAD_FIELD ngay. Đủ 4 key hợp lệ mới gán *out = tmp, nên lệnh hỏng không
//   ghi đè dở dang lên lệnh cũ.
//   size: loại riêng bool trước (true/false), rồi is<uint32_t>() loại số thực (1.5), số âm,
//   chuỗi "702157" và số quá lớn; cuối cùng kiểm 1..OTA_MAX_FW_SIZE.
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
// 4. ota_decide: bắt đầu OTA, bỏ vì trùng bản, hay bỏ vì đang bận
//   Kiểm busy TRƯỚC (đang tải thì mọi lệnh mới đều bỏ). Sau đó so version theo SỐ
//   (major/minor/patch) nên "1.0.01" coi như "1.0.1". Bản đang chạy không đọc được thì coi
//   như khác -> START, để còn cứu board bằng OTA. Hạ bản vẫn START (CONTRACT 9 chỉ cấm trùng).
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
// 5. ota_progress_next: mốc % cần gửi "downloading" (0, 10, ..., 100) hoặc -1
//   pct = received * 100 / total tính bằng uint64_t (file 3 MB * 100 vượt uint32_t), làm
//   tròn xuống, tối đa 100; mốc = pct / 10 * 10. Chỉ trả mốc khi lớn hơn mốc đã gửi, nên 1
//   khối lớn nhảy qua nhiều mốc thì chỉ gửi mốc mới nhất. total = 0 -> -1 (tránh chia 0).
// =====================================================================================
int ota_progress_next(uint32_t received, uint32_t total, int last_sent) {
  if (total == 0) return -1;
  uint64_t pct = (uint64_t)received * 100 / total;
  if (pct > 100) pct = 100;
  int mark = (int)(pct / 10 * 10);
  return (mark > last_sent) ? mark : -1;
}

// ota_state_str / ota_error_str: chuỗi đúng bảng state / error của CONTRACT 9. switch không có
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
// 6. ota_status_build: JSON room1/ota/status
//   Gán key theo thứ tự version, state (ArduinoJson giữ đúng thứ tự gán); progress chỉ khi
//   downloading, error chỉ khi failed. measureJson cho số byte cần (chưa tính '\0'); thiếu
//   chỗ thì trả 0 thay vì ghi JSON bị cắt.
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
// 7. ota_boot_case: lúc khởi động, vừa OTA xong hay không (bảng quyết định ở .h)
//   Ảnh đang chờ xác nhận luôn thắng (không xác nhận thì lần reset sau bootloader tự quay
//   về). Còn lại: NVS rỗng -> bình thường; pending trùng bản đang chạy -> báo success còn
//   thiếu; pending khác -> bản mới đã bị quay về, báo rolled_back.
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
// 8. ota_confirm_step: trong 60 s đầu của bản mới
//   Nối đủ WiFi + MQTT -> OK (ưu tiên, kể cả lúc vừa quá hạn). Không thì so now - start
//   (phép trừ uint32_t, đúng cả khi millis() tràn) với limit: quá hạn -> ROLLBACK, chưa -> WAIT.
// =====================================================================================
OtaConfirm ota_confirm_step(uint32_t now_ms, uint32_t start_ms, bool wifi_ok, bool mqtt_ok,
                            uint32_t limit_ms) {
  if (wifi_ok && mqtt_ok) {
    return OTA_CONFIRM_OK;
  }
  if (now_ms - start_ms >= limit_ms) {
    return OTA_CONFIRM_ROLLBACK;
  }
  return OTA_CONFIRM_WAIT;
}