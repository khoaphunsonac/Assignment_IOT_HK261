# Hướng dẫn chạy: firmware Node A, Node B, OTA (Minh, #68)

Phần này gồm:
- Node A: DHT20, PIR, ánh sáng, gửi ESP-NOW tới Node B (T06).
- Node B: nhận ESP-NOW, quạt, đèn RGB, MQTT về laptop, ngưỡng lưu NVS (T06, T14).
- OTA Node B qua WiFi (T14).
- Chạy testcase logic trên PC (không cần board).

Webserver và LCD chạy cùng firmware Node B, xem [`coreiot_web.md`](coreiot_web.md). Broker, bridge, CoreIOT xem [`HUONG_DAN_CHAY_HE_THONG.md`](../HUONG_DAN_CHAY_HE_THONG.md). Định dạng gói ESP-NOW, topic, lệnh OTA xem [`CONTRACT.md`](../CONTRACT.md) mục 3, 4, 9.

Lệnh viết theo repo ở `D:\Assignment_IOT_HK261`, PowerShell trên Windows. Không có lệnh `pio` thì thay bằng `& "$HOME\.platformio\penv\Scripts\pio.exe"`.

## 1. Cần cài gì

| Thứ | Dùng cho |
|---|---|
| VS Code + PlatformIO | Build, nạp, monitor. Cả 2 node dùng `espressif32@6.9.0` (Arduino core 2.0.17), PlatformIO tự tải lần đầu |
| `gcc`/`g++` trong PATH (MSYS2 hoặc MinGW) | Chỉ để chạy testcase trên PC (`pio test -e native`) |
| Mosquitto 2.x (`mosquitto`, `mosquitto_sub`, `mosquitto_pub`) | Broker local cho Node B, gửi lệnh OTA bằng tay |
| Python 3.10+ | `python -m http.server` phát file firmware khi OTA |
| `App/IOT/firmware/common/secrets.h` (chép từ `secrets.example.h`) | `WIFI_SSID`, `WIFI_PASS` của hotspot, `MQTT_HOST` = IP laptop. **Không** commit |
| 2 board YoloUNO, 2 cáp USB-C có dữ liệu | Node A, Node B |

Đường dẫn repo **không được có dấu tiếng Việt** (toolchain ESP32 báo `Invalid argument`).

Cắm module:

| Node | Module | Cổng Grove | Chân |
|---|---|---|---|
| A | Cảm biến ánh sáng | A1-A0 | GPIO 1 (ADC1) |
| A | PIR | D4-D3 | GPIO 6 |
| A | DHT20 (I2C 0x38) | I2C | SDA 11, SCL 12 |
| B | Quạt mini (PWM) | D6-D5 | GPIO 8 |
| B | LED RGB (NeoPixel) | D8-D7 | GPIO 10 |
| B | LCD 16x2 (I2C 0x21) | I2C | SDA 11, SCL 12 |

Board mượn mỗi buổi khác nhau cũng được: firmware không ghi cứng MAC, Node A tự quét channel hotspot rồi ghép cặp với Node B.

## 2. Chạy testcase trên PC (không cần board)

```powershell
cd D:\Assignment_IOT_HK261\App\IOT\firmware\node_a
pio test -e native
cd ..\node_b
pio test -e native
```
**Kết quả đúng:** Node A `17 Tests 0 Failures`; Node B 2 bộ `test_logic` 44 test và `test_ota` 32 test, đều `0 Failures`.

## 3. Nạp Node B (nạp trước)

Broker phải đang chạy (`mosquitto -c App\IOT\gateway\mosquitto.conf -v`, xem file tổng bước 3). Chỉ cắm **1 board** vào laptop khi nạp.
```powershell
cd D:\Assignment_IOT_HK261\App\IOT\firmware\node_b
pio run -t upload
pio device monitor
```
**Kết quả đúng:**
```
===== NODE B fw 1.0.0 =====
[OTA] Khoi dong: fw=1.0.0 phan_vung=app0 pending='' cho_xac_nhan=0
[CFG] ... fan_on_1=31.0 ... empty=600s manual=1800s
[WIFI] OK IP=10.x.x.x ch=.. MAC=..
[MQTT] Ket noi 10.x.x.x:1883 ... OK
[B] === ESPNOW nhan=0 mat=0 ...        (mỗi 10 s)
```
`mosquitto_sub -h localhost -t "room1/#" -v` thấy `room1/status {"state":"online","fw_version":"1.0.0",...}` và `room1/telemetry` mỗi 5 s (chưa có Node A thì `"node_a_online":false`). Lần đầu trên board mới có thể in `nvs_open failed: NOT_FOUND`, bình thường.

Xong thì Ctrl+C monitor, rút Node B ra cắm củ sạc.

## 4. Nạp Node A

Cắm board Node A.
```powershell
cd D:\Assignment_IOT_HK261\App\IOT\firmware\node_a
pio run -t upload
pio device monitor
```
**Kết quả đúng:**
```
===== NODE A =====
[A] Quet tim '<WIFI_SSID>'...
[A] Hotspot o channel 6
[A] GHEP CAP OK voi Node B XX:XX:XX:XX:XX:XX (ch 6)
[A] T: 29.1 H: 45.7 light: 950 presence: 1 valid: 1 dht_fail: 0      (mỗi 2 s)
[A] ch: 6 paired: 1 seq: 5 sent_ok: 5 resend: 0 ... | empty: 600s cfg_rx: 1   (mỗi 10 s)
```
Node A không cần WiFi, chỉ dùng hotspot để biết channel. Node B in `[B] seq=.. T=.. H=..` theo mỗi gói, `room1/telemetry` có đủ `temperature`, `humidity`, `light`, `presence`, `"node_a_online":true`. Xong có thể cắm Node A vào củ sạc.

