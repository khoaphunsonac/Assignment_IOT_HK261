# TASKS tuần W1 (11/11 – CN 15/11/2026)

> Hạn chung: **CN 15/11, họp 15 phút buổi tối** để xem issue nào còn mở.
> Đọc [`docs/CONTRACT.md`](CONTRACT.md) **trước khi code**. Mọi topic, key, lệnh phải khớp file đó.
> Tuần này **chưa cần board**: mọi người làm với dữ liệu giả từ `fake_node_b.py`. Khi Node B thật chạy, tắt script giả là xong, code của mọi người giữ nguyên.

---

## 1. Quy trình Git

### 1.1. Nhánh
- **Docs** (`docs/`, `Report/`, README): push thẳng `main`.
- **Code: KHÔNG push thẳng `main`.** Mỗi issue một nhánh, tên `<số issue>-<tên ngắn>`:

| Người | Issue | Tên nhánh |
|---|---|---|
| Minh | #26 | `26-node-a-sensor-espnow` |
| Tuấn | #27 | `27-fake-node-b` |
| Tuấn | #28 | `28-gateway-t15` |
| Phú | #30 | `30-tinyml-retrain` |
| Phú | #31 | `31-streamlit-ui` |
| Khoa | #32 | `32-coreiot-dashboard` |
| Khoa | #33 | `33-webserver-standalone` |

Tạo nhánh từ `main` mới nhất:
```bash
git checkout main
git pull origin main
git checkout -b 27-fake-node-b
```

### 1.2. Commit
Theo Conventional Commits, `<loại>(<phần>): <việc đã làm>`:
```
feat(gateway): publish telemetry gia moi 2 giay
feat(ai): them chuan hoa input khi train
fix(node-b): sua loi mat ket noi MQTT khi doi WiFi
docs(contract): them key espnow_loss
```
Loại: `feat` (tính năng), `fix` (sửa lỗi), `docs`, `test`, `chore` (cấu hình, dọn dẹp).
Phần: `node-a`, `node-b`, `gateway`, `ai`, `ui`, `coreiot`, `contract`, `report`.

Commit bằng **tài khoản GitHub của chính mình**. Thông báo BTL yêu cầu lịch sử commit thể hiện ai làm gì.

### 1.3. Pull Request
```bash
git push -u origin 27-fake-node-b
```
Lên GitHub tạo PR vào `main`. Mô tả PR phải có:
1. Dòng `Closes #27` (merge xong issue tự đóng).
2. **Minh chứng:** ảnh màn hình, log hoặc video ngắn, đúng mục "Xong khi" của issue.
3. Cách chạy lại (lệnh gì).

Minh review rồi merge. Đừng tự merge PR của mình.

### 1.4. Không được
- Commit `secrets.h`, `.env`, token CoreIOT, mật khẩu WiFi. Repo public, lộ là phải đổi token ngay.
- Đổi tên key/topic mà không sửa `CONTRACT.md` trước và báo nhóm.
- Commit thư mục `.pio/`, `.venv/`, `__pycache__/` (đã có trong `.gitignore`).

---

## 2. Phân chia W1

| # | Việc | Người | Giờ | Hạn | Chờ |
|---|---|---|---|---|---|
| 24 | Khung repo + 2 project PlatformIO + file bí mật mẫu | Minh | 1.5h | 11/11 | — |
| 25 | `docs/CONTRACT.md` | Minh | — | **xong** | — |
| 26 | Node A: đọc cảm biến + gửi ESP-NOW | Minh | 6h | 15/11 | — |
| 27 | `fake_node_b.py` | Tuấn | 3h | **12/11** | — |
| 28 | Thử T15: Gateway CoreIOT với 2 thiết bị con | Tuấn | 5–6h | 15/11 | #27 |
| 29 | Kiểm tra OTA của CoreIOT cho thiết bị sau Gateway | Tuấn | 1–2h | 15/11 | #28 |
| 30 | Train lại model TinyML | Phú | 4–5h | 15/11 | — |
| 31 | Khung giao diện Streamlit | Phú | 4–5h | 15/11 | #27 |
| 32 | Dashboard CoreIOT với dữ liệu giả | Khoa | 3–4h | 15/11 | — |
| 33 | Webserver Node B bản đứng riêng | Khoa | 4–5h | 15/11 | — |

