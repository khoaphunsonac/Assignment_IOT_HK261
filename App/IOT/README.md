# IOT

Nền tảng theo yêu cầu môn học: CoreIOT (app.coreiot.io) và ESP32-S3 (YoloUNO).
Quy ước dữ liệu giữa các phần: [docs/CONTRACT.md](../../docs/CONTRACT.md).

| Thư mục | Nội dung | Người giữ |
|---|---|---|
| `firmware/common/` | `espnow_packet.h` dùng chung 2 node | TV1 |
| `firmware/node_a/` | Node A: DHT20, PIR, ánh sáng, TinyML, gửi ESP-NOW | TV1 |
| `firmware/espnow_rx_test/` | Node B giả chỉ nhận ESP-NOW, để test Node A (không phải firmware Node B thật) | TV1 |
| `firmware/node_b/` | Node B: quạt, LED RGB, LCD, MQTT, webserver, OTA | TV1 (webserver: TV4) |
| `gateway/` | Python: bridge CoreIOT, OTA server, AI, giao diện local, `fake_node_b.py` | TV2, TV3 |
| `ai/` | Dữ liệu, script train, model TinyML đã xuất | TV3 |
| `coreiot/` | Export JSON dashboard, RuleChain | TV4 |
| `docs/` | Sơ đồ đấu nối, hình ảnh phần cứng | TV1 |

## Phần cứng

Board: YoloUNO (ESP32-S3), 2 board. Chân đã kiểm trên board ngày 10/11/2026:

| Node | Module | Chân |
|---|---|---|
| A | Cảm biến ánh sáng | GPIO 1 (ADC1) |
| A | PIR | GPIO 6 |
| A | DHT20 (I2C 0x38) | SDA 11, SCL 12 |
| B | Quạt mini (PWM) | GPIO 8 |
| B | LED RGB (NeoPixel) | GPIO 10 |
| B | LCD 16x2 (I2C 0x21) | SDA 11, SCL 12 |
| Cả 2 | LED trên board | GPIO 48 |

Cảm biến analog chỉ dùng ADC1 (GPIO 1–10), vì ADC2 không đọc được khi WiFi bật.

## Firmware: build và nạp

- PlatformIO, `platform = espressif32@6.9.0` (Arduino core 2.0.17) cho **cả 2 node**. Không dùng bản mới hơn: thư viện TensorFlowLite_ESP32 không build được với core 3.x.
- Trong `platformio.ini` để `monitor_dtr = 0` và `monitor_rts = 0`. Xem log bằng nút Monitor của PlatformIO, **không** dùng extension Serial Monitor (Stop của extension làm board đứng).
- Mật khẩu WiFi: chép `secrets.example.h` thành `secrets.h` rồi điền. `secrets.h` không được commit.
- Mạng: dùng hotspot điện thoại, cả lúc demo.

## Gateway: chạy

[TV2 điền khi có code: cài Mosquitto, `pip install -r requirements.txt`, chạy lệnh gì.]

## CoreIOT

Chi tiết đầy đủ: [`coreiot/SETUP_COREIOT.md`](coreiot/SETUP_COREIOT.md). Nền tảng là app.coreiot.io (ThingsBoard PE 4.2.0).

**Thiết bị** (tên theo CONTRACT mục 11):

| Tên | Ai tạo | Dữ liệu |
|---|---|---|
| `room1-gateway` | tạo ở Entities → Gateways (Tuấn, #28) | Sự kiện AI (`event_type`, `event_level`, `event_msg`) |
| `room1-node-a` | Gateway tự tạo | Số đo cảm biến, `anomaly_score` |
| `room1-node-b` | Gateway tự tạo | `fan_level`, `light_on`, `mode`, nhận RPC, giữ shared attribute ngưỡng, nhận OTA |
| `room1-test` | tạo tay, tạm thời | Dữ liệu giả từ `fake_telemetry.py`, khi chưa có Gateway |

**Chạy dữ liệu giả** (chưa có Gateway):
```powershell
cd App\IOT\coreiot
pip install paho-mqtt==1.6.1
copy .env.example .env      # dán Access Token của room1-test vào COREIOT_TEST_TOKEN
python fake_telemetry.py
```

**Import dashboard:** Dashboards → **+** → *Import dashboard* → chọn `coreiot/dashboard_room1.json` → gán alias `sensor_node` và `actuator_node` vào device (`room1-test`, hoặc `room1-node-a` / `room1-node-b` khi đã có Gateway).

**RuleChain:** chưa có (T12 `autoFan`/`autoLight`, T13 cảnh báo). Khi làm xong sẽ export vào `coreiot/` và import bằng Rule chains → **+** → *Import rule chain*.
