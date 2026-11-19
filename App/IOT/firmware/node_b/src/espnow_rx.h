// espnow_rx: nhận ESP-NOW từ Node A (CONTRACT mục 3, "Quy tắc nhận").
//   - PKT_PAIR_REQ (broadcast): thêm Node A làm peer, trả PKT_PAIR_RESP kèm channel WiFi.
//   - PKT_DATA: kiểm seq (trùng / mất / Node A khởi động lại), tính nhiệt độ trung bình,
//     đưa vào control, xin gửi telemetry.
// Phần lớn code lấy từ espnow_rx_test (#26, đã chạy đúng ở lab 13/11, bước L3–L7).
#pragma once
#include <Arduino.h>

// espnow_rx_init: tạo queue, esp_now_init, đăng ký callback nhận. Gọi SAU net_init()
// (radio phải ở chế độ STA). Ra false nếu lỗi.
bool espnow_rx_init();

// task_espnow: lấy gói từ queue, xử lý từng loại, 10 s in 1 dòng thống kê
// (nhận/mất/trùng/loss/ghép cặp). main.cpp tạo task này.
// Bộ đếm seq chỉ task này đọc/ghi nên không cần mutex.
void task_espnow(void *pvParameters);