**#27 chặn Phú và Tuấn (#28)**, nên Tuấn làm #27 trước tiên, xong trước tối 12/11. Trong lúc chờ, Phú làm #30, Khoa làm #32 và #33.

---

## 3. Cài đặt chung (Tuấn, Phú, Khoa)

### 3.1. Mosquitto (broker MQTT trên laptop)
1. Tải bản Windows 64-bit tại https://mosquitto.org/download/ và cài mặc định.
2. Tạo file `App/IOT/gateway/mosquitto.conf`:
   ```
   listener 1883 0.0.0.0
   allow_anonymous true
   ```
3. Chạy (giữ cửa sổ này mở):
   ```powershell
   & "C:\Program Files\mosquitto\mosquitto.exe" -c App\IOT\gateway\mosquitto.conf -v
   ```
4. Lần đầu Windows hỏi Firewall: cho phép cả **Private** và **Public** (để board trong hotspot kết nối được).

Báo lỗi `Only one usage of each socket address`: cổng 1883 đang bị chiếm. Chạy `netstat -ano | findstr :1883` để xem PID, rồi tắt app đó.

### 3.2. Python
- Python 3.10 hoặc 3.11 (TensorFlow chưa ổn với 3.12+ trên Windows).
- Tạo môi trường ảo trong thư mục repo:
  ```powershell
  python -m venv .venv
  .venv\Scripts\activate
  pip install paho-mqtt==1.6.1
  ```
- **Ghim `paho-mqtt==1.6.1`** như slide thầy. Bản 2.x đổi cách tạo client, code mẫu sẽ lỗi.

### 3.3. Lệnh kiểm tra nhanh
Xem mọi tin trên broker:
```powershell
& "C:\Program Files\mosquitto\mosquitto_sub.exe" -h localhost -t "room1/#" -v
```
Gửi thử một lệnh:
```powershell
& "C:\Program Files\mosquitto\mosquitto_pub.exe" -h localhost -t room1/cmd -m '{\"id\":\"test-1\",\"method\":\"setFan\",\"params\":2}'
```

---

## 4. Minh (TV1 – Lead + Firmware)

### #24 Khung repo (còn lại sau bản dọn repo)
- [x] Cây thư mục, README, `.gitignore` (đã push).
- [ ] `App/IOT/firmware/node_a/` và `node_b/`: project PlatformIO, `platformio.ini` có:
  ```ini
  [platformio]
  default_envs = yolo_uno

  [env:yolo_uno]
  platform = espressif32@6.9.0
  board = yolo_uno
  framework = arduino
  monitor_speed = 115200
  monitor_dtr = 0
  monitor_rts = 0
  build_flags = -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1
  ```
  kèm `boards/yolo_uno.json` và `src/main.cpp` rỗng.
- [ ] `secrets.example.h` (`WIFI_SSID`, `WIFI_PASS`, `MQTT_HOST`), `App/IOT/gateway/.env.example` (`COREIOT_TOKEN=`, `MQTT_HOST=localhost`).

**Xong khi:** 2 project build SUCCESS, `git status` không thấy file bí mật.

### #26 Node A: đọc cảm biến + gửi ESP-NOW
Nhánh `26-node-a-sensor-espnow`. File:
| File | Nội dung |
|---|---|
| `firmware/common/espnow_packet.h` | Chép đúng mục 3 CONTRACT |
| `firmware/node_a/src/sensors.{h,cpp}` | DHT20, PIR có thời gian chờ, ánh sáng ADC |
| `firmware/node_a/src/espnow_link.{h,cpp}` | Quét channel, ghép cặp, gửi, gửi lại, ghép lại |
| `firmware/node_a/src/main.cpp` | Tạo task, nối queue |

Thứ tự:
1. `task_sensor` 2 giây/lần, in Serial. Kiểm: hà hơi vào DHT20 thì độ ẩm tăng, che cảm biến thì `light` đổi.
2. `espnow_packet.h`.
3. `task_espnow_tx`: ghép cặp, unicast (hoặc broadcast, tùy kết quả test 11/11), gửi lại cùng `seq` tối đa 3 lần.
4. Chưa có số đo hợp lệ thì không gửi.

**Xong khi:** PR có log Node B nhận `seq` liên tục, và log tự ghép lại sau khi reset Node B.

---

## 5. Tuấn (TV2 – Gateway)

### #27 `fake_node_b.py` — hạn **12/11**
Nhánh `27-fake-node-b`. File `App/IOT/gateway/fake_node_b.py`.