**TinyML (T04):** đang làm ở W4. Khi merge, Node A in thêm `[ML] tu kiem: PASS` lúc boot và dòng `[ML] score ... anomaly ...` mỗi 2 s; mục này sẽ ghi cách đổi model (train lại rồi build lại).

## 5. OTA Node B (T14)

Node B nhận lệnh trên `room1/ota`, tải file `.bin` qua HTTP, kiểm sha256, ghi vào phân vùng còn trống rồi khởi động lại. Bản mới không nối được WiFi + MQTT trong 60 s thì tự quay về bản cũ (rollback). Chi tiết state xem CONTRACT mục 9.

Cách chuẩn là Gateway gửi lệnh (`ota_publish.py`, xem `gateway.md` của Tuấn). Dưới đây là cách làm tay, dùng khi kiểm riêng firmware.

**5.1. Build bản mới (không nạp).** Sửa `App/IOT/firmware/node_b/src/version.h` thành `"1.0.1"`, lưu.
```powershell
cd D:\Assignment_IOT_HK261\App\IOT\firmware\node_b
pio run
New-Item -ItemType Directory -Force D:\ota_fw
Copy-Item .pio\build\yolo_uno\firmware.bin D:\ota_fw\node_b_1.0.1.bin
git checkout -- src/version.h
```
Trả `version.h` về ngay, không commit số tăng.

**5.2. Tạo file lệnh.** Đổi `<IP>` thành IP laptop:
```powershell
$f = "D:\ota_fw\node_b_1.0.1.bin"; $ip = "<IP>"
@{version="1.0.1"; url="http://${ip}:8000/node_b_1.0.1.bin"; size=(Get-Item $f).Length; sha256=(Get-FileHash $f -Algorithm SHA256).Hash.ToLower()} | ConvertTo-Json -Compress | Set-Content -NoNewline -Encoding ascii D:\ota_fw\ota_101.json
```

**5.3. Phát file** (tab riêng, để chạy suốt lúc OTA):
```powershell
cd D:\ota_fw
python -m http.server 8000
```
Windows hỏi Firewall thì cho phép cả Private và Public.

**5.4. Gửi lệnh:**
```powershell
mosquitto_pub -h localhost -t room1/ota -f D:\ota_fw\ota_101.json
```
**Kết quả đúng** (khoảng 30 s):
- `room1/ota/status`: `downloading` 0, 10, ..., 100, rồi `verifying`, `rebooting`.
- Monitor Node B: `===== NODE B fw 1.0.1 =====`, `[OTA] Khoi dong: fw=1.0.1 phan_vung=app1 pending='1.0.1' cho_xac_nhan=1`, rồi `[OTA] Ban 1.0.1 OK sau N ms`.
- `room1/ota/status {"version":"1.0.1","state":"success"}`, `room1/status` có `"fw_version":"1.0.1"`.
- Gửi lại đúng lệnh đó: Node B in `[OTA] Trung ban dang chay 1.0.1, bo qua`, không có status mới.
- sha256 sai: tải tới 100 rồi `{"state":"failed","error":"checksum_mismatch"}`, board **không** khởi động lại, vẫn bản cũ.

Muốn về 1.0.0 thì nạp lại bằng cáp như mục 3.

## 6. Lỗi hay gặp

| Triệu chứng | Nguyên nhân / cách sửa |
|---|---|
| Upload báo `port busy` / `Access denied` | Monitor (hoặc tab khác) còn giữ cổng COM: Ctrl+C monitor rồi upload lại. Cắm 2 board cùng lúc thì thêm `--upload-port COMx` |
| Monitor đứng, board như treo khi tắt monitor | Dùng `pio device monitor` (đã đặt `monitor_dtr = 0`, `monitor_rts = 0`), **không** dùng extension Serial Monitor: nó kéo DTR/RTS làm chip vào chế độ nạp. Treo rồi thì bấm RST |
| Node A in `Quet tim ...` mãi | Hotspot tắt, sai `WIFI_SSID`, hoặc đang ở 5 GHz. Bật 2.4 GHz ("tương thích tối đa"). Hotspot bật lại đổi channel thì Node A tự quét lại sau khoảng 11 s |
| Node A ghép cặp hụt lặp lại | Node B chưa chạy hoặc chưa nối hotspot (Node B phải ở cùng channel với hotspot). Kiểm monitor Node B có `[WIFI] OK` |
| Node B `[MQTT] ... FAIL` lặp lại | `MQTT_HOST` khác IP laptop (hotspot cấp IP mới sau khi bật lại), broker chạy thiếu `-c mosquitto.conf`, hoặc Firewall chặn `mosquitto.exe`. Sửa `secrets.h` thì phải nạp lại Node B |
| `[A] ... valid: 0`, `dht_fail` tăng | DHT20 lỏng dây hoặc dây Grove hỏng (ra 0.00 hay 42 °C). Đổi dây; cắm lại thì Node A tự đọc lại, không cần reset |
| OTA `download_failed` | `http.server` không chạy, sai IP trong lệnh, hoặc Firewall chặn `python.exe` cổng 8000. Mở `http://<IP>:8000/` trên điện thoại cùng hotspot để kiểm |
| OTA báo `bad_json` | File lệnh ghi bằng UTF-8 có BOM hoặc có xuống dòng cuối: tạo lại đúng lệnh 5.2 (có `-NoNewline -Encoding ascii`) |

Lỗi khác xem [`LOI_THUONG_GAP.md`](../LOI_THUONG_GAP.md).
