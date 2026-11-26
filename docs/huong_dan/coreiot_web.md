# Hướng dẫn chạy: CoreIOT, webserver, LCD (Khoa, #68)

Phần này gồm ba thứ:
- Dashboard `Room 1` và 2 rule chain trên **app.coreiot.io**.
- Webserver điều khiển trong Node B.
- LCD 16x2 trên Node B.

Chi tiết cấu hình từng widget và từng node của rule chain nằm ở [`App/IOT/coreiot/SETUP_COREIOT.md`](../../App/IOT/coreiot/SETUP_COREIOT.md). File này chỉ ghi những gì cần làm để chạy được.

## 1. Cần cài gì

| Thứ | Dùng cho |
|---|---|
| Tài khoản CoreIOT (Tenant administrator), xin trong nhóm | Dashboard, rule chain |
| Python 3.10+, `pip install paho-mqtt==1.6.1` | `bridge.py`, `fake_node_b.py` (xem [gateway README](../../App/IOT/gateway/README.md)) |
| VS Code + PlatformIO | Nạp Node B (env `yolo_uno`) |
| `App/IOT/gateway/.env` có `COREIOT_TOKEN` của `room1-gateway` | Bridge gửi lên CoreIOT. Token xin trong nhóm, **không** commit |
| `App/IOT/firmware/common/secrets.h` (chép từ `secrets.example.h`) | WiFi hotspot + IP máy chạy broker. **Không** commit |
| Điện thoại cùng hotspot với Node B | Mở webserver |
| LCD 16x2 I2C (địa chỉ `0x21`), cắm cổng I2C (SDA 11, SCL 12) của Yolo UNO | LCD |

## 2. CoreIOT: dashboard và rule chain

Tài khoản của Khoa đã dựng sẵn hết, chỉ cần mở link dashboard:
https://app.coreiot.io/dashboards/d89e63c0-dd7f-11f1-9298-f795494fee72

Dựng lại trên tài khoản khác thì làm theo thứ tự sau:

1. **Tạo thiết bị.** Devices → **+** → tên `room1-gateway`, tick **Is gateway** → chép Access Token vào `.env` (`COREIOT_TOKEN`).
2. **Cho bridge tạo node A, B.** Chạy bridge một lần để Gateway tự tạo `room1-node-a`, `room1-node-b`. Không có board thì chạy `fake_node_b.py` thay:
   ```powershell
   # terminal 1
   python App/IOT/gateway/bridge.py
   # terminal 2
   python App/IOT/gateway/fake_node_b.py
   ```
3. **Đặt attribute.**
   - Server attribute `inactivityTimeout` = `60000` cho cả 3 thiết bị.
   - 8 shared attribute ngưỡng (CONTRACT 7) trên `room1-node-b`: `fan_on_1`=31, `fan_off_1`=29, `fan_on_2`=33, `fan_off_2`=31, `light_dark`=1500, `light_bright`=2500, `empty_timeout_s`=600, `manual_timeout_s`=1800.
4. **Import rule chain.** Rule chains → **+** → *Import rule chain*, lần lượt chọn:
   - `App/IOT/coreiot/rulechain_room1_alarms.json` (cảnh báo T13)
   - `App/IOT/coreiot/rulechain_room1_auto.json` (tự động quạt/đèn T12)
5. **Nối vào Root Rule Chain** (bước này hay quên). Mở *Root Rule Chain* → kéo 2 node *Rule Chain* vào, chọn `Room1 Alarms` và `Room1 Auto Control` → nối từ *Message Type Switch*:
   - `Room1 Alarms`: nhánh `Post telemetry`, `Activity Event`, `Inactivity Event`.
   - `Room1 Auto Control`: nhánh `Post telemetry`.
   - Bấm Save.
6. **Import dashboard.** Dashboards → **+** → *Import dashboard* → `App/IOT/coreiot/dashboard_room1.json`. Ba alias tự tìm thiết bị theo **tên** (`room1-node-a`, `room1-node-b`, `room1-gateway`), không phải sửa gì.

**Kết quả đúng** (bridge + Node B thật hoặc `fake_node_b.py` đang chạy):
- Các ô nhiệt độ, độ ẩm, ánh sáng, có người, quạt, đèn, chế độ ghi *Last update just now*. Biểu đồ chạy theo thời gian thực.
- Bấm **Quạt 1** thì ô *Mức quạt* đổi thành 1 và *Chế độ* thành `manual` sau khoảng 2 s.
- Ô *Phiên bản FW* hiện phiên bản firmware Node B.
- Khi OTA, *Trạng thái OTA* đi qua `DOWNLOADING` → `DOWNLOADED` → `UPDATING` → `UPDATED`. Nếu OTA lỗi thì ra `FAILED`, và *Lỗi OTA* ghi lý do, ví dụ `checksum_mismatch`.
- Bảng *Nhật ký sự kiện AI* có dòng mới mỗi khi Gateway nhận `room1/event`, mới nhất ở trên.
- Menu Alarms: nóng ≥ 35 °C ra `High Temperature`, dừng bridge > 1 phút ra `Device Offline`, sự kiện AI mức warning ra `AI Event`, OTA ra `OTA Success` / `OTA Failed`. Chuông góc phải báo `High Temperature`, `Device Offline`.