**Mục đích:** giả Node B **đúng 100%** CONTRACT mục 4–9, để Phú, Khoa và chính Tuấn làm việc khi chưa có board.

**Khung gợi ý:**
```python
import json, random, time
import paho.mqtt.client as mqtt

BROKER = "localhost"
state = {"fan_level": 0, "light_on": False, "mode": "auto"}
seq = 0

def make_telemetry() -> dict:
    # TODO: đủ key ở CONTRACT mục 5, số ngẫu nhiên hợp lý
    # (temperature 26–35, humidity 50–80, light 500–3500, presence random...)
    ...

def handle_cmd(cmd: dict) -> dict:
    # TODO: xử lý 6 method ở CONTRACT mục 6.2, đúng luật chế độ ở mục 13
    # Trả dict đúng format room1/cmd/resp (mục 6.3), kể cả các mã lỗi
    ...

def on_connect(client, userdata, flags, rc):
    # TODO: subscribe room1/cmd, room1/config; publish retained room1/status online
    ...

def on_message(client, userdata, msg):
    # TODO: room1/cmd -> handle_cmd -> publish room1/cmd/resp
    #       room1/config -> in ra bộ ngưỡng
    ...

client = mqtt.Client(client_id="room1-fake-node-b")
client.will_set("room1/status", json.dumps({"state": "offline"}), qos=1, retain=True)
client.on_connect = on_connect
client.on_message = on_message
client.connect(BROKER, 1883, 60)
client.loop_start()
while True:
    # TODO: publish room1/telemetry mỗi 2 giây
    time.sleep(2)
```

**Tự kiểm (gõ lệnh ở mục 3.3):**
| Gửi | Phải thấy |
|---|---|
| `setFan` 2 | resp `ok: true`, `mode: "manual"`, telemetry sau đó `fan_level: 2` |
| `autoFan` 1 khi đang manual | resp `ok: false`, `error: "ignored_not_auto"` |
| `setFan` 5 | `error: "bad_params"` |
| `abc` (không phải JSON) | `error: "bad_json"`, `id: null` |
| Ctrl+C tắt script | `room1/status` chuyển `{"state":"offline"}` (Last Will) |

**Xong khi:** PR có ảnh `mosquitto_sub` thấy đủ 5 dòng trên, README ngắn trong `App/IOT/gateway/` (cài gì, chạy lệnh gì). Nhắn Phú và Khoa chạy thử.

### #28 Thử T15: Gateway CoreIOT với 2 thiết bị con
Nhánh `28-gateway-t15`. File `App/IOT/gateway/bridge.py`. Tài liệu: slide thầy `01_Slides/IoT_Gateway_RuleChain_VN.pptx`, CONTRACT mục 11.

Bước:
1. app.coreiot.io → Entities → Gateways → tạo `room1-gateway`. Lấy Access Token, ghi vào `App/IOT/gateway/.env` (`COREIOT_TOKEN=...`).
2. `bridge.py` mở **2 client MQTT**: một vào broker local (`localhost`), một vào `app.coreiot.io:1883` (token làm username).
3. Nhận `room1/telemetry` → tách 2 thiết bị theo bảng 11.2 → publish `v1/gateway/telemetry`.
4. Khởi động gửi `v1/gateway/connect` cho 2 thiết bị (11.3).
5. Subscribe `v1/gateway/rpc` → đổi thành `room1/cmd` với `id` = `tb-<id>` → chờ `room1/cmd/resp` → trả CoreIOT (11.4). Quá 5 giây trả `timeout`.

**Kiểm:**
- CoreIOT → Devices: thấy `room1-node-a`, `room1-node-b` tự tạo, tab Latest telemetry có số mới mỗi 2 giây.
- Tạo dashboard tạm, thêm widget "RPC debug terminal" cho `room1-node-b`, gõ `setFan 2` → fake_node_b đổi `fan_level` → terminal hiện resp.

**Xong khi:** PR có 2 ảnh trên. **Không làm được thì báo nhóm ngay trong tuần** (T15 là 2 điểm, phải biết sớm để đổi hướng).

### #29 Kiểm tra OTA của CoreIOT cho thiết bị sau Gateway
Không cần nhánh, kết quả ghi bằng comment trong issue. Trả lời 3 câu:
1. CoreIOT có mục OTA updates (upload file `.bin`, gán cho device/device profile) không? Chụp màn hình.
2. Gán firmware cho thiết bị con `room1-node-b` được không? Gateway nhận thông báo qua topic nào (thường là shared attribute `fw_title`, `fw_version`, `fw_checksum`... qua `v1/gateway/attributes`)?
3. Gateway tải file `.bin` về bằng cách nào?

