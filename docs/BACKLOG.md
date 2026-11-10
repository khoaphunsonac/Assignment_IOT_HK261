# Việc làm thêm (quyết định ở buổi họp CN 29/11)

Chỉ làm khi IoT đã chạy end-to-end. Không ai tự làm trước khi nhóm chốt.

| Việc | Lợi | Chi phí | Điều kiện làm |
|---|---|---|---|
| Web app (Next.js/Express/MySQL) | Giao diện đẹp hơn. Điểm N06 đã dự kiến lấy từ Streamlit (T10), điểm nâng cao tối đa 1đ, nên web app gần như không thêm điểm | Lớn | IoT xong trước 29/11 |
| **T02 Modbus RS485** (ưu tiên): cảm biến nhiệt độ/độ ẩm RS485 (Slave ID 4, 9600 8N1, FC03: thanh ghi 0 = độ ẩm ×10, 1 = nhiệt độ ×10) cắm **USB-RS485 vào laptop**, Gateway đọc bằng `minimalmodbus` | +1.0đ, có sẵn linh kiện + code mẫu Python + video hướng dẫn. Không đụng firmware Node A/B, không cần board thứ 3 | Gateway (TV2) thêm module đọc Modbus 2 s/lần, gửi lên CoreIOT thành thiết bị con `room1-modbus`; CONTRACT thêm key; Khoa thêm widget. ~2–3h | Có T không chắc đạt (T15, T14) hoặc dư thời gian |
| T01 Tasmota I2C (board thứ 3, thiết bị con `room1-node-c` qua Gateway) | +0.5đ | 3–5h chia 2–3 người, mỗi buổi demo mượn 3 board | Chỉ khi T02 không làm được |

Không làm:
- T03 BLE: dùng chung radio với ESP-NOW/WiFi, rủi ro làm hỏng T06; không có slide của thầy.
