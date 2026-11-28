# Hướng dẫn sử dụng công cụ đo đạc (measure.py - Issue #67)

Tác giả: Tuấn (TV2 - Gateway)  
Mục đích: Thu thập số liệu thực nghiệm phục vụ **Chương 4 Báo cáo đồ án** theo yêu cầu mục 13 tài liệu dự án và mục 4 `TASKS_W3.md`.

---

## 1. Cấu hình ban đầu
Các biến môi trường được đọc từ `App/IOT/gateway/.env`:
```ini
MQTT_HOST=localhost
MQTT_PORT=1883
COREIOT_HOST=app.coreiot.io
COREIOT_USER=tuan.nguyen@example.com
COREIOT_PASSWORD=your_password
COREIOT_NODE_B_ID=your_device_id
```

---

## 2. Các lệnh đo con

### 2.1. Đo độ trễ dữ liệu lên Cloud (`latency_up`)
Đo độ trễ truyền bản tin từ Node B lên CoreIOT:
```bash
python App/IOT/tools/measure.py latency_up --n 5
```
* Tham số `--fake`: Chạy thử nhanh với `fake_node_b.py` trên máy.
* Kết quả lưu tại: `Report/data/latency_up.csv` (hoặc `latency_up_fake.csv`).

### 2.2. Đo độ trễ lệnh điều khiển hai chiều (`latency_cmd`)
Gửi lệnh `setFan` hai chiều và đo thời gian tới khi có phản hồi, tách riêng đoạn mạng Local và Cloud:
```bash
python App/IOT/tools/measure.py latency_cmd --n 5
```
* Tham số `--fake`: Chạy thử nhanh với `fake_node_b.py`.
* Kết quả lưu tại: `Report/data/latency_cmd.csv` (hoặc `latency_cmd_fake.csv`).

### 2.3. Đo thời gian tự phục hồi khi mất mạng (`recovery`)
Đo thời gian hệ thống khôi phục sau khi ngắt kết nối:
```bash
python App/IOT/tools/measure.py recovery --n 3
```
* Kịch bản: Làm theo hướng dẫn trên màn hình terminal: bấm Enter khi ngắt mạng (hotspot/bridge), bấm Enter khi bật lại để bắt đầu bấm giờ.
* Kết quả lưu tại: `Report/data/recovery.csv` (hoặc `recovery_fake.csv`).

---

## 3. Chạy kiểm thử tự động
```bash
pytest App/IOT/tools/test_measure.py -v
```
