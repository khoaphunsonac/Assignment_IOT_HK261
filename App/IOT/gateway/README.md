# Module Gateway & Giả lập Node B (`fake_node_b.py`)

Người phụ trách: **Tuấn (TV2 – Gateway)**
Issue: **#27** (Tạo script giả lập Node B phục vụ test W1) & **#28** (CoreIOT Gateway Bridge)

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

*(Code `fake_node_b.py` đã tương thích cả `paho-mqtt` 1.6.1 lẫn 2.x).*

---

## 2. Hướng dẫn chạy và kiểm thử

### 2.1. Chạy unit test tự động (không cần broker)

```powershell
python App/IOT/gateway/test_fake_node_b.py
```

Kết quả mong đợi: `Ran 7 tests ... OK`.

### 2.2. Chạy `fake_node_b.py`

Mở một terminal mới (đã kích hoạt `.venv`), chạy:

```powershell
python App/IOT/gateway/fake_node_b.py
```

Script sẽ kết nối tới `localhost:1883` và:

- Gửi retained status `online` lên topic `room1/status`.
- Đăng ký nhận lệnh từ topic `room1/cmd` và cấu hình từ `room1/config`.
- Publish dữ liệu cảm biến định kỳ mỗi 2 giây lên topic `room1/telemetry`.

---

## 3. Tự kiểm (Self-check với Mosquitto)

Mở 2 cửa sổ terminal:

### Cửa sổ 1: Theo dõi các topic của phòng 1

```powershell
& "C:\Program Files\mosquitto\mosquitto_sub.exe" -h localhost -t "room1/#" -v
```

### Cửa sổ 2: Bắn các lệnh kiểm tra

| Thao tác                                 | Lệnh PowerShell                                                                                                                            | Kết quả mong đợi trên Cửa sổ 1                                                                  |
| ----------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------ |
| **1. Bật quạt số 2**             | `& "C:\Program Files\mosquitto\mosquitto_pub.exe" -h localhost -t room1/cmd -m '{\"id\":\"test-1\",\"method\":\"setFan\",\"params\":2}'`  | `room1/cmd/resp` trả `ok: true`, `mode: "manual"`, telemetry ngay sau đó có `fan_level: 2` |
| **2. autoFan khi đang manual**     | `& "C:\Program Files\mosquitto\mosquitto_pub.exe" -h localhost -t room1/cmd -m '{\"id\":\"test-2\",\"method\":\"autoFan\",\"params\":1}'` | `room1/cmd/resp` trả `ok: false`, `error: "ignored_not_auto"`                                   |
| **3. setFan quá khoảng (mức 5)** | `& "C:\Program Files\mosquitto\mosquitto_pub.exe" -h localhost -t room1/cmd -m '{\"id\":\"test-3\",\"method\":\"setFan\",\"params\":5}'`  | `room1/cmd/resp` trả `ok: false`, `error: "bad_params"`                                         |
| **4. Lệnh không phải JSON**      | `& "C:\Program Files\mosquitto\mosquitto_pub.exe" -h localhost -t room1/cmd -m "abc"`                                                     | `room1/cmd/resp` trả `ok: false`, `error: "bad_json"`, `id: null`                             |
| **5. Tắt script (Ctrl+C)**         | Nhấn `Ctrl + C` tại terminal đang chạy `fake_node_b.py`                                                                             | `room1/status` chuyển sang `{"state":"offline"}` (Last Will)                                      |
