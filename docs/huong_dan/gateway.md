# Hướng dẫn chạy và vận hành Gateway (TV2 - Tuấn)

Tài liệu hướng dẫn triển khai, cấu hình và vận hành tầng **Gateway** trong hệ thống BTL IoT HK261.  
Khớp 100% yêu cầu tại `docs/TASKS_W3.md` mục 7 và kiến trúc `docs/CONTRACT.md`.

---

## 1. Yêu cầu cài đặt môi trường

### 1.1. Cài đặt Mosquitto MQTT Broker
- Cài đặt Mosquitto (Windows: `C:\Program Files\mosquitto\mosquitto.exe`).
- Khởi động broker lắng nghe cổng `1883`:
  ```powershell
  & "C:\Program Files\mosquitto\mosquitto.exe" -c App\IOT\gateway\mosquitto.conf -v
  ```

### 1.2. Cài đặt thư viện Python
Sử dụng Python 3.10+:
```bash
pip install paho-mqtt scikit-learn joblib pytest
```

### 1.3. Cấu hình biến môi trường (`.env`)
Sao chép file mẫu và cấu hình:
```bash
cp App/IOT/gateway/.env.example App/IOT/gateway/.env
```
Điền các giá trị vào `App/IOT/gateway/.env`:
```ini
MQTT_HOST=localhost
MQTT_PORT=1883
COREIOT_HOST=app.coreiot.io
COREIOT_PORT=1883
COREIOT_TOKEN=YOUR_GATEWAY_ACCESS_TOKEN
GATEWAY_IP=192.168.1.100    # Đổi thành IP máy tính trong mạng WiFi/Hotspot khi chạy với board thật
OTA_PORT=8000
```

---

## 2. Các lệnh chạy chi tiết

### 2.1. Chạy Gateway Bridge chính (`bridge.py`)
Khởi động cầu nối giữa Mosquitto Local và CoreIOT Cloud (bao gồm cả HTTP Server OTA và cơ chế Lưu tạm & Gửi bù):
```bash
python App/IOT/gateway/bridge.py
```
**Kết quả đúng trông thế nào:**
```text
15:30:00 [INFO] [Gateway] === Khởi động Gateway Bridge (Tuấn - TV2) ===
15:30:00 [INFO] [Gateway] Local MQTT : localhost:1883 (Client ID: room1-gateway)
15:30:00 [INFO] [Gateway] CoreIOT    : app.coreiot.io:1883 (Token: Đã có)
15:30:00 [INFO] [Gateway] OTA Server : http://192.168.1.100:8000/fw/ (Phát file firmware)
15:30:00 [INFO] [Gateway] Buffer T15 : 0 bản ghi đang lưu tạm (File: buffer.jsonl)
15:30:00 [INFO] [Gateway] Đã kết nối tới Local Broker (localhost:1883)
15:30:01 [INFO] [Gateway] Đã kết nối tới CoreIOT Cloud (app.coreiot.io:1883)
15:30:01 [INFO] [Gateway] CoreIOT: Đã gửi v1/gateway/connect cho room1-node-a và room1-node-b
```

### 2.2. Kiểm thử cơ chế Lưu tạm khi mất mạng & Gửi bù (Store-and-Forward - T15)
- Khi đang chạy `bridge.py`, ngắt kết nối mạng hoặc đổi sai `COREIOT_HOST`:
  ```text
  [WARNING] [Gateway] CoreIOT: Bị ngắt kết nối bất thường. Paho sẽ tự động kết nối lại...
  [INFO] [Gateway] CoreIOT Offline: Đã lưu tạm 1 bản telemetry vào buffer (Tổng: 1/1800)
  ```
- Khi khôi phục kết nối mạng:
  ```text
  [INFO] [Gateway] Đã kết nối lại tới CoreIOT Cloud
  [INFO] [Gateway] CoreIOT Store-and-Forward: Bắt đầu gửi bù 50 bản ghi từ buffer...
  [INFO] [Gateway] CoreIOT Store-and-Forward: Đã gửi bù hoàn tất toàn bộ dữ liệu!
  ```

### 2.3. Phát hành Firmware OTA thủ công (`ota_publish.py`)
Đặt file firmware `.bin` vào thư mục `App/IOT/gateway/fw/`, sau đó chạy:
```bash
python App/IOT/gateway/ota_publish.py fw/node_b_1.0.1.bin 1.0.1
```
**Kết quả đúng:**
```text
[INFO] [OTA-Publish] Chuẩn bị phát hành Firmware OTA:
  - File    : node_b_1.0.1.bin
  - Version : 1.0.1
  - Size    : 702157 bytes
  - SHA256  : 9f2c...e81a
  - URL     : http://192.168.1.100:8000/fw/node_b_1.0.1.bin
[INFO] [OTA-Publish] Đã publish thành công bản tin lên topic [room1/ota]
```

### 2.4. Huấn luyện & Chạy AI Isolation Forest (`ai_ml.py`)
- **Huấn luyện mô hình:**
  ```bash
  python App/IOT/ai/train/train_iforest.py
  ```
  Mô hình xuất ra tại `App/IOT/ai/models/iforest.joblib`.
- **Chạy kiểm tra bất thường trong module:**
  Hàm `check_anomaly_ml(sample)` được tích hợp sẵn sàng để nhận dữ liệu từ `room1/telemetry` và đẩy sự kiện lên `room1/event`.

### 2.5. Chạy công cụ đo đạc hiệu năng (`measure.py` - Chương 4)
- Đo độ trễ dữ liệu lên Cloud:
  ```bash
  python App/IOT/tools/measure.py latency_up --n 5
  ```
- Đo độ trễ lệnh hai chiều:
  ```bash
  python App/IOT/tools/measure.py latency_cmd --n 5
  ```
- Đo thời gian tự phục hồi khi mất mạng:
  ```bash
  python App/IOT/tools/measure.py recovery --n 3
  ```
Dữ liệu kết quả được tự động lưu ra các file CSV trong thư mục `Report/data/`.

---

## 3. Chạy toàn bộ Unit Tests của Gateway
Kiểm tra toàn bộ test suite để đảm bảo không lỗi:
```bash
pytest App/IOT/gateway/ App/IOT/tools/ -v
```

---

## 4. Các lỗi thường gặp và cách xử lý

1. **Lỗi `rc=5` (Connection Refused: not authorised) khi kết nối CoreIOT:**
   * *Nguyên nhân:* Token `COREIOT_TOKEN` trong `.env` chưa điền hoặc bị sai.
   * *Khắc phục:* Mở `app.coreiot.io`, vào mục Gateways -> `room1-gateway` -> Copy Access Token và dán vào file `App/IOT/gateway/.env`.

2. **Node B thật báo lỗi `download_failed` khi tải OTA:**
   * *Nguyên nhân:* `GATEWAY_IP` trong `.env` đang để `127.0.0.1` (loopback). Node B thật qua WiFi không thể truy cập IP nội bộ này của máy tính.
   * *Khắc phục:* Mở `ipconfig` trên Windows, tìm địa chỉ IPv4 của Wi-Fi/Hotspot (vd: `192.168.137.1`), cập nhật `GATEWAY_IP=192.168.137.1` trong `.env`.

3. **Bridge bị ngắt kết nối và tự kết nối lại liên tục mỗi 1-2 giây:**
   * *Nguyên nhân:* Đang có 2 cửa sổ terminal chạy song song `bridge.py` hoặc có người khác dùng chung token kết nối vào cùng Client ID CoreIOT.
   * *Khắc phục:* Tắt các tiến trình Python bridge đang chạy ngầm, chỉ giữ duy nhất một instance `bridge.py`.
