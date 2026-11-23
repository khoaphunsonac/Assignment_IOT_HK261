// Webserver Node B (#33 viết bản đứng riêng, #59 ghép vào Node B). Chỉ gọi hàm trong control.h.
#pragma once

// web_start: tạo task FreeRTOS "task_web" chạy webserver cổng 80. Gọi trong setup() sau net_init().
// Task tự chờ WiFi nối được rồi mới server.begin(), in IP ra Serial (net_init chỉ chờ WiFi 15 s).
// Lệnh từ web dùng CmdSource::Web: Node B vào manual, có đổi thì task_net gửi telemetry ngay.
void web_start();