Nếu câu 2 không được: đề xuất phương án dự phòng ở mục 10 tài liệu dự án (Gateway tự giữ file, CoreIOT chỉ gửi lệnh qua RPC).

---

## 6. Phú (TV3 – AI + giao diện)

### #30 Train lại model TinyML
Nhánh `30-tinyml-retrain`.

**Vì sao:** model `.h` của thầy đoán **ngược nhãn**. Đã kiểm 10/11 trên PC (ai-edge-litert), khớp với kết quả trên board:
| Input (nhiệt, ẩm) | Nhãn đúng | Model thầy trả |
|---|---|---|
| (80, 20) | 1 | 0.002 |
| (25, 60) | 0 | 0.524 |

Runtime TinyML trên ESP32 chạy đúng, chỉ có model sai.

**File:**
| File | Nội dung |
|---|---|
| `App/IOT/ai/data/sensor_data.csv` | Chép từ `08_Code/TinyML/sensor_data.csv` |
| `App/IOT/ai/train/train_tinyml.py` | Sửa từ `08_Code/TinyML/TFL_For_MCU.py` |
| `App/IOT/ai/train/check_model.py` | Chạy file `.tflite` trên PC, in dự đoán |
| `App/IOT/ai/models/` | `tinyml_model.tflite`, `tinyml_model.h`, `norm.json` |

Cài: `pip install tensorflow pandas scikit-learn` (trong `.venv`).

Bước:
1. **Chuẩn hóa input:** tính `mean`, `std` của nhiệt độ và độ ẩm trên tập train, đưa `(x - mean) / std` vào model. Lưu `mean`, `std` vào `norm.json` **và** ghi thành hằng số trong `tinyml_model.h` (vd `const float TEMP_MEAN = ...;`), để firmware chuẩn hóa giống hệt.
2. `train_test_split(..., stratify=y, random_state=42)` để kết quả lặp lại được.
3. Train, in accuracy trên tập test, in **confusion matrix** (`sklearn.metrics.confusion_matrix`).
4. Xuất `.tflite` và `.h` như script của thầy. Tên mảng giữ là `model` để firmware không phải sửa.
5. `check_model.py`: load `.tflite` bằng `tf.lite.Interpreter`, chạy lại 2 dòng trong bảng trên và toàn bộ CSV, in nhãn thật cạnh dự đoán.

Lưu ý: CSV chỉ có 35 dòng, nên accuracy chưa có ý nghĩa thống kê. Mục tiêu tuần này là **pipeline đúng chiều nhãn**. Dữ liệu thật thu ở W2–W3 qua công tắc trên giao diện.

**Xong khi:** PR có output của `check_model.py`: (80, 20) ra gần 1, (25, 60) ra gần 0, cùng confusion matrix và nội dung `norm.json`.

### #31 Khung giao diện Streamlit
Nhánh `31-streamlit-ui`. File `App/IOT/gateway/ui/app.py`. Cài `pip install "streamlit>=1.37"` (cần bản có `st.fragment`). Chạy `streamlit run App/IOT/gateway/ui/app.py`. Chờ #27 xong để có dữ liệu.

**Khung gợi ý:**
```python
import json
import streamlit as st
import paho.mqtt.client as mqtt

@st.cache_resource          # chỉ tạo 1 client MQTT cho cả app
def get_mqtt():
    data = {"telemetry": {}, "status": "unknown", "last_resp": None}
    # TODO: tạo client id "room1-ui", subscribe room1/telemetry, room1/status, room1/cmd/resp
    #       on_message cập nhật vào data; connect localhost; loop_start()
    return client, data

client, data = get_mqtt()
st.session_state.setdefault("next_id", 1)   # id lệnh tăng dần, không reset khi app vẽ lại

@st.fragment(run_every=1)   # vẽ lại phần này mỗi 1 giây
def live():
    # TODO: st.metric cho temperature, humidity, light; presence; fan_level, light_on, mode
    #       trạng thái online/offline của Node B
    ...

live()
# TODO: 3 nút quạt 0/1/2, 1 công tắc đèn -> publish room1/cmd
#       {"id": f"ui-{st.session_state.next_id}", ...}, rồi tăng next_id; hiện last_resp
```

