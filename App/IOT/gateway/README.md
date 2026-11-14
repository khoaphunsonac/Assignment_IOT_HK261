# Module Gateway & Giả lập Node B (`fake_node_b.py`, `bridge.py`)

Người phụ trách: **Tuấn (TV2 – Gateway)**  
Issue: **#27** (Tạo script giả lập Node B phục vụ test W1) & **#28** (Thử T15: Gateway CoreIOT với 2 thiết bị con)

---

## 1. Cài đặt môi trường

### 1.1. Cài đặt Mosquitto (MQTT Broker)

1. Tải bản Windows 64-bit tại: [https://mosquitto.org/download/](https://mosquitto.org/download/)
2. Cài đặt mặc định (đường dẫn: `C:\Program Files\mosquitto\`).
3. File cấu hình đã có sẵn tại `App/IOT/gateway/mosquitto.conf`:
   ```conf
   listener 1883 0.0.0.0
   allow_anonymous true
   ```
4. Khởi động broker trong PowerShell:
   ```powershell
   & "C:\Program Files\mosquitto\mosquitto.exe" -c App\IOT\gateway\mosquitto.conf -v
   ```

### 1.2. Môi trường Python

Yêu cầu Python 3.10 hoặc 3.11.

```powershell
# Tạo và kích hoạt venv tại thư mục gốc của repo
python -m venv .venv
.venv\Scripts\activate

# Cài đặt thư viện paho-mqtt
pip install paho-mqtt==1.6.1
```

*(Code đã tương thích cả `paho-mqtt` 1.6.1 lẫn 2.x).*

---

## 2. Giả lập Node B (`fake_node_b.py` - Issue #27)

### 2.1. Chạy unit test tự động (không cần broker)

```powershell
python App/IOT/gateway/test_fake_node_b.py
```
Kết quả mong đợi: `Ran 7 tests ... OK`.

### 2.2. Chạy `fake_node_b.py`

```powershell
python App/IOT/gateway/fake_node_b.py
```

Script sẽ kết nối tới `localhost:1883` và:
- Gửi retained status `online` lên topic `room1/status`.
- Đăng ký nhận lệnh từ topic `room1/cmd` và cấu hình từ `room1/config`.
- Publish dữ liệu cảm biến định kỳ mỗi 2 giây lên topic `room1/telemetry`.

### 2.3. Tự kiểm 5 ca test

| Thao tác | Lệnh PowerShell | Kết quả mong đợi trên `mosquitto_sub` |
|---|---|---|
| **1. Bật quạt số 2** | `& "C:\Program Files\mosquitto\mosquitto_pub.exe" -h localhost -t room1/cmd -m '{\"id\":\"test-1\",\"method\":\"setFan\",\"params\":2}'` | `room1/cmd/resp` trả `ok: true`, `mode: "manual"`, telemetry ngay sau đó có `fan_level: 2` |
| **2. autoFan khi đang manual** | `& "C:\Program Files\mosquitto\mosquitto_pub.exe" -h localhost -t room1/cmd -m '{\"id\":\"test-2\",\"method\":\"autoFan\",\"params\":1}'` | `room1/cmd/resp` trả `ok: false`, `error: "ignored_not_auto"` |
| **3. setFan quá khoảng (mức 5)** | `& "C:\Program Files\mosquitto\mosquitto_pub.exe" -h localhost -t room1/cmd -m '{\"id\":\"test-3\",\"method\":\"setFan\",\"params\":5}'` | `room1/cmd/resp` trả `ok: false`, `error: "bad_params"` |
| **4. Lệnh không phải JSON** | `& "C:\Program Files\mosquitto\mosquitto_pub.exe" -h localhost -t room1/cmd -m "abc"` | `room1/cmd/resp` trả `ok: false`, `error: "bad_json"`, `id: null` |
| **5. Tắt script (Ctrl+C)** | Nhấn `Ctrl + C` tại terminal đang chạy `fake_node_b.py` | `room1/status` chuyển sang `{"state":"offline"}` (Last Will) |

---

## 3. Cầu nối CoreIOT Gateway (`bridge.py` - Issue #28, Tiêu chí T15)

### 3.1. Cấu hình Access Token CoreIOT

1. Truy cập [app.coreiot.io](https://app.coreiot.io) $\rightarrow$ Entities $\rightarrow$ Gateways $\rightarrow$ Tạo Gateway tên `room1-gateway`.
2. Bấm vào gateway vừa tạo $\rightarrow$ Copy **Access Token**.
3. Tạo file `App/IOT/gateway/.env` từ `.env.example`:
   ```powershell
   copy App\IOT\gateway\.env.example App\IOT\gateway\.env
   ```
4. Điền Token vào file `.env`:
   ```env
   COREIOT_TOKEN=dien_access_token_o_day
   MQTT_HOST=localhost
   MQTT_PORT=1883
   COREIOT_HOST=app.coreiot.io
   COREIOT_PORT=1883
   ```

### 3.2. Chạy unit test tự động cho Bridge

```powershell
python App/IOT/gateway/test_bridge.py
```
Kết quả mong đợi: `Ran 4 tests ... OK` (kiểm tra tách telemetry 2 thiết bị, ánh xạ RPC 2 chiều, timeout và kiểm tra ràng buộc cấu hình).

### 3.3. Chạy `bridge.py` trong thực tế

Mở terminal chạy:
```powershell
python App/IOT/gateway/bridge.py
```

Gateway Bridge sẽ:
1. Mở 2 kết nối MQTT: Local (`localhost:1883`) và Cloud (`app.coreiot.io:1883`).
2. Gửi `v1/gateway/connect` khai báo 2 thiết bị con `room1-node-a` và `room1-node-b`.
3. Nhận `room1/telemetry` từ Node B $\rightarrow$ tách thành số đo cho 2 thiết bị $\rightarrow$ publish lên `v1/gateway/telemetry`.
4. Nhận RPC từ CoreIOT `v1/gateway/rpc` $\rightarrow$ chuyển thành `room1/cmd` (`id` = `tb-<id>`) $\rightarrow$ nhận kết quả từ `room1/cmd/resp` $\rightarrow$ phản hồi ngược lại CoreIOT.
5. Đồng bộ cấu hình ngưỡng (Shared Attributes) từ CoreIOT xuống `room1/config`.
