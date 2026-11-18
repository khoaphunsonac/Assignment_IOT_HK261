// cmd: xử lý lệnh room1/cmd và trả lời room1/cmd/resp (CONTRACT mục 6).
#pragma once
#include <stddef.h>

// cmd_handle: gọi từ callback MQTT (task_net) cho MỖI tin trên room1/cmd.
//   Vào : payload, len = nội dung tin (không có '\0' cuối).
//   Làm : parse -> (nếu parse OK) áp lệnh qua control -> publish room1/cmd/resp
//         với trạng thái SAU khi làm lệnh. Luôn trả lời, kể cả bad_json (id null).
void cmd_handle(const char *payload, size_t len);
