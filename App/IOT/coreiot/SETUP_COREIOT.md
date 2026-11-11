# Cấu hình CoreIOT (issue #32)

Ghi lại cấu hình đã dựng trên **app.coreiot.io** ngày 11/11/2026, để người khác dựng lại hoặc sửa tiếp.
Key, method RPC và tên thiết bị theo [docs/CONTRACT.md](../../../docs/CONTRACT.md) mục 5, 6.2, 11.

- Nền tảng: CoreIOT = ThingsBoard **PE 4.2.0**, tài khoản Tenant administrator của Khoa.
- Tuần W1 chưa có Gateway, nên mọi widget đọc từ một device tạm `room1-test`, dữ liệu do `fake_telemetry.py` gửi.

## 1. Device

| Tên | Profile | Label | ID |
|---|---|---|---|
| `room1-test` | `default` | Room 1 test (du lieu gia, issue #32) | `a13b9e20-dd7f-11f1-9298-f795494fee72` |

- Credentials: **Access Token** (mặc định của ThingsBoard). Lấy ở Entities → Devices → `room1-test` → *Copy access token*.
- Token **không** ghi vào repo. Dán vào `App/IOT/coreiot/.env` (chép từ `.env.example`), key `COREIOT_TEST_TOKEN`.
- Khi Gateway (#28) chạy, `room1-node-a` và `room1-node-b` sẽ do Gateway tự tạo. Lúc đó có thể xóa `room1-test`.

## 2. Gửi dữ liệu giả

```powershell
cd App\IOT\coreiot
pip install paho-mqtt python-dotenv
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

Mọi widget lấy dữ liệu qua alias, không trỏ thẳng device. Khi có Gateway chỉ cần đổi alias, không phải làm lại widget.

| Alias | Kiểu | Hiện trỏ tới | Khi có Gateway (#28) đổi sang |
|---|---|---|---|
| `sensor_node` | Single entity | `room1-test` | `room1-node-a` |
| `actuator_node` | Single entity | `room1-test` | `room1-node-b` |

Cách đổi: mở dashboard → Edit (bút chì) → biểu tượng *Entity aliases* → sửa từng alias → Save.

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

Ghi chú:
- `anomaly_score` theo CONTRACT 11.2 thuộc `room1-node-a`, nên widget này dùng alias `sensor_node`.
- Nút quạt: RPC **two-way** (`oneWayElseTwoWay = false`), timeout 5000 ms, để thấy được trả lời hoặc lỗi timeout.
- Hai công tắc lấy **trạng thái ban đầu từ telemetry** (`light_on`; `mode` qua hàm `return data === 'auto';`), không gọi RPC `getState`. Cách này chạy được cả khi chưa có ai trả RPC. Bật là gửi giá trị đầu, tắt là gửi giá trị sau, timeout 5000 ms.
- RuleChain sau này chỉ gửi `autoFan`/`autoLight`. Dashboard chỉ gửi `setFan`/`setLight`/`setMode` (CONTRACT 11.4).

## 4. Import lại dashboard

Dashboards → **+** → *Import dashboard* → chọn `dashboard_room1.json` → ở bước alias chọn device cho `sensor_node` và `actuator_node`.

## 5. Việc còn lại (tuần sau)

- [ ] Khi #28 xong: đổi 2 alias sang `room1-node-a` / `room1-node-b`, export lại JSON.
- [ ] Widget sửa ngưỡng (shared attribute của `room1-node-b`, key CONTRACT mục 7).
- [ ] RuleChain T12 (`autoFan`, `autoLight`) và cảnh báo T13 (CONTRACT 11.7).
- [ ] Xóa device `room1-test` khi không cần nữa.