Thử nhanh bảng sự kiện AI và OTA mà không cần board (bridge + `fake_node_b.py` đang chạy, có `mosquitto_pub`):
```powershell
mosquitto_pub -t room1/event -q 1 -m '{"ts": 0, "type": "sensor_stuck", "level": "warning", "msg": "thu bang su kien"}'
```
Thử OTA: dùng `ota_publish.py` của Gateway (PR #56, xem `gateway.md` của Tuấn). Thêm `--ota-corrupt` cho `fake_node_b.py` thì ra `FAILED`.

## 3. Webserver trong Node B (T07)

1. Điền `WIFI_SSID`, `WIFI_PASS`, `MQTT_HOST` trong `firmware/common/secrets.h`. Dùng hotspot điện thoại; `MQTT_HOST` là IP laptop chạy broker.
2. Nạp Node B: mở thư mục `App/IOT/firmware/node_b` trong PlatformIO → **Upload** → **Monitor** (115200). Dòng lệnh:
   ```powershell
   cd App/IOT/firmware/node_b
   & "$HOME\.platformio\penv\Scripts\pio.exe" run -e yolo_uno -t upload
   & "$HOME\.platformio\penv\Scripts\pio.exe" device monitor -b 115200
   ```
3. Chờ Serial in dòng có IP:
   ```
   [WEB] mo http://192.168.x.y/ tren dien thoai cung hotspot
   ```
4. Mở địa chỉ đó trên điện thoại (cùng hotspot).

| Đường dẫn | Việc |
|---|---|
| `/` | Trang điều khiển: 7 ô số liệu, nút Quạt 0/1/2, Bật/tắt đèn, Auto/Manual. Tự tải lại mỗi 2 s |
| `/api/state` | JSON số liệu + trạng thái. Mất Node A thì chỉ còn `node_a_online: false` và trạng thái quạt/đèn |
| `/api/fan?level=0..2` | Đặt quạt, Node B tự chuyển `manual` |
| `/api/light?on=0\|1` | Bật/tắt đèn, chuyển `manual` |
| `/api/mode?value=auto\|manual` | Đổi chế độ |

**Kết quả đúng:**
- Bấm Quạt 2 thì quạt quay mạnh, nút sáng xanh. Serial in `[WEB] fan level=2 ok=1`.
- Khoảng 2 s sau, dashboard CoreIOT hiện `Mức quạt` = 2, `Chế độ` = `manual`.
- Đang `manual` thì rule chain không tự đổi quạt/đèn. Bấm Auto/Manual để trả về `auto`.
- Tham số sai (ví dụ `/api/fan?level=5`) trả HTTP 400 `{"ok":false,"error":"bad_params",...}`.

## 4. LCD 16x2

Không cần làm gì thêm: LCD chạy cùng firmware Node B ở trên (`task_lcd`). Lúc khởi động, Serial in một trong hai dòng:
- `[LCD] OK 0x21`: thấy LCD.
- `[LCD] khong thay LCD o 0x21 ...`: không thấy LCD. Kiểm lại dây, Node B vẫn chạy tiếp.

Màn đổi mỗi 3 s:

| Màn | Dòng 1 | Dòng 2 |
|---|---|---|
| 1 | `Nhiet do: 30.5°C` | `Do am   : 65.0 %` |
| 2 | `Quat: 1  Den:BAT` | `Che do: auto` |
| 1, khi mất Node A (> 10 s không có gói) | `Node A:` | `mat ket noi` |

Rút điện Node A khoảng 10 s thì màn 1 chuyển sang `Node A: mat ket noi`, Serial in `[LCD] Node A mat ket noi`. Cắm lại thì số đo hiện lại, Serial in `[LCD] Node A co lai`.

## 5. Lỗi hay gặp

| Triệu chứng | Nguyên nhân / cách sửa |
|---|---|
| LCD sáng đèn nền nhưng không có chữ, hoặc một hàng ô vuông đen | Chỉnh **biến trở tương phản** sau lưng module I2C. Nếu Serial báo `khong thay LCD o 0x21`: kiểm dây SDA/SCL, cắm đúng cổng I2C 11/12 |
| Điện thoại không mở được trang web | Điện thoại phải cùng hotspot với Node B (tắt 4G dự phòng / VPN). Không thấy dòng `[WEB] mo http://...` thì WiFi chưa nối: kiểm `WIFI_SSID`/`WIFI_PASS` trong `secrets.h`, hotspot để 2.4 GHz |
| Dashboard đứng số, thiết bị lúc online lúc offline | Có **2 bridge** cùng chạy một token: chúng đá nhau ra khỏi CoreIOT. Chỉ để một người chạy `bridge.py`. Bridge chạy rồi mà vẫn không có số: kiểm `COREIOT_TOKEN` trong `.env` và firewall Windows cho broker cổng 1883 |
| Không có `Room Empty`, quạt/đèn không tự đổi theo nhiệt độ | `room1-node-b` thiếu telemetry `fan_level`/`light_on`/`mode` hoặc thiếu 8 ngưỡng, nên rule chain đi nhánh **Failure**. Xem tab *Events* của node log `Bao thieu ...` trong rule chain. Chạy bridge + Node B ít nhất một lần và đặt đủ ngưỡng (mục 2, bước 3). Kiểm cả Node B đang `auto` và đã nối node vào Root Rule Chain (bước 5) |
| Bấm nút trên dashboard báo timeout | Node B (hoặc `fake_node_b.py`) hay bridge không chạy, hoặc đang có bridge thứ hai (xem dòng trên) |
