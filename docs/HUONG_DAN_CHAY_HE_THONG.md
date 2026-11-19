# Hướng dẫn chạy toàn hệ thống

Chạy đủ chuỗi Node A -> Node B -> Mosquitto -> bridge -> CoreIOT (dashboard, RuleChain) trên phần cứng thật, từ máy chưa cài gì tới lúc bấm dashboard thấy quạt quay.

File này là nguồn cho README và chương "Hướng dẫn sử dụng mã nguồn" của báo cáo. Sửa code làm đổi cách chạy thì sửa file này luôn.

**Tình trạng (19/11/2026, main e214fc0):**

| Phần | Có chưa | Mục |
|---|---|---|
| Node A: DHT20, PIR, ánh sáng, ESP-NOW, nhận ngưỡng từ Node B | Có | 4, 5 |
| Node B: nhận ESP-NOW, MQTT, quạt, đèn RGB, lệnh, ngưỡng lưu NVS | Có | 4, 5 |
| Gateway `bridge.py`: CoreIOT, RPC, đồng bộ ngưỡng | Có | 5 (bước 7) |
| CoreIOT: dashboard, RuleChain tự động (T12), cảnh báo (T13) | Có | 7 |
| LCD, webserver trên Node B | Chưa (W3) | 10 |
| OTA qua Gateway | Chưa (W3, #45) | 10 |
| TinyML trên Node A, AI + giao diện local trên Gateway | Chưa (#30, #31, #48) | 10 |
| Chế độ offline (mất Gateway) | Chưa (W4) | 10 |

---

## 1. Sơ đồ và ai chạy gì

```
Node A --ESP-NOW--> Node B --MQTT room1/#--> Mosquitto --> bridge.py --MQTT--> CoreIOT
(DHT20, PIR,        (quạt, đèn RGB)          (laptop)      (laptop)            app.coreiot.io
 ánh sáng)
```

| Thành phần | Chạy ở đâu | Ghi chú |
|---|---|---|
| Node A | YoloUNO, cắm củ sạc hoặc laptop | Không cần WiFi, tự tìm Node B |
| Node B | YoloUNO, cắm củ sạc hoặc laptop | Nối hotspot, gửi MQTT về laptop |
| Mosquitto | Laptop | Broker local, cổng 1883 |
| `bridge.py` | Laptop, **chỉ 1 máy trong nhóm** | 2 máy cùng chạy thì CoreIOT ngắt cả hai (`rc=7`) |
| Dashboard | Trình duyệt bất kỳ | Xem số đo, bấm quạt/đèn, sửa ngưỡng |

Laptop, Node B và điện thoại (hotspot) phải chung một mạng. Mạng trường không dùng được, dùng hotspot điện thoại.

---

## 2. Cài đặt (làm 1 lần trên mỗi máy)

Lệnh trong file viết theo repo ở `D:\Assignment_IOT_HK261`, PowerShell trên Windows. Máy khác thì đổi đường dẫn. Đường dẫn **không được có dấu tiếng Việt** (toolchain ESP32 báo `Invalid argument`).

| Phần mềm | Dùng cho | Ghi chú |
|---|---|---|
| Git | Lấy code | |
| VS Code + extension PlatformIO | Build, nạp firmware | PlatformIO tự tải `espressif32@6.9.0`, lần đầu mất vài phút |
| Mosquitto 2.x | Broker MQTT | https://mosquitto.org/download/ , cài mặc định vào `C:\Program Files\mosquitto\` |
| Python 3.10 trở lên | `bridge.py`, script dữ liệu | |

1. Lấy code:
   ```powershell
   cd D:\
   git clone https://github.com/khoaphunsonac/Assignment_IOT_HK261.git
   cd D:\Assignment_IOT_HK261
   ```
2. Môi trường Python (ở thư mục gốc repo):
   ```powershell
   python -m venv .venv
   .venv\Scripts\activate
   pip install paho-mqtt==1.6.1
   ```
3. Thêm `C:\Program Files\mosquitto` vào PATH để gõ được `mosquitto`, `mosquitto_sub`, `mosquitto_pub`. Không thêm thì gõ đủ đường dẫn, ví dụ `& "C:\Program Files\mosquitto\mosquitto_sub.exe"`.
4. Windows Firewall: cho phép `mosquitto.exe` cả mạng Private và Public. Không cho thì Node B không nối được broker.
5. PowerShell báo không có lệnh `pio` thì dùng `& "$HOME\.platformio\penv\Scripts\pio.exe"` thay cho `pio` ở mọi lệnh bên dưới.

---

## 3. Cấu hình bí mật (làm 1 lần, sửa khi đổi hotspot)

Hai file này đã có trong `.gitignore`. Repo public, **không commit**.

**`App/IOT/firmware/common/secrets.h`** (dùng chung Node A, Node B). Chép từ `secrets.example.h` cùng thư mục rồi điền:
```c
#define WIFI_SSID "ten_hotspot_cua_ban"
#define WIFI_PASS "mat_khau_hotspot"
#define MQTT_HOST "IPv4_cua_laptop"
```
`MQTT_HOST` lấy ở mục 5 bước 2.

**`App/IOT/gateway/.env`** (chỉ máy chạy bridge cần):
```powershell
copy App\IOT\gateway\.env.example App\IOT\gateway\.env
```
```env
COREIOT_TOKEN=token_cua_room1-gateway
MQTT_HOST=localhost
MQTT_PORT=1883
```
Token `room1-gateway` xin trong nhóm. Chỉ dùng token này cho bridge, không dùng token của `room1-node-a`, `room1-node-b` hay `room1-test`.

---

## 4. Phần cứng

2 board YoloUNO (ESP32-S3). Board mượn mỗi buổi khác nhau cũng được: firmware không ghi cứng MAC, 2 node tự ghép cặp.

| Node | Module | Cổng Grove | Chân |
|---|---|---|---|
| A | Cảm biến ánh sáng | A1-A0 (vàng) | GPIO 1 (ADC1) |
| A | PIR | D4-D3 (trắng) | GPIO 6 |
| A | DHT20 (I2C 0x38) | I2C (đỏ) | SDA 11, SCL 12 |
| B | Quạt mini (PWM) | D6-D5 (trắng) | GPIO 8 |
| B | LED RGB (NeoPixel) | D8-D7 (trắng) | GPIO 10 |
| B | LCD 16x2 (I2C 0x21), chưa dùng | I2C (đỏ) | SDA 11, SCL 12 |

Dây Grove hỏng hay làm DHT20 ra 0.00 hoặc 42 °C: đổi dây là hết.

---

## 5. Chạy mỗi buổi

Làm đúng thứ tự. Mỗi bước có "Đạt khi", chưa đạt thì đừng sang bước sau. Mở nhiều tab terminal, mỗi tab một việc.

### Bước 1. Hotspot
Bật hotspot điện thoại, băng tần **2.4 GHz** (hoặc "tương thích tối đa"), giữ màn hình sáng. Laptop nối vào hotspot này.

### Bước 2. Lấy IP laptop
```powershell
ipconfig
```
Lấy `IPv4 Address` của card **Wi-Fi** (bỏ card VirtualBox/VMware). Khác `MQTT_HOST` trong `secrets.h` thì sửa `secrets.h`, rồi phải nạp lại Node B (bước 5). Hotspot bật lại có thể cấp IP mới: làm lại bước này.

### Bước 3. Broker (tab 1, để chạy suốt buổi)
```powershell
cd D:\Assignment_IOT_HK261
mosquitto -c App\IOT\gateway\mosquitto.conf -v
```
Đạt khi: thấy `Opening ipv4 listen socket on port 1883`. Phải có `-c ...mosquitto.conf`, thiếu thì broker chỉ nghe localhost và Node B không vào được.

### Bước 4. Theo dõi MQTT (tab 2, để chạy suốt buổi)
```powershell
mosquitto_sub -h localhost -t "room1/#" -v
```
Tab này là log chính để kiểm và chụp minh chứng.

### Bước 5. Nạp và kiểm Node B (tab 3)
Cắm board B (quạt, LED RGB theo mục 4). Mỗi lần chỉ cắm 1 board vào laptop khi nạp, cắm 2 board thì phải thêm `--upload-port COMx`.
```powershell
cd D:\Assignment_IOT_HK261\App\IOT\firmware\node_b
pio run -t upload
pio device monitor
```
Log mong đợi:
```
===== NODE B fw 1.0.0 =====
[CFG] ... fan_on_1=31.0 ... empty=600s manual=1800s
[WIFI] Dang ket noi '...'....
[WIFI] OK IP=10.x.x.x ch=.. MAC=..
[MQTT] Ket noi 10.x.x.x:1883 ... OK
[B] === ESPNOW nhan=0 mat=0 ...        (mỗi 10 s)
```
Lần đầu trên board mới có thể có `nvs_open failed: NOT_FOUND`, bình thường.

Đạt khi tab 2 thấy:
- `room1/status {"state":"online","fw_version":"1.0.0","ip":"..."}`
- `room1/telemetry {... "node_a_online":false, "fan_level":0 ...}` mỗi 5 s, chưa có key cảm biến.

Muốn thử lệnh mà không bị luật phòng trống xen vào thì thử ở bước này, trước khi bật Node A (mục 8).

### Bước 6. Nạp và kiểm Node A (tab 4)
Nạp xong Node B thì rút ra cắm củ sạc, cắm board A (DHT20, PIR, ánh sáng).
```powershell
cd D:\Assignment_IOT_HK261\App\IOT\firmware\node_a
pio run -t upload
pio device monitor
```
Node A tự quét channel của hotspot rồi xin ghép cặp. Đạt khi:
- Node A in `[A] GHEP CAP OK voi Node B ...`. Node B in `[B] Gui PKT_CONFIG empty_timeout=X s (lan 1)`, với X là `empty=` trong dòng `[CFG]` lúc Node B boot.
- Dòng thống kê Node A (10 s/lần) có `empty: Xs cfg_rx: 1` trở lên. Node A boot luôn dùng 60 s, nên nếu X khác 60 thì Node A in thêm `[A] PKT_CONFIG empty_timeout 60 -> X s`; X bằng 60 thì không có dòng này, vẫn đạt.
- Tab 2: `room1/telemetry` mỗi khoảng 2 s, có đủ `temperature`, `humidity`, `light`, `presence`, `node_a_online:true`.

Xong có thể cắm Node A vào củ sạc. Không cần monitor Node A để hệ thống chạy.

### Bước 7. Bridge (tab 5, chỉ 1 máy trong nhóm)
```powershell
cd D:\Assignment_IOT_HK261
.venv\Scripts\activate
python App/IOT/gateway/bridge.py
```
Đạt khi log bridge có:
- `Đã kết nối tới Local Broker (localhost:1883)`
- `Đã kết nối tới CoreIOT Cloud (app.coreiot.io:1883)`
- `Đã đồng bộ Shared Attributes từ CoreIOT xuống room1/config`

Node B nhận bộ ngưỡng mới thì monitor B in `[CFG] Moi ...` (trùng bộ đang có thì in `Khong doi`).

### Bước 8. CoreIOT
Mở dashboard `Room 1` trên app.coreiot.io (link public xin trong nhóm). Đạt khi:
- Nhiệt độ, độ ẩm, ánh sáng là số thật của phòng, cập nhật khoảng 2 s.
- Bấm `Quạt 2`: quạt quay, tab 2 thấy `room1/cmd` có `"id":"tb-..."` và `room1/cmd/resp` có `"ok":true`.

Tới đây cả hệ thống đã chạy.

---

## 6. Kiểm tra nhanh toàn hệ thống

Dùng trước khi demo hoặc quay video. Mỗi dòng xem ở tab 2 trừ khi ghi khác.

| # | Làm | Đạt khi |
|---|---|---|
| 1 | Để yên | `room1/telemetry` khoảng 2 s/lần, `node_a_online:true` |
| 2 | Dashboard bấm `Quạt 2` | Quạt quay mạnh, resp `ok:true`, `mode:"manual"` |
| 3 | Dashboard bật công tắc đèn | LED RGB sáng, `light_on:true` |
| 4 | Dashboard bật `Chế độ auto` | `mode:"auto"` |
| 5 | Nắm tay quanh DHT20 tới khi vượt `fan_on_1` | RuleChain gửi `autoFan` (id `tb-...`), quạt lên mức tương ứng |
| 6 | Úp PIR, không cử động quá `empty_timeout_s` | `presence:false`, `fan_level:0`, `light_on:false`, `mode:"auto"`; CoreIOT có alarm `Room Empty` |
| 7 | Vẫy tay trước PIR | `presence:true` |
| 8 | Rút điện Node A | Sau khoảng 10 s `node_a_online:false`, telemetry 5 s/lần, mất key cảm biến; log bridge `room1-node-a -> disconnect` |
| 9 | Cắm lại Node A | Tự ghép cặp lại, telemetry có số đo |
| 10 | Rút điện Node B | Sau khoảng 20-25 s `room1/status {"state":"offline"}` |

Demo nên đặt ngưỡng ngắn (mục 9) để bước 6 không phải chờ 10 phút.

---

## 7. CoreIOT đã dựng sẵn những gì

Chi tiết và cách dựng lại trên tài khoản khác: [`App/IOT/coreiot/SETUP_COREIOT.md`](../App/IOT/coreiot/SETUP_COREIOT.md).

| Thứ | Tên | Vai trò |
|---|---|---|
| Gateway | `room1-gateway` | `bridge.py` đăng nhập bằng token này |
| Thiết bị con | `room1-node-a` | Số đo cảm biến (bridge tự tạo) |
| Thiết bị con | `room1-node-b` | Quạt, đèn, chế độ, nhận RPC, giữ 8 ngưỡng (bridge tự tạo) |
| Dashboard | `Room 1` | Số đo, biểu đồ, nút quạt/đèn/chế độ, sửa ngưỡng |
| RuleChain | `Room1 Auto Control` | T12: tính mức quạt, đèn theo ngưỡng, gửi `autoFan`/`autoLight` khi `mode = auto` |
| RuleChain | `Room1 Alarms` | T13: nhiệt độ cao, bất thường, mất kết nối, phòng trống |

---

## 8. Gửi lệnh bằng tay (không cần CoreIOT)

Gửi thẳng vào broker, dùng để thử Node B khi CoreIOT chậm. Định dạng đầy đủ: `docs/CONTRACT.md` mục 6.

```powershell
mosquitto_pub -h localhost -t room1/cmd -m '{\"id\":\"test-1\",\"method\":\"setFan\",\"params\":2}'
```

| Lệnh (`method`, `params`) | Ai gửi | Kết quả trên `room1/cmd/resp` |
|---|---|---|
| `setFan` `0`/`1`/`2` | Người bấm | `ok:true`, đổi quạt, `mode:"manual"` |
| `setLight` `true`/`false` | Người bấm | `ok:true`, đổi đèn, `mode:"manual"` |
| `setMode` `"auto"`/`"manual"` | Người bấm | `ok:true` |
| `autoFan` `0`/`1`/`2` | RuleChain | Chỉ chạy khi `mode:"auto"`, không thì `ignored_not_auto` |
| `autoLight` `true`/`false` | RuleChain | Như `autoFan` |
| `getState` | Bất kỳ | `ok:true` kèm trạng thái hiện tại |
| Tham số sai (vd `setFan 5`) | | `ok:false`, `bad_params` |
| Không phải JSON | | `ok:false`, `bad_json`, `id:null` |

Lưu ý:
- PowerShell 7.3+ làm mọi lệnh ra `bad_json`. Cách chắc ăn: ghi JSON ra file rồi gửi `-f`:
  ```powershell
  '{"id":"test-1","method":"setFan","params":2}' | Out-File -Encoding ascii c1.json
  mosquitto_pub -h localhost -t room1/cmd -f c1.json
  ```
- Node A đang chạy mà PIR không thấy ai quá `empty_timeout_s` thì Node B ép tắt quạt, đèn và về auto. Thử lệnh khi chưa bật Node A, hoặc vẫy tay trước PIR.
- Lệnh tay giữ `manual_timeout_s` rồi tự về auto. Mỗi lệnh tay mới đặt lại đồng hồ.

---

## 9. Đổi ngưỡng

Ngưỡng nằm ở shared attribute của `room1-node-b`. Sửa trên widget `Ngưỡng tự động` của dashboard, bấm lưu. Bridge đẩy xuống `room1/config`, Node B lưu vào NVS (rút điện vẫn giữ) và gửi `empty_timeout_s` xuống Node A qua ESP-NOW.

| Key | Mặc định | Nhỏ nhất | Nghĩa |
|---|---|---|---|
| `fan_on_1` / `fan_off_1` | 31 / 29 | | Từ mức 0: >= thì bật mức 1 / <= thì tắt quạt (°C) |
| `fan_on_2` / `fan_off_2` | 33 / 31 | | >= thì lên mức 2 / từ mức 2: <= thì về mức 1 (°C) |
| `light_dark` / `light_bright` | 1500 / 2500 | | Dưới thì bật đèn / trên thì tắt đèn (ADC) |
| `empty_timeout_s` | 600 | 60 | Không có chuyển động bao lâu thì coi là phòng trống |
| `manual_timeout_s` | 1800 | 120 | Lệnh tay giữ bao lâu rồi về auto |

Ràng buộc: `fan_off_1 < fan_on_1 <= fan_off_2 < fan_on_2` và `light_dark < light_bright`. Sai ràng buộc thì bridge và Node B bỏ, giữ bộ cũ.

**Demo:** đặt `empty_timeout_s = 60`, `manual_timeout_s = 120`, chỉnh `fan_on_1` cao hơn nhiệt độ phòng khoảng 1 °C để nắm tay là quạt bật. Demo xong trả lại giá trị cũ.

---

## 10. Phần sẽ thêm (điền khi merge)

| Phần | Issue | Cần ghi vào file này |
|---|---|---|
| TinyML trên Node A | #30 | Model ở đâu, nạp thế nào, `anomaly_score` trên dashboard |
| Giao diện local + AI Gateway | #31, #48 | Cài thêm gì, lệnh chạy, cổng web |
| OTA qua Gateway | #45 | Tạo package trên CoreIOT, `fw/`, cách kiểm `fw_state` |
| LCD, webserver Node B | W3 | Màn hình hiện gì, địa chỉ web, các nút |
| Chế độ offline | W4 | Mất Gateway thì Node B làm gì, cách thử |

---

## 11. Tắt và dọn sau buổi

1. Ctrl+C `bridge.py` (tab 5) trước, rồi các tab còn lại.
2. Có thử `room1/config` bằng tay thì xóa bản retained, không buổi sau Node B nhận lại bộ cũ:
   ```powershell
   mosquitto_pub -h localhost -t room1/config -r -n
   ```
3. Đã đổi ngưỡng để demo thì trả lại trên dashboard.
4. Board mượn: không cần xóa code, buổi sau nạp đè.

---

## 12. Chạy không có board

Thiếu board vẫn thử được Gateway, CoreIOT và dashboard: thay Node A + Node B bằng `fake_node_b.py`. Bỏ bước 5, 6, làm bước 3, 4 rồi:
```powershell
.venv\Scripts\activate
python App/IOT/gateway/fake_node_b.py
```
Rồi chạy bridge như bước 7. Tham số thêm (mất Node A, OTA lỗi) xem `App/IOT/gateway/README.md` mục 2.2. Có board thật thì tắt `fake_node_b.py`, không chạy cả hai cùng lúc.

---

## 13. Gặp lỗi

Xem [`docs/LOI_THUONG_GAP.md`](LOI_THUONG_GAP.md), chia theo USB/nạp code, mạng, lệnh PowerShell, Node A/B, bridge/CoreIOT. Gặp lỗi mới thì chụp nguyên log (monitor + tab 2) rồi thêm vào file đó.

Thu dữ liệu TinyML dùng bước 1-6 của file này, rồi làm tiếp theo [`docs/THU_DU_LIEU_TINYML.md`](THU_DU_LIEU_TINYML.md).
