# Cấu hình CoreIOT (issue #32, #39, #46, #47)

Ghi lại cấu hình đã dựng trên **app.coreiot.io** ngày 11/11/2026, để người khác dựng lại hoặc sửa tiếp.
Key, method RPC và tên thiết bị theo [docs/CONTRACT.md](../../../docs/CONTRACT.md) mục 5, 6.2, 11.

- Nền tảng: CoreIOT = ThingsBoard **PE 4.2.0**, tài khoản Tenant administrator của Khoa.
- Từ W2 (#46) dashboard và cảnh báo chạy với thiết bị thật `room1-node-a`, `room1-node-b` sau Gateway `room1-gateway` (`bridge.py`). Device tạm `room1-test` (W1, `fake_telemetry.py`) vẫn giữ để thử nhanh không cần Gateway.

## 1. Device

| Tên | Profile | Label | ID |
|---|---|---|---|
| `room1-gateway` | `default`, **Is gateway** | Gateway Room 1 (`bridge.py`) | `f2266a90-e2e8-11f1-8cd8-29dc599bec29` |
| `room1-node-a` | `default` | do Gateway tự tạo (`v1/gateway/connect`) | `088afda0-e2e9-11f1-8cd8-29dc599bec29` |
| `room1-node-b` | `default` | do Gateway tự tạo | `08b9aec0-e2e9-11f1-8cd8-29dc599bec29` |
| `room1-test` | `default` | Room 1 test (du lieu gia, issue #32) | `a13b9e20-dd7f-11f1-9298-f795494fee72` |

- Credentials: **Access Token**. Token **không** ghi vào repo.
  - `room1-gateway`: dán vào `App/IOT/gateway/.env`, key `COREIOT_TOKEN` (xem README gateway). Ai chạy `bridge.py` thì xin token này trong nhóm (chỉ chạy **một** bridge một lúc).
  - `room1-test`: dán vào `App/IOT/coreiot/.env`, key `COREIOT_TEST_TOKEN`.
- `room1-node-a`, `room1-node-b` không có token riêng: Gateway gửi thay qua `v1/gateway/*` (CONTRACT 11).
- Server attribute `inactivityTimeout` = `60000` đã đặt cho `room1-gateway`, `room1-node-a`, `room1-node-b`, `room1-test`.
- Shared attribute 8 ngưỡng mặc định (CONTRACT 7) đã đặt trên `room1-node-b`. `bridge.py` lấy về lúc khởi động và khi đổi, đẩy xuống `room1/config` (retained).

## 2. Gửi dữ liệu giả

```powershell
cd App\IOT\coreiot
pip install paho-mqtt==1.6.1
copy .env.example .env      # dán token vào COREIOT_TEST_TOKEN
python fake_telemetry.py
```

- MQTT `app.coreiot.io:1883`, username = token, topic `v1/devices/me/telemetry`, 2 giây/lần.
- Bản tin có đủ 17 key của CONTRACT mục 5 (`ts`, `seq`, `temperature`, …, `uptime_s`).
- Script cũng nghe `v1/devices/me/rpc/request/+` và trả lời `setFan`, `setLight`, `setMode`, `getState` (dữ liệu trả giống CONTRACT 11.4). Nhờ vậy bấm nút trên dashboard là thấy trạng thái đổi, không bị timeout.
- Chạy được với `paho-mqtt` 1.6.1 (bản nhóm ghim) lẫn 2.x.

## 3. Dashboard `Room 1`

- ID: `d89e63c0-dd7f-11f1-9298-f795494fee72`
- Link: https://app.coreiot.io/dashboards/d89e63c0-dd7f-11f1-9298-f795494fee72
- File export: [`dashboard_room1.json`](dashboard_room1.json)
- Time window mặc định: Realtime, 10 phút gần nhất, không gộp (aggregation `NONE`). Biểu đồ dùng chung time window của dashboard.

### 3.1. Entity alias

Mọi widget lấy dữ liệu qua alias, không trỏ thẳng device.

| Alias | Kiểu | Trỏ tới |
|---|---|---|
| `sensor_node` | Entity name, DEVICE, `room1-node-a` | `room1-node-a` |
| `actuator_node` | Entity name, DEVICE, `room1-node-b` | `room1-node-b` |

- Alias tìm theo **tên** thiết bị (không theo ID), nên import dashboard sang tài khoản khác vẫn chạy nếu có thiết bị đúng tên.
- Muốn thử với `room1-test`: Edit → *Entity aliases* → đổi tên lọc thành `room1-test` → Save (đừng Save khi demo).

### 3.2. Widget

Lưới 24 cột. Vị trí ghi theo (hàng, cột, rộng × cao).

| Widget | Loại (FQN) | Alias | Key / RPC | Vị trí |
|---|---|---|---|---|
| Nhiệt độ | `system.cards.value_card` | sensor_node | `temperature`, °C, 1 số lẻ | 0, 0, 4×3 |
| Độ ẩm | `system.cards.value_card` | sensor_node | `humidity`, %, 1 số lẻ | 0, 4, 4×3 |
| Ánh sáng (ADC) | `system.cards.value_card` | sensor_node | `light` | 0, 8, 4×3 |
| Có người | `system.cards.value_card` | sensor_node | `presence` | 0, 12, 4×3 |
| Nhiệt độ và độ ẩm | `system.time_series_chart` | sensor_node | `temperature`, `humidity` | 3, 0, 16×6 |
| Mức quạt | `system.cards.value_card` | actuator_node | `fan_level` | 0, 16, 4×3 |
| Đèn | `system.cards.value_card` | actuator_node | `light_on` | 3, 16, 4×3 |
| Chế độ | `system.cards.value_card` | actuator_node | `mode` | 6, 16, 4×3 |
| Điểm bất thường | `system.cards.value_card` | sensor_node | `anomaly_score`, 3 số lẻ | 9, 16, 4×3 |
| Quạt 0 / 1 / 2 | `system.control_widgets.rpcbutton` | actuator_node | RPC `setFan`, params `0` / `1` / `2` | 9, 0/4/8, 4×2 |
| Đèn (setLight) | `system.single_switch` | actuator_node | RPC `setLight` `true`/`false` | 11, 0, 6×2 |
| Chế độ auto (setMode) | `system.single_switch` | actuator_node | RPC `setMode` `"auto"`/`"manual"` | 11, 6, 6×2 |
| Ngưỡng tự động | `system.input_widgets.update_multiple_attributes` | actuator_node | 8 shared attribute CONTRACT mục 7 | 13, 0, 24×5 |

Ghi chú:
- `anomaly_score` theo CONTRACT 11.2 thuộc `room1-node-a`, nên widget này dùng alias `sensor_node`.
- Nút quạt: RPC **two-way** (`oneWayElseTwoWay = false`), timeout 5000 ms, để thấy được trả lời hoặc lỗi timeout.
- Hai công tắc lấy **trạng thái ban đầu từ telemetry** (`light_on`; `mode` qua hàm `return data === 'auto';`), không gọi RPC `getState`. Cách này chạy được cả khi chưa có ai trả RPC. Bật là gửi giá trị đầu, tắt là gửi giá trị sau, timeout 5000 ms.
- Widget **Ngưỡng tự động** sửa shared attribute của `actuator_node` (sau này là `room1-node-b`), bấm *Lưu ngưỡng* là gửi cả 8 key (`updateAllValues`). Widget chỉ kiểm từng ô (bắt buộc, min/max), **không** kiểm ràng buộc `fan_off_1 < fan_on_1 <= fan_off_2 < fan_on_2`, `light_dark < light_bright`: Gateway kiểm (CONTRACT 7). Trên `room1-node-b` đã đặt sẵn giá trị mặc định.
- RuleChain sau này chỉ gửi `autoFan`/`autoLight`. Dashboard chỉ gửi `setFan`/`setLight`/`setMode` (CONTRACT 11.4).

## 4. Import lại dashboard

Dashboards → **+** → *Import dashboard* → chọn `dashboard_room1.json`. Alias tự tìm `room1-node-a`, `room1-node-b` theo tên, nên chạy `bridge.py` trước một lần để 2 thiết bị được tạo.

## 5. Cảnh báo T13: rule chain `Room1 Alarms`

- File export: [`rulechain_room1_alarms.json`](rulechain_room1_alarms.json). Import: Rule chains → **+** → *Import rule chain*.
- **Root Rule Chain** có thêm một node *Rule Chain* tên `Room1 Alarms`, nối từ *Message Type Switch* với 3 nhánh `Post telemetry`, `Activity Event`, `Inactivity Event`. Các nhánh cũ giữ nguyên. Import lại trên tài khoản khác thì phải tự thêm node này.
- Node đầu là switch TBEL: bỏ qua mọi thiết bị không có tên bắt đầu bằng `room1`, rồi chia ra các nhánh dưới.

| Điều kiện (CONTRACT 11.7) | Alarm type | Mức ThingsBoard | Tự xóa khi |
|---|---|---|---|
| `temperature` ≥ 35 | `High Temperature` | CRITICAL | `temperature` < 35 |
| `anomaly` = true | `Anomaly` | WARNING | `anomaly` = false |
| `event_level` = warning | `AI Event` | WARNING | không, phải Ack/Clear tay |
| Thiết bị không gửi > 1 phút | `Device Offline` | WARNING | có dữ liệu lại |
| `presence` = false (node-a), `fan_level` = 0, `light_on` = false (node-b) | `Room Empty` (trên `room1-node-b`) | INDETERMINATE (= Info) | `presence` = true |
| `fw_state` = `UPDATED` / `FAILED` | `OTA Success` / `OTA Failed` | INDETERMINATE (= Info) | không |

Ghi chú:
- ThingsBoard không có mức `INFO`, nên Info dùng `INDETERMINATE`.
- **Offline** dùng server attribute `inactivityTimeout` = `60000` (ms) của từng thiết bị (đã đặt, xem mục 1). Thêm thiết bị mới thì đặt ở Device → Attributes → Server attributes → **+**.
- **Room Empty** (#46): `presence` ở `room1-node-a`, quạt/đèn ở `room1-node-b` (CONTRACT 11.2). Switch đầu thấy bản tin có `presence` mà không có `fan_level` thì đi nhánh `presenceCheck`:
  1. *Change originator* `Sang room1-node-b`: nguồn **Entity**, DEVICE, tên `room1-node-b`.
  2. *Originator attributes* `Lay fan_level, light_on`: latest telemetry `fan_level`, `light_on` vào **metadata** (giá trị là chuỗi).
  3. Switch TBEL `Kiem tra phong trong`: `presence == false && parseInt(metadata.fan_level) == 0 && metadata.light_on == 'false'` → `roomEmpty`; `presence == true` → `roomOccupied`.

  Vì đã đổi originator nên alarm `Room Empty` nằm trên `room1-node-b` (thiết bị đã tắt quạt/đèn). Bản tin gộp kiểu `room1-test` vẫn đi nhánh cũ.
  Lúc vừa trống, Node B cần ~1 bản tin (2 s) mới báo `fan_level = 0`, nên alarm có thể trễ 2–4 s.
  `room1-node-b` phải có sẵn telemetry `fan_level`, `light_on` (chạy `bridge.py` + Node B ít nhất một lần). Thiếu thì node lấy trạng thái đi nhánh **Failure** sang node log `Bao thieu trang thai node-b`: Room Empty không tạo, không tự xóa được. Xem ở tab *Events* (debug Failure đã bật) của 2 node này.
- **OTA**: đang giả sử Gateway đổi state sang `fw_state` chuẩn ThingsBoard (`UPDATED`, `FAILED`). Chờ #29 chốt cách đổi rồi sửa nếu khác.
- Đã thử 13/11 trên `room1-test`: gửi 36.5 °C ra CRITICAL, về 30 °C tự xóa; dừng gửi 1 phút ra `Device Offline`.
- Đã thử 18/11 với `fake_node_b.py` + `bridge.py`: High Temperature trên `room1-node-a`, Room Empty trên `room1-node-b` tạo và tự xóa; tắt `bridge.py` > 1 phút ra `Device Offline` cho cả 3 thiết bị.

### 5.1. Notification (T13)

Notification center:
- Template `Room1 canh bao quan trong` (loại Alarm, kênh **Web**): tiêu đề `Room 1: ${alarmType} (${alarmSeverity})`, nội dung có tên thiết bị.
- Rule `Room1: Nhiet do cao / Mat ket noi`: trigger **Alarm**, loại `High Temperature`, `Device Offline`, khi **Created**; gửi tới *Tenant administrators*.
- Xem ở chuông thông báo góc trên bên phải. Muốn thêm email/Telegram: sửa template, bật thêm kênh (cần cấu hình mail server / bot của tenant).
- Rule mặc định `New alarm` của CoreIOT vẫn bật (báo mọi alarm), nên High Temperature có 2 thông báo. Tắt rule đó nếu thấy thừa.

### 5.2. Tự động quạt/đèn T12: rule chain `Room1 Auto Control` (#47)

- File export: [`rulechain_room1_auto.json`](rulechain_room1_auto.json). Import như mục 5.
- **Root Rule Chain** có thêm node *Rule Chain* `Room1 Auto Control`, nối từ *Message Type Switch* nhánh `Post telemetry` (song song với `Room1 Alarms`). Import trên tài khoản khác thì phải tự thêm node này.
- Các node:
  1. Filter `Tu room1-node-a`: chỉ lấy telemetry của `room1-node-a` có `temperature` hoặc `light`.
  2. *Change originator* `Sang room1-node-b`: nguồn **Entity**, DEVICE, tên `room1-node-b`, vì ngưỡng, trạng thái và RPC đều ở Node B.
  3. *Originator attributes*: 8 shared attribute ngưỡng (vào metadata với tiền tố `shared_`) + latest telemetry `fan_level`, `light_on`, `mode`. Giá trị trong metadata là **chuỗi**, nên script dùng `parseFloat`/`parseInt`.
  4. Script `Tinh muc quat, den` (CONTRACT 7, 13):
     - Quạt có hysteresis: đang 0 thì `t ≥ fan_on_2` lên 2, `t ≥ fan_on_1` lên 1; đang 1 thì `t ≥ fan_on_2` lên 2, `t ≤ fan_off_1` về 0; đang 2 thì `t ≤ fan_off_1` về 0, `t ≤ fan_off_2` về 1.
     - Đèn: `presence` = true và `light < light_dark` thì bật; `light > light_bright` thì tắt; còn lại giữ nguyên.
     - `presence` = false thì không tính quạt: Node B ưu tiên tắt hết (CONTRACT 13 mục 1), nếu rule cứ gửi `autoFan` thì sẽ lặp RPC mỗi 2 s.
  5. Switch `Can gui RPC?`: `mode` khác `auto` thì dừng; chỉ đi nhánh `autoFan` / `autoLight` khi giá trị mới **khác** giá trị hiện tại.
  6. Script tạo `{method: 'autoFan', params: <int>}` hoặc `{method: 'autoLight', params: <bool>}` → *RPC call request* (timeout 10 s) tới `room1-node-b`. Không bao giờ gửi `setFan`/`setLight` (CONTRACT 11.4).
- Đổi ngưỡng trên widget `Ngưỡng tự động` có tác dụng ngay ở bản tin kế tiếp, vì rule đọc lại shared attribute mỗi lần.
- Đã thử 18/11 với `fake_node_b.py` + `bridge.py` (bơm thêm bản tin vào `room1/telemetry` bằng paho):
  - 34 °C, đang quạt 1, auto → `autoFan 2`, trả `ok: true`.
  - Bấm `setFan 0` (chuyển `manual`), bơm 34 °C → không có `autoFan` nào.
  - `fan_on_1` = 31, bơm 30.5 °C → không gửi; đổi `fan_on_1` = 30 trên widget, bơm 30.5 °C → `autoFan 1`, `ok: true`.
  - `presence` = false, 34 °C → không gửi.
- Lưu ý:
  - Hai bản tin node-a sát nhau trước khi Node B kịp báo `fan_level` mới có thể sinh 2 RPC giống nhau. Vô hại vì `autoFan`/`autoLight` gửi cùng giá trị.
  - `fake_node_b.py` sinh `light` ngẫu nhiên 1200–2600 mỗi 2 s nên đèn bật/tắt liên tục quanh 1500/2500. Cảm biến thật không bị vậy.

## 6. Việc còn lại

- [x] Đổi 2 alias sang `room1-node-a` / `room1-node-b`, export lại JSON (#46).
- [x] `inactivityTimeout` = 60000 cho thiết bị thật; nhánh Room Empty lấy trạng thái từ `room1-node-b` (#46).
- [x] 8 ngưỡng mặc định trên `room1-node-b` (#46).
- [x] Notification cho High Temperature, Device Offline (#46).
- [x] RuleChain T12 (`autoFan`, `autoLight`) – #47, xem mục 5.2.
- [ ] Xóa device `room1-test` khi không cần nữa.
