// cmd.cpp - xem cmd.h. Logic đọc lệnh / tạo JSON nằm ở node_b_logic (đã có test);
// file này chỉ nối: cmd_parse -> control_apply_cmd -> resp_build -> net_publish.
#include "cmd.h"

#include <Arduino.h>

#include "control_ext.h"
#include "net.h"
#include "node_b_logic.h"

#define RESP_BUF 256  // resp dài nhất ~160 byte (id 39 ký tự + error + state)

void cmd_handle(const char *payload, size_t len) {
  // TODO 17: đọc lệnh -> làm -> trả lời. Ví dụ log:
  //   [CMD] id=test-1 method=setFan -> ok | fan=2 light=0 mode=manual
  //   [CMD] id=(null) method= -> bad_json | fan=2 light=0 mode=manual
  ParsedCmd c;
  CmdErr e = cmd_parse(payload, len, &c);
  if (e == CMD_OK) {
    // điền: e = control_apply_cmd(&c);   (đọc lỗi thì KHÔNG làm gì)
  }

  uint8_t fan;
  bool light;
  LMode mode;
  control_get_lstate(&fan, &light, &mode);  // trạng thái SAU khi làm lệnh

  char buf[RESP_BUF];
  size_t n = 0;
  // điền: n = resp_build(&c, e, fan, light, mode, buf, sizeof(buf));
  if (n == 0) {
    Serial.println("[CMD] LOI resp_build");
    return;
  }
  // điền: net_publish("room1/cmd/resp", buf, false);

  const char *es = cmd_err_str(e);  // NULL khi ok
  Serial.printf("[CMD] id=%s method=%s -> %s | fan=%u light=%d mode=%s\n",
                c.has_id ? c.id : "(null)", c.method_str, es ? es : "ok", fan, light,
                lmode_str(mode));
}