**Xong khi:** PR có video ngắn (hoặc 2 ảnh): bấm quạt 2 → resp `ok` → số `fan_level` trên giao diện đổi thành 2 → tắt `fake_node_b.py` thì trạng thái chuyển offline.

---

## 7. Khoa (TV4 – CoreIOT + Webserver)

### #32 Dashboard CoreIOT với dữ liệu giả
Nhánh `32-coreiot-dashboard`. Làm ngay, không chờ Gateway.

1. app.coreiot.io → tạo device tạm `room1-test`, lấy Access Token (để trong `.env`, **không** commit).
2. Viết `App/IOT/coreiot/fake_telemetry.py`: publish `v1/devices/me/telemetry` mỗi 2 giây, giống LAB1, **đúng key ở CONTRACT mục 5**.
3. Dashboard `Room 1` theo mục 11 tài liệu dự án:
   - Thẻ số: `temperature`, `humidity`, `light`, `presence`.
   - Biểu đồ đường: `temperature`, `humidity`.
   - Trạng thái: `fan_level`, `light_on`, `mode`, `anomaly_score`.
   - Điều khiển: nút/slider gọi RPC `setFan`, công tắc gọi `setLight`, `setMode` (tên method đúng CONTRACT 6.2). Tuần này bấm sẽ timeout vì chưa có ai nghe, như vậy là bình thường.
4. Dùng **alias** cho nguồn dữ liệu (Entity alias), để khi Gateway của Tuấn xong (#28) chỉ cần đổi alias sang `room1-node-a` / `room1-node-b`, không phải làm lại widget.
5. Export dashboard ra JSON (Dashboard → Export) vào `App/IOT/coreiot/dashboard_room1.json`.

**Xong khi:** PR có ảnh dashboard có số chạy, file JSON và `fake_telemetry.py`.

### #33 Webserver Node B bản đứng riêng
Nhánh `33-webserver-standalone`. Nguồn mẫu: `07_Labs/LAB4_Webserver/mainserver.cpp` (dùng `WebServer.h`, `server.on(...)`, `server.handleClient()`).

**File:**
| File | Nội dung |
|---|---|
| `App/IOT/firmware/webserver_test/platformio.ini` | Giống mẫu ở #24 (6.9.0, `monitor_dtr = 0`) |
| `.../webserver_test/include/control.h` | **Chép đúng** CONTRACT mục 12, không sửa |
| `.../webserver_test/src/control_stub.cpp` | Bản giả: lưu trạng thái trong biến, `control_get_sensors()` trả số ngẫu nhiên |
| `.../webserver_test/src/web.cpp` | Task webserver |
| `.../webserver_test/src/main.cpp` | Nối WiFi (mật khẩu trong `secrets.h`), tạo task |

**Endpoint:**
| URL | Làm gì |
|---|---|
| `GET /` | Trang HTML: số đo, trạng thái quạt/đèn/chế độ, nút quạt 0/1/2, nút đèn |
| `GET /api/state` | JSON: số đo + `fan_level`, `light_on`, `mode` (key đúng CONTRACT) |
| `GET /api/fan?level=2` | Gọi `control_set_fan(2, CmdSource::Web)` |
| `GET /api/light?on=1` | Gọi `control_set_light(true, CmdSource::Web)` |

Yêu cầu:
- Trang tự cập nhật bằng `fetch('/api/state')` mỗi 2 giây (như LAB4 `/sensors`), không reload cả trang.
- Webserver chạy trong **một FreeRTOS task riêng** (`xTaskCreate`, trong vòng lặp gọi `server.handleClient()` rồi `vTaskDelay(2)`), không viết trong `loop()`. Như vậy W2 Minh ghép thẳng vào Node B.
- Chỉ gọi hàm trong `control.h`, **không** đọc/ghi biến toàn cục của module khác.

**Xong khi:** build SUCCESS. Có lên lab thì nạp, mở trang bằng điện thoại cùng hotspot, chụp ảnh bấm quạt và trạng thái đổi.

---

## 8. Hỏi ở đâu
- Không hiểu CONTRACT hoặc thấy CONTRACT thiếu: hỏi trong nhóm, **đừng tự đặt key mới**.
- Kẹt quá 1 tiếng ở một chỗ: nhắn nhóm kèm ảnh lỗi, đừng ngồi một mình.
