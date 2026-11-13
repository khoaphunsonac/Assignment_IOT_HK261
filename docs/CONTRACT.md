# CONTRACT – Dữ liệu và giao thức dùng chung (BTL IoT nhóm 4)

> **Phiên bản 1.0 – 10/11/2026.** Nguồn gốc: mục 6, 7, 8, 9, 11 tài liệu dự án.
> Đây là "hợp đồng" giữa 4 người. Code của ai cũng phải khớp file này.
> **Muốn đổi bất cứ thứ gì (tên key, topic, kiểu dữ liệu):** sửa file này trước bằng PR, ghi vào mục 14 (Lịch sử thay đổi), báo cả nhóm, rồi mới sửa code.

## Mục lục
1. [Sơ đồ luồng dữ liệu](#1-sơ-đồ-luồng-dữ-liệu)
2. [Quy ước chung](#2-quy-ước-chung)
3. [ESP-NOW: Node A ↔ Node B](#3-esp-now-node-a--node-b)
4. [MQTT local: danh sách topic](#4-mqtt-local-danh-sách-topic)
5. [Telemetry `room1/telemetry`](#5-telemetry-room1telemetry)
6. [Lệnh `room1/cmd` và trả lời `room1/cmd/resp`](#6-lệnh-room1cmd-và-trả-lời-room1cmdresp)
7. [Ngưỡng `room1/config`](#7-ngưỡng-room1config)
8. [Trạng thái online/offline](#8-trạng-thái-onlineoffline)
9. [OTA `room1/ota` và `room1/ota/status`](#9-ota-room1ota-và-room1otastatus)
10. [Sự kiện AI `room1/event`](#10-sự-kiện-ai-room1event)
11. [CoreIOT: thiết bị, key, RPC, attribute](#11-coreiot-thiết-bị-key-rpc-attribute)
12. [API nội bộ Node B cho webserver](#12-api-nội-bộ-node-b-cho-webserver)
13. [Chế độ auto / manual / offline](#13-chế-độ-auto--manual--offline)
14. [Lịch sử thay đổi](#14-lịch-sử-thay-đổi)

---

## 1. Sơ đồ luồng dữ liệu

```
Node A ──ESP-NOW──> Node B ──MQTT (Mosquitto trên laptop)──> Gateway Python ──MQTT──> CoreIOT
(cảm biến,           (quạt, đèn,     room1/#                   (bridge, OTA, AI)      app.coreiot.io
 TinyML)              LCD, web)                                    │
                                                                   └──> Giao diện local (Streamlit)
```

| Đoạn | Giao thức | Ai viết bên gửi | Ai viết bên nhận |
|---|---|---|---|
| Node A → Node B | ESP-NOW, struct cố định (mục 3) | TV1 Minh | TV1 Minh |
| Node B ↔ Gateway | MQTT local, JSON (mục 4–10) | TV1 Minh | TV2 Tuấn |
| Gateway ↔ giao diện local | MQTT local, cùng topic | TV2 Tuấn / TV3 Phú | TV3 Phú |
| Gateway ↔ CoreIOT | MQTT Gateway API (mục 11) | TV2 Tuấn | TV4 Khoa (dashboard, RuleChain) |

**Trước khi có Node B thật:** `fake_node_b.py` (TV2) đóng vai Node B và phải làm **đúng** mục 4–9 của file này. Khi Node B thật chạy, tắt `fake_node_b.py`, các phần khác không sửa gì.

---

## 2. Quy ước chung

| Quy ước | Chi tiết |
|---|---|
| Tên key | `snake_case`, tiếng Anh, viết thường. Ví dụ `fan_level`, không phải `fanLevel`. |
| Số | Gửi kiểu số JSON, **không** để trong dấu nháy. Đúng: `"temperature": 30.5`. Sai: `"temperature": "30.5"`. |
| Boolean | `true` / `false`, **không** dùng `0` / `1`. |
| Không có giá trị | **Bỏ hẳn key đó**. Không gửi `null`, không gửi `NaN` (NaN làm hỏng JSON, Python `json.loads` báo lỗi). |
| Số lẻ | `temperature`, `humidity`: 1 chữ số thập phân. `anomaly_score`: 3 chữ số. |
| Thời gian `ts` | Unix epoch **mili giây** (giờ NTP). Node B chưa đồng bộ NTP thì bỏ key `ts`, Gateway tự dùng giờ của laptop. |
| Mã hóa | UTF-8. Nội dung tin nhắn (`msg`) viết tiếng Việt có dấu được. |
| Bí mật | Mật khẩu WiFi, token CoreIOT chỉ để trong `secrets.h` / `.env` (đã có trong `.gitignore`). Repo chỉ có `secrets.example.h`, `.env.example`. |

**Đơn vị:**

| Key | Đơn vị | Khoảng hợp lệ |
|---|---|---|
| `temperature` | °C | -10 … 60 (ngoài khoảng = lỗi cảm biến, Node A không gửi) |
| `humidity` | % | 0 … 100 |
| `light` | giá trị ADC thô 12 bit | 0 … 4095 (càng sáng số càng lớn; lab 13/11: che kín ~25–100, phòng bật đèn ~800–1100, rọi đèn ~3000) |
| `anomaly_score` | không đơn vị | 0.0 … 1.0 |
| `rssi` | dBm | -100 … 0 |

---

## 3. ESP-NOW: Node A ↔ Node B

File chung: `App/IOT/firmware/common/espnow_packet.h`. Cả 2 node cùng `#include`, **không ai copy struct ra file khác**.

```cpp
#pragma once
#include <stdint.h>

#define ESPNOW_MAGIC   0x31544C42u  // "BTL1": bỏ gói lạ từ thiết bị khác trong phòng lab
#define ESPNOW_VERSION 1            // tăng khi đổi struct; Node B bỏ gói sai version

enum PktType : uint8_t {
  PKT_PAIR_REQ  = 1,  // A -> B (broadcast): xin ghép cặp
  PKT_PAIR_RESP = 2,  // B -> A (unicast): trả lời, kèm channel
  PKT_DATA      = 3,  // A -> B: số đo
  PKT_CONFIG    = 4,  // B -> A: cấu hình cho Node A (làm từ W2)
};

typedef struct __attribute__((packed)) {
  uint32_t magic;    // = ESPNOW_MAGIC
  uint8_t  version;  // = ESPNOW_VERSION
  uint8_t  type;     // PktType
} PktHeader;         // 6 byte

typedef struct __attribute__((packed)) {
  PktHeader hdr;
} PktPairReq;        // 6 byte

typedef struct __attribute__((packed)) {
  PktHeader hdr;
  uint8_t   channel;  // channel WiFi Node B đang dùng (= channel hotspot)
} PktPairResp;       // 7 byte

typedef struct __attribute__((packed)) {
  PktHeader hdr;
  uint32_t seq;            // tăng 1 mỗi mẫu mới, về 0 khi Node A khởi động lại
  float    temperature;    // °C, đã kiểm hợp lệ (không bao giờ NaN)
  float    humidity;       // %
  uint16_t light;          // ADC thô 0..4095
  uint8_t  presence;       // 0/1, ĐÃ tính thời gian chờ phòng trống
  uint8_t  anomaly;        // 0/1, kết quả TinyML
  float    anomaly_score;  // 0..1; chưa chạy TinyML thì gửi -1
  uint16_t dht_fail;       // tổng số lần đọc DHT20 lỗi từ lúc khởi động
} PktData;           // 28 byte (giới hạn ESP-NOW 250 byte)

typedef struct __attribute__((packed)) {
  PktHeader hdr;
  uint16_t  empty_timeout_s;  // thời gian chờ "phòng trống" cho Node A
} PktConfig;         // 8 byte
```

**Quy tắc gửi (Node A):**
- Chu kỳ 2 giây một mẫu.
- **Chưa có số đo hợp lệ thì không gửi.** DHT20 lỗi hoặc ngoài khoảng ở mục 2 thì bỏ mẫu đó, tăng `dht_fail`.
- Ghép cặp: broadcast `PKT_PAIR_REQ`, nhận `PKT_PAIR_RESP` thì học MAC Node B. **Không hardcode MAC** (board mượn mỗi buổi khác nhau).
- Gửi `PKT_DATA` bằng unicast tới Node B. Gửi thất bại thì gửi lại **cùng `seq`**, tối đa 3 lần.
- Lỗi liên tiếp khoảng 10 giây: coi như mất Node B, quét lại channel hotspot và ghép cặp lại.
- `PKT_CONFIG`: W1 Node A dùng hằng số `empty_timeout_s = 60` (demo). Nhận `PKT_CONFIG` làm từ W2.

**Quy tắc nhận (Node B):**
- Callback nhận chỉ kiểm `len`, `magic`, `version`, copy vào queue rồi thoát. Không xử lý nặng trong callback.
- `seq` trùng với gói vừa nhận: bỏ (gói gửi lại). `seq` nhảy cóc: cộng vào số gói mất. `seq` nhỏ hơn trước: Node A vừa khởi động lại, đếm lại.
- Không nhận `PKT_DATA` quá 10 giây: `node_a_online = false`.

---

## 4. MQTT local: danh sách topic

Broker: Mosquitto trên laptop, cổng 1883, cho phép kết nối không mật khẩu trong mạng hotspot.
Địa chỉ broker: Node B đọc `MQTT_HOST` trong `secrets.h` (IP laptop có thể đổi mỗi lần bật hotspot).

| Topic | Hướng | Retained | QoS | Nội dung | Mục |
|---|---|---|---|---|---|
| `room1/telemetry` | Node B → Gateway, UI | không | 0 | Số đo + trạng thái | 5 |
| `room1/cmd` | Gateway, UI → Node B | không | 1 | Lệnh | 6 |
| `room1/cmd/resp` | Node B → Gateway, UI | không | 0 | Kết quả lệnh | 6 |
| `room1/config` | Gateway → Node B | **có** | 1 | Bộ ngưỡng đầy đủ | 7 |
| `room1/status` | Node B → mọi người | **có** | 1 | online/offline (Last Will) | 8 |
| `room1/gateway/status` | Gateway → Node B, UI | **có** | 1 | online/offline (Last Will) | 8 |
| `room1/ota` | Gateway → Node B | không | 1 | Có firmware mới | 9 |
| `room1/ota/status` | Node B → Gateway | không | 0 | Tiến trình OTA | 9 |
| `room1/event` | Gateway (AI) → UI | không | 0 | Sự kiện AI | 10 |

**Client ID MQTT** (không được trùng, trùng là broker đá nhau ra):
`room1-node-b`, `room1-gateway`, `room1-ui`, `room1-fake-node-b`.

**Lưu ý cho firmware Node B (PubSubClient):**
- Buffer mặc định 256 byte **không đủ** cho telemetry. Gọi `mqtt.setBufferSize(1024)` trước khi `connect`.
- PubSubClient chỉ publish được QoS 0. Retained thì dùng `publish(topic, payload, true)`.

---

## 5. Telemetry `room1/telemetry`

**Khi nào gửi:**
- Mỗi lần Node B nhận một `PKT_DATA` hợp lệ (khoảng 2 giây một lần).
- Ngay khi quạt, đèn hoặc chế độ đổi (để dashboard cập nhật nhanh).
- Node A mất (`node_a_online = false`): vẫn gửi 5 giây một lần, **bỏ các key cảm biến**.

**Ví dụ đầy đủ:**
```json
{
  "ts": 1762761600123,
  "seq": 1532,
  "temperature": 30.5,
  "humidity": 65.2,
  "light": 1830,
  "presence": true,
  "anomaly": false,
  "anomaly_score": 0.124,
  "dht_fail": 0,
  "node_a_online": true,
  "espnow_loss": 0.8,
  "fan_level": 1,
  "light_on": true,
  "mode": "auto",
  "rssi": -45,
  "fw_version": "1.0.0",
  "uptime_s": 3605
}
```

**Ví dụ khi mất Node A:**
```json
{ "ts": 1762761700000, "node_a_online": false, "fan_level": 1, "light_on": true, "mode": "auto", "rssi": -46, "fw_version": "1.0.0", "uptime_s": 3705 }
```

| Key | Kiểu | Nguồn | Ý nghĩa |
|---|---|---|---|
| `ts` | int | Node B | Lúc Node B nhận gói từ Node A (epoch ms, NTP) |
| `seq` | int | Node A | Số thứ tự gói ESP-NOW |
| `temperature` | float | Node A, Node B tính | **Trung bình 2 lần đo gần nhất** (Node B tính, mục 6 tài liệu dự án) |
| `humidity` | float | Node A | Độ ẩm |
| `light` | int | Node A | ADC thô |
| `presence` | bool | Node A | Có người (đã tính thời gian chờ) |
| `anomaly` | bool | Node A | TinyML báo bất thường |
| `anomaly_score` | float | Node A | 0–1. Node A gửi -1 thì Node B **bỏ key này** |
| `dht_fail` | int | Node A | Tổng lần DHT20 lỗi |
| `node_a_online` | bool | Node B | Có nhận gói Node A trong 10 giây gần nhất |
| `espnow_loss` | float | Node B | % gói mất từ lúc Node A khởi động, = mất / (nhận + mất) × 100 |
| `fan_level` | int | Node B | Mức quạt **thật** đang chạy: 0, 1, 2 |
| `light_on` | bool | Node B | Đèn (LED RGB) đang bật |
| `mode` | string | Node B | `"auto"`, `"manual"`, `"offline"` (mục 13) |
| `rssi` | int | Node B | RSSI WiFi tới hotspot |
| `fw_version` | string | Node B | Phiên bản firmware, dạng `"MAJOR.MINOR.PATCH"` |
| `uptime_s` | int | Node B | Giây từ lúc khởi động (thấy được watchdog reset) |

---

## 6. Lệnh `room1/cmd` và trả lời `room1/cmd/resp`

### 6.1. Định dạng lệnh
```json
{ "id": "tb-17", "method": "setFan", "params": 2 }
```
- `id`: **chuỗi**, duy nhất, dùng để ghép lệnh với trả lời. Tiền tố theo người gửi:
  - `tb-<id RPC của CoreIOT>`: Gateway chuyển RPC từ CoreIOT (dashboard hoặc RuleChain).
  - `ui-<số tăng dần>`: giao diện local Streamlit.
  - `test-<bất kỳ>`: gõ tay bằng `mosquitto_pub` khi test.
- `method`: một trong bảng 6.2.
- `params`: kiểu theo bảng 6.2.

### 6.2. Danh sách method

| method | params | Ai gửi | Node B làm gì |
|---|---|---|---|
| `setFan` | int `0`/`1`/`2` | Người bấm (dashboard, UI, webserver) | Đặt quạt, chuyển `mode = manual`, đếm giờ `manual_timeout_s` |
| `setLight` | bool | Người bấm | Đặt đèn, chuyển `mode = manual` |
| `setMode` | `"auto"` / `"manual"` | Người bấm | Đổi chế độ. **Không** đặt được `"offline"` (Node B tự vào) |
| `autoFan` | int `0`/`1`/`2` | **Chỉ RuleChain** | Chỉ làm khi `mode == "auto"`, **không** đổi chế độ |
| `autoLight` | bool | **Chỉ RuleChain** | Chỉ làm khi `mode == "auto"`, **không** đổi chế độ |
| `getState` | `null` hoặc bỏ | Ai cũng được | Không đổi gì, trả về trạng thái hiện tại |

Tách `setFan` (người bấm) và `autoFan` (RuleChain) để Node B biết lệnh nào là lệnh tay. Lệnh tay thắng luật tự động trong `manual_timeout_s` (mục 6 tài liệu dự án).

### 6.3. Trả lời `room1/cmd/resp`
Node B trả lời **mọi** lệnh có `id`, trong vòng 1 giây.

Thành công:
```json
{ "id": "tb-17", "method": "setFan", "ok": true,
  "state": { "fan_level": 2, "light_on": false, "mode": "manual" } }
```
Không làm (đang manual nên bỏ lệnh của RuleChain):
```json
{ "id": "tb-18", "method": "autoFan", "ok": false, "error": "ignored_not_auto",
  "state": { "fan_level": 2, "light_on": false, "mode": "manual" } }
```

| `error` | Khi nào |
|---|---|
| `unknown_method` | `method` không có trong bảng 6.2 |
| `bad_params` | Sai kiểu hoặc ngoài khoảng (vd `setFan` 3) |
| `ignored_not_auto` | `autoFan`/`autoLight` khi `mode` khác `"auto"` |
| `bad_json` | Không parse được (khi đó không có `id`, trả `"id": null`) |

`state` luôn có đủ 3 key `fan_level`, `light_on`, `mode` (kể cả khi lỗi).

### 6.4. Test bằng tay
```bash
mosquitto_sub -h localhost -t "room1/#" -v
mosquitto_pub -h localhost -t room1/cmd -m "{\"id\":\"test-1\",\"method\":\"setFan\",\"params\":2}"
```
(PowerShell: dùng `-m '{\"id\":\"test-1\",\"method\":\"setFan\",\"params\":2}'`.)

---

## 7. Ngưỡng `room1/config`

Gateway publish **retained** bộ ngưỡng **đầy đủ** (không gửi từng key lẻ). Nhờ retained, Node B khởi động lại là broker gửi ngay bản mới nhất. Node B lưu vào NVS (`Preferences`) để dùng khi mất mạng.

```json
{
  "fan_on_1": 31.0, "fan_off_1": 29.0,
  "fan_on_2": 33.0, "fan_off_2": 31.0,
  "light_dark": 1500, "light_bright": 2500,
  "empty_timeout_s": 600,
  "manual_timeout_s": 1800
}
```

| Key | Kiểu | Mặc định | Demo | Ý nghĩa |
|---|---|---|---|---|
| `fan_on_1` | float °C | 31.0 | | ≥ thì bật quạt mức 1 |
| `fan_off_1` | float °C | 29.0 | | ≤ thì tắt quạt |
| `fan_on_2` | float °C | 33.0 | | ≥ thì lên mức 2 |
| `fan_off_2` | float °C | 31.0 | | ≤ thì về mức 1 |
| `light_dark` | int ADC | 1500 | | Tối hơn ngưỡng này và có người thì bật đèn |
| `light_bright` | int ADC | 2500 | | Sáng hơn ngưỡng này thì tắt đèn |
| `empty_timeout_s` | int giây | 600 | 60 | Không có chuyển động bao lâu thì coi là phòng trống (Node A dùng) |
| `manual_timeout_s` | int giây | 1800 | 120 | Lệnh tay giữ bao lâu rồi về auto (Node B dùng) |

**Ràng buộc** (Gateway kiểm trước khi gửi, Node B kiểm lại, sai thì giữ bộ cũ):
`fan_off_1 < fan_on_1 <= fan_off_2 < fan_on_2`, và `light_dark < light_bright`.

Đã kiểm trên board 13/11: ADC càng lớn càng sáng. Phòng bật đèn đo ~800–1100 < `light_dark`, nên có người là bật đèn; chỉ rất sáng (> `light_bright`) mới tắt. Cố ý vì phòng học thường luôn bật đèn.

Node A không nối WiFi nên `empty_timeout_s` đi tiếp xuống Node A bằng `PKT_CONFIG` (làm từ W2).

---

## 8. Trạng thái online/offline

**`room1/status`** (Node B, retained):
```json
{ "state": "online", "fw_version": "1.0.0", "ip": "10.199.40.70" }
```
Last Will (broker tự gửi khi Node B rớt), retained:
```json
{ "state": "offline" }
```

**`room1/gateway/status`** (Gateway, retained): giống hệt, `{"state":"online"}`, Last Will `{"state":"offline"}`.

Node B vào `mode = "offline"` khi **mất broker** hoặc **`room1/gateway/status` = offline** quá 30 giây (mục 13).

---

## 9. OTA `room1/ota` và `room1/ota/status`

**`room1/ota`** (Gateway → Node B):
```json
{
  "version": "1.0.1",
  "url": "http://10.199.40.112:8000/fw/node_b_1.0.1.bin",
  "size": 702157,
  "sha256": "9f2c...e81a"
}
```
- `url`: link HTTP trên laptop (Gateway mở server). Dùng IP, không dùng tên miền.
- `sha256`: chuỗi hex 64 ký tự, chữ thường. Node B tính lại trong lúc tải, sai thì hủy.
- `version` bằng `fw_version` đang chạy thì Node B bỏ qua.

**`room1/ota/status`** (Node B → Gateway):
```json
{ "version": "1.0.1", "state": "downloading", "progress": 45 }
```

| `state` | Ý nghĩa |
|---|---|
| `downloading` | Đang tải, `progress` 0–100 (gửi mỗi 10%) |
| `verifying` | Tải xong, đang kiểm sha256 |
| `rebooting` | Ghi xong, sắp khởi động lại |
| `success` | Gửi **sau khi** khởi động lại bằng bản mới và nối được MQTT |
| `failed` | Kèm `"error"`: `download_failed`, `checksum_mismatch`, `write_failed`, `rolled_back` |

Gateway đổi các state này sang thuộc tính OTA của CoreIOT (`fw_state`...). Cách đổi chính xác: chờ kết quả issue kiểm tra OTA trên CoreIOT (TV2), rồi ghi bổ sung vào mục này.

---

## 10. Sự kiện AI `room1/event`

Module AI trên Gateway (TV3) publish để giao diện local hiện, đồng thời Gateway gửi lên CoreIOT (mục 11).

```json
{
  "ts": 1762761600123,
  "type": "waste_energy",
  "level": "warning",
  "msg": "Quạt bật 15 phút trong khi phòng trống"
}
```

| `type` | Loại | Ví dụ |
|---|---|---|
| `waste_energy` | Luật | Quạt/đèn bật khi phòng trống quá lâu |
| `sensor_stuck` | Luật | Một giá trị đứng yên nhiều lần liên tiếp |
| `node_offline` | Luật | Node A hoặc Node B ngừng gửi quá lâu |
| `anomaly_ml` | Học máy (Isolation Forest) | Bất thường theo thời gian |

`level`: `"info"`, `"warning"`, `"critical"`.

---

## 11. CoreIOT: thiết bị, key, RPC, attribute

Làm trên **app.coreiot.io**. API Gateway theo slide thầy `IoT_Gateway_RuleChain_VN.pptx`. Thư viện Python: `paho-mqtt==1.6.1`.

### 11.1. Thiết bị
| Tên | Loại | Ghi chú |
|---|---|---|
| `room1-gateway` | Gateway (Entities → Gateways) | Token để trong `.env` (`COREIOT_TOKEN`) |
| `room1-node-a` | Thiết bị con, tự tạo | Cảm biến |
| `room1-node-b` | Thiết bị con, tự tạo | Chấp hành. Nhận RPC, giữ shared attribute ngưỡng, nhận OTA |

### 11.2. Tách telemetry
Gateway nhận một bản `room1/telemetry`, tách thành 2 thiết bị, giữ `ts` gốc:

| Thiết bị | Key |
|---|---|
| `room1-node-a` | `temperature`, `humidity`, `light`, `presence`, `anomaly`, `anomaly_score`, `seq`, `dht_fail`, `espnow_loss` |
| `room1-node-b` | `fan_level`, `light_on`, `mode`, `rssi`, `fw_version`, `uptime_s` |

Ví dụ publish lên `v1/gateway/telemetry`:
```json
{
  "room1-node-a": [{ "ts": 1762761600123,
                     "values": { "temperature": 30.5, "humidity": 65.2, "light": 1830,
                                 "presence": true, "anomaly": false, "anomaly_score": 0.124,
                                 "seq": 1532, "dht_fail": 0, "espnow_loss": 0.8 } }],
  "room1-node-b": [{ "ts": 1762761600123,
                     "values": { "fan_level": 1, "light_on": true, "mode": "auto",
                                 "rssi": -45, "fw_version": "1.0.0", "uptime_s": 3605 } }]
}
```
Key nào không có trong bản tin local thì không gửi.

### 11.3. Online/offline thiết bị con
| Sự kiện local | Gateway publish |
|---|---|
| Gateway khởi động | `v1/gateway/connect` `{"device":"room1-node-a"}` và `{"device":"room1-node-b"}` |
| `room1/status` = offline | `v1/gateway/disconnect` `{"device":"room1-node-b"}` |
| `node_a_online` = false | `v1/gateway/disconnect` `{"device":"room1-node-a"}` |
| Có lại | `v1/gateway/connect` tương ứng |

### 11.4. RPC
RPC luôn gửi tới `room1-node-b`. Tên method và `params` **giống hệt** mục 6.2.

CoreIOT → Gateway (`v1/gateway/rpc`):
```json
{ "device": "room1-node-b", "data": { "id": 17, "method": "setFan", "params": 2 } }
```
Gateway → Node B (`room1/cmd`):
```json
{ "id": "tb-17", "method": "setFan", "params": 2 }
```
Node B trả `room1/cmd/resp` → Gateway trả CoreIOT (`v1/gateway/rpc`):
```json
{ "device": "room1-node-b", "id": 17,
  "data": { "ok": true, "fan_level": 2, "light_on": false, "mode": "manual" } }
```
Không có trả lời sau 5 giây thì Gateway trả `"data": {"ok": false, "error": "timeout"}`.

**Dashboard (TV4):** nút/công tắc gọi `setFan`, `setLight`, `setMode`. Widget cần đọc trạng thái ban đầu thì gọi `getState` và dùng parse function, ví dụ `return data.fan_level;`.
**RuleChain (TV4):** chỉ gửi `autoFan`, `autoLight`, **không bao giờ** gửi `setFan`/`setLight`.

### 11.5. Shared attribute ngưỡng
- Lưu trên `room1-node-b`, tên key đúng bảng mục 7.
- Gateway khởi động: hỏi toàn bộ bằng `v1/gateway/attributes/request`. Khi dashboard đổi: nhận qua `v1/gateway/attributes`. Cả hai trường hợp đều gộp thành bộ đầy đủ, kiểm ràng buộc, rồi publish retained `room1/config`.
- RuleChain đọc ngưỡng từ shared attribute của `room1-node-b` (node enrichment).

### 11.6. Sự kiện AI
Gateway gửi telemetry của chính nó (`v1/devices/me/telemetry`, token `room1-gateway`):
```json
{ "ts": 1762761600123, "values": { "event_type": "waste_energy", "event_level": "warning", "event_msg": "Quạt bật 15 phút trong khi phòng trống" } }
```

### 11.7. Cảnh báo (T13, TV4 làm trong RuleChain)
| Điều kiện | Mức |
|---|---|
| `temperature` ≥ 35 | Critical |
| `anomaly` = true | Warning |
| `event_level` = warning | Warning |
| Node B hoặc Gateway offline > 1 phút | Warning |
| `presence` chuyển false và đã tắt hết | Info |
| OTA `success` / `failed` | Info |

---

## 12. API nội bộ Node B cho webserver

Webserver (TV4) chạy thành một task trong Node B. Nó **không** dùng MQTT, gọi thẳng các hàm dưới đây (file `App/IOT/firmware/node_b/src/control.h`, TV1 viết).

```cpp
#pragma once
#include <stdint.h>

enum class Mode : uint8_t { Auto, Manual, Offline };
enum class CmdSource : uint8_t { Rule, Dashboard, LocalUi, Web, Serial };

struct ControlState {
  uint8_t fan_level;    // 0..2
  bool    light_on;
  Mode    mode;
};

struct SensorSnapshot {
  bool     valid;        // false nếu chưa có gói nào hoặc Node A mất
  float    temperature;
  float    humidity;
  uint16_t light;
  bool     presence;
  bool     anomaly;
  float    anomaly_score;
};

// Thread-safe (có mutex bên trong), gọi từ task nào cũng được.
// Trả false nếu params sai, hoặc lệnh Rule bị bỏ vì không ở auto.
bool control_set_fan(uint8_t level, CmdSource src);
bool control_set_light(bool on, CmdSource src);
bool control_set_mode(Mode m);              // chỉ Auto hoặc Manual
ControlState   control_get_state();
SensorSnapshot control_get_sensors();
const char*    mode_to_str(Mode m);         // "auto" / "manual" / "offline"
```

Tuần 1 Khoa viết webserver đứng riêng thì tự tạo bản giả (stub) của các hàm này, cùng chữ ký, rồi W2 thay bằng bản thật.

---

## 13. Chế độ auto / manual / offline

| Chế độ | Ai quyết định | Vào khi | Ra khi |
|---|---|---|---|
| `auto` | RuleChain (`autoFan`, `autoLight`) | Mặc định khi khởi động; hết `manual_timeout_s`; phòng trống | Có lệnh tay; mất Gateway/broker > 30 s |
| `manual` | Người bấm | `setFan`/`setLight` từ dashboard, UI, webserver; `setMode "manual"` | Hết `manual_timeout_s` → auto; phòng trống → auto; `setMode "auto"` |
| `offline` | Node B tự chạy luật với ngưỡng đã lưu | Mất broker, hoặc `room1/gateway/status` offline > 30 s | Có lại cả broker và Gateway → về chế độ trước đó |

**Thứ tự ưu tiên** (Node B tự áp dụng, kể cả khi đang online):
1. `presence` = false (phòng trống): tắt quạt, tắt đèn, xóa manual, về `auto`.
2. Lệnh tay: thắng luật tự động trong `manual_timeout_s`.
3. Luật tự động (RuleChain khi online, Node B khi offline), có hysteresis theo ngưỡng mục 7.

Lệnh tay từ webserver vẫn dùng được khi `offline`: Node B chuyển sang `manual`, hết giờ thì quay về `offline` nếu vẫn còn mất mạng.

---

## 14. Lịch sử thay đổi

| Phiên bản | Ngày | Ai | Thay đổi |
|---|---|---|---|
| 1.0 | 10/11/2026 | Minh | Bản đầu, từ mục 6–11 tài liệu dự án. Thêm so với tài liệu dự án: lệnh `autoFan`/`autoLight` riêng cho RuleChain, `getState`, `id` dạng chuỗi, topic `room1/config` (retained), `room1/gateway/status`, `room1/ota/status`, `room1/event`, key `node_a_online`, `espnow_loss`, `dht_fail`, `uptime_s`, header `magic`/`version`/`type` cho gói ESP-NOW, API `control.h` cho webserver. |
