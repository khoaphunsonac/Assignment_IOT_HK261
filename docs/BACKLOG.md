# Việc làm thêm (quyết định ở buổi họp CN 29/11)

Chỉ làm khi IoT đã chạy end-to-end. Không ai tự làm trước khi nhóm chốt.

| Việc | Lợi | Chi phí | Điều kiện làm |
|---|---|---|---|
| Web app (Next.js/Express/MySQL) | Giao diện đẹp hơn. Điểm N06 đã dự kiến lấy từ Streamlit (T10), điểm nâng cao tối đa 1đ, nên web app gần như không thêm điểm | Lớn | IoT xong trước 29/11 |
| **T02 Modbus RS485** (ưu tiên): cảm biến RS485 nối UART của Node B | +1.0đ, có linh kiện, có slide thầy (Week3_Modbus_CAN_VI) | Thêm task Modbus trên Node B + 1 key telemetry + CONTRACT + widget. Không cần board thứ 3, không đụng radio | Có T không chắc đạt (T15, T14) hoặc dư thời gian |
| T01 Tasmota I2C (board thứ 3, thiết bị con `room1-node-c` qua Gateway) | +0.5đ | 3–5h chia 2–3 người, mỗi buổi demo mượn 3 board | Chỉ khi T02 không làm được |

Không làm:
- T03 BLE: dùng chung radio với ESP-NOW/WiFi, rủi ro làm hỏng T06; không có slide của thầy.
