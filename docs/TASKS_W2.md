# TASKS tuần W2 (16/11 – CN 22/11/2026): Nối các lớp

> Hạn chung: **20:00 CN 22/11**, họp 15 phút buổi tối. Issue nào chưa xong lúc đó tính là trễ.
> Việc W1 còn mở có hạn riêng (bảng mục 2), **dồn vào tuần này**, không dời hạn W2.
> Quy trình Git, cài Mosquitto/Python, lệnh test nhanh: giữ nguyên như [`TASKS_W1.md`](TASKS_W1.md) mục 1 và 3.
> Đọc [`CONTRACT.md`](CONTRACT.md) **trước khi code**. Topic, key, lệnh phải khớp file đó. Thấy CONTRACT thiếu thì hỏi nhóm, đừng tự đặt key mới.

Mục tiêu tuần (kế hoạch mục 15): Node B thật thay `fake_node_b.py`, CoreIOT chạy với 2 thiết bị thật, RuleChain tự bật quạt/đèn, OTA phía Gateway chạy với Node B giả, bắt đầu thu dữ liệu TinyML.

---

## 1. Trước khi làm

```bash
git checkout main
git pull origin main          # có #39 (cảnh báo T13) và #40 (bridge.py) mới merge
git checkout -b <số issue>-<tên ngắn>
```

Từ tuần này mọi người test với **`bridge.py` thật** (đã merge), không chỉ `fake_node_b.py`:
```powershell
& "C:\Program Files\mosquitto\mosquitto.exe" -c App\IOT\gateway\mosquitto.conf -v   # cửa sổ 1
python App/IOT/gateway/fake_node_b.py                                                # cửa sổ 2
python App/IOT/gateway/bridge.py                                                     # cửa sổ 3 (cần .env có COREIOT_TOKEN)
```
Chỉ chạy **một** `bridge.py` một lúc (2 cái cùng client id sẽ đá nhau).

---

## 2. Phân chia W2

Số issue mới (#42 trở đi) là số dự kiến theo thứ tự tạo. Nếu lệch thì đặt tên nhánh theo **số thật** trên GitHub.

| # | Việc | Người | Giờ | Hạn | Chờ |
|---|---|---|---|---|---|
| 29 | (W1) Kiểm tra OTA của CoreIOT cho thiết bị sau Gateway | Tuấn | 0.5–1h | **T3 17/11** | — |
| 30 | (W1) Train lại model TinyML | Phú | 4–5h | **T4 18/11** | — |
| 31 | (W1) Khung giao diện Streamlit | Phú | 4–5h | **T4 18/11** | — |
| 42 | Node B: nhận ESP-NOW + MQTT + điều khiển quạt/đèn | Minh | 10–12h | CN 22/11 | board |
| 43 | Node A nhận `PKT_CONFIG` | Minh | 2h | CN 22/11 | #42 |
| 44 | `fake_node_b`: chế độ mất Node A + giả OTA | Tuấn | 3–4h | **T5 19/11** | — |
| 45 | OTA phía Gateway | Tuấn | 6–8h | CN 22/11 | #29, #44 |
| 46 | CoreIOT chuyển sang thiết bị thật + notification | Khoa | 3–4h | CN 22/11 | — |
| 47 | RuleChain T12: tự bật tắt quạt, đèn | Khoa | 5–6h | CN 22/11 | #46 |
| 48 | Thu dữ liệu TinyML trên giao diện | Phú | 3h | CN 22/11 | #31 |

Tổng giờ: Minh 12–14h, Tuấn 10–13h, Khoa 8–10h, Phú 11–13h (gồm 2 issue W1).

**Thứ tự quan trọng:**
- Tuấn: #29 (ngày đầu) → #44 → #45. Kết quả #29 quyết định #45 lấy tín hiệu OTA từ CoreIOT hay chỉ phát hành bằng tay.
- Khoa: #46 trước, vì #47 cần alias và ngưỡng trên `room1-node-b`.
- Phú: #30, #31 xong trước T4, rồi mới #48 (làm trên `app.py` của #31).
- Nếu #31 quá 20:00 T4 18/11 mà chưa có PR: chuyển #31 và #48 cho Khoa (cùng làm trên `app.py`). Phú giữ #30.

---

## 3. Minh (TV1 – Lead + Firmware)

### #42 Node B: nhận ESP-NOW + MQTT + điều khiển quạt/đèn
Nhánh `42-node-b-core`. Mục tiêu: **tắt `fake_node_b.py`, cắm Node B thật, các phần khác không sửa gì** (CONTRACT mục 1).

**File** (template + TODO + testcase Unity như #26):
| File | Nội dung |
|---|---|
| `node_b/src/espnow_rx.{h,cpp}` | Trả `PKT_PAIR_RESP` (kèm channel WiFi đang dùng), nhận `PKT_DATA` vào queue (callback chỉ kiểm `len`, `magic`, `version`) |
| `node_b/lib/node_b_logic/` | Logic thuần, test được trên PC: theo dõi `seq` (trùng / nhảy cóc / Node A khởi động lại), `espnow_loss`, nhiệt độ trung bình 2 lần đo, kiểm ràng buộc ngưỡng, kiểm `params` 6 method, hết `manual_timeout_s` |
| `node_b/src/net.{h,cpp}` | WiFi STA, `WiFi.setSleep(false)`, watchdog tự nối lại sau 10 s; PubSubClient `setBufferSize(1024)`, Last Will `room1/status`; subscribe `room1/cmd`, `room1/config`, `room1/gateway/status` |
| `node_b/src/control.{h,cpp}` | `control.h` **đúng** CONTRACT mục 12, có mutex. Quạt PWM chân 8, đèn RGB chân 10 (dùng lại code lab 10/11) |
| `node_b/src/cmd.{h,cpp}` | Parse `room1/cmd`, 6 method mục 6.2, trả `room1/cmd/resp` đúng 6.3 (kể cả `bad_json` với `id: null`) |
| `node_b/src/config_store.{h,cpp}` | `room1/config`: kiểm ràng buộc, sai thì giữ bộ cũ, lưu NVS (`Preferences`), nạp lại lúc khởi động |
| `node_b/src/main.cpp` | Tạo task, nối queue |

Luật áp dụng tuần này (CONTRACT 13): `presence = false` thì tắt hết, xóa manual, về `auto`; lệnh tay thắng trong `manual_timeout_s`. **Chưa làm W2:** LCD, ghép webserver của Khoa (W3), OTA trên Node B (W3), chế độ `offline` tự chạy luật (W4).

**Kiểm:** chạy Node A + Node B + `bridge.py`, `mosquitto_sub -t "room1/#" -v`, gõ lại 5 ca của #27:
| Gửi | Phải thấy |
|---|---|
| `setFan` 2 | resp `ok: true`, `mode: "manual"`, **quạt thật quay mức 2** |
| `autoFan` 1 khi đang manual | `error: "ignored_not_auto"` |
| `setFan` 5 | `error: "bad_params"` |
| `abc` | `error: "bad_json"`, `id: null` |
| Rút nguồn Node B | `room1/status` chuyển `{"state":"offline"}` |

**Xong khi:** PR có: `pio test -e native` PASS; ảnh CoreIOT `room1-node-a`, `room1-node-b` có **số thật**; ảnh/video bấm `setFan 2` trên dashboard của Khoa, quạt quay; log 5 ca trên.

### #43 Node A nhận `PKT_CONFIG`
Nhánh `43-node-a-config` (tạo từ `main` sau khi #42 merge).
- Node B gửi `PktConfig` (CONTRACT mục 3) **sau mỗi lần ghép cặp** và **khi `empty_timeout_s` đổi**.
- Node A chỉ nhận `PKT_CONFIG` từ đúng MAC Node B đã ghép, kiểm `len`/`magic`/`version`, cập nhật thời gian chờ phòng trống. Mất điện thì về 60 s cho tới lần ghép sau.

**Xong khi:** đổi `empty_timeout_s` trên widget ngưỡng của Khoa → Node A in giá trị mới, phòng trống sau đúng thời gian mới. Có log.

---

## 4. Tuấn (TV2 – Gateway)

### #29 (W1) Kiểm tra OTA – hạn **T3 17/11**
Làm theo comment của Minh trong issue: tạo package `.bin` bất kỳ bản `1.0.1` đúng profile của `room1-node-b`, gán **trực tiếp** cho `room1-node-b`, xem log `bridge.py` có `Nhận tin từ CoreIOT [v1/gateway/attributes]` kèm `fw_title`, `fw_version` không.
**Xong khi:** comment trong issue có ảnh màn hình gán firmware + log bridge. Ghi rõ **được** hay **không được**.

### #44 `fake_node_b`: chế độ mất Node A + giả OTA – hạn **T5 19/11**
Nhánh `44-fake-node-b-modes`. Sửa `fake_node_b.py`, vẫn phải **đúng CONTRACT**.

1. **Mất Node A:** tham số `--node-a-off-after N` (giây, `0` = mất ngay từ đầu). Sau N giây: telemetry **5 giây/lần**, `node_a_online: false`, **bỏ hẳn** các key cảm biến, đúng ví dụ "khi mất Node A" ở CONTRACT mục 5.
2. **Giả OTA** (CONTRACT mục 9): nghe `room1/ota`.
   - `version` bằng `fw_version` đang chạy thì bỏ qua.
   - Tải **thật** file ở `url` (urllib), tính `sha256` trong lúc tải, gửi `room1/ota/status` `downloading` mỗi 10%.
   - Lỗi tải → `failed` + `download_failed`. Sai sha256 → `failed` + `checksum_mismatch`.
   - Đúng → `verifying` → `rebooting` → ngắt MQTT 2–3 giây, đổi `fw_version` = bản mới, nối lại, gửi `room1/status` online kèm `fw_version` mới → `success`.
   - Tham số `--ota-corrupt`: đổi 1 byte trước khi tính sha256, để test ca `checksum_mismatch`.
   - **Chạy OTA trong thread riêng**, không tải file trong `on_message` (sẽ chặn thread mạng của paho, mất telemetry).
3. Sửa `README.md` mục 3.2: `Ran 6 tests` (hiện ghi 4).

**Xong khi:** PR có ảnh `mosquitto_sub` thấy telemetry đổi sang dạng mất Node A và `bridge.py` log `room1-node-a -> disconnect`; test cũ vẫn PASS.

### #45 OTA phía Gateway
Nhánh `45-gateway-ota`. Phần nạp thật trên Node B là việc của Minh ở W3; tuần này test bằng `fake_node_b` (#44).

**File:**
| File | Nội dung |
|---|---|
| `gateway/ota.py` | Hàm thuần + HTTP server (bảng dưới) |
| `gateway/ota_publish.py` | Lệnh phát hành tay: `python App/IOT/gateway/ota_publish.py fw/node_b_1.0.1.bin 1.0.1` → publish `room1/ota` |
| `gateway/bridge.py` | Khởi động HTTP server; nghe `room1/ota/status` → gửi `fw_state` lên CoreIOT; (nếu #29 được) nhận `fw_version` từ CoreIOT → tự phát hành |
| `gateway/test_ota.py` | Unit test |
| `gateway/fw/` | Chứa file `.bin`. Thêm `App/IOT/gateway/fw/*.bin` vào `.gitignore` |
| `gateway/.env.example` | Thêm `GATEWAY_IP=` (IP laptop trong hotspot), `OTA_PORT=8000` |

**Hàm trong `ota.py`:**
| Hàm | Làm gì |
|---|---|
| `sha256_file(path) -> str` | sha256 hex, chữ thường, 64 ký tự |
| `make_ota_msg(path, version, base_url) -> dict` | `{"version", "url", "size", "sha256"}` đúng CONTRACT mục 9. `url` dùng **IP**, không dùng tên miền |
| `map_ota_status(status: dict) -> dict` | Đổi `room1/ota/status` sang telemetry CoreIOT (bảng dưới) |
| `start_http_server(fw_dir, port)` | `ThreadingHTTPServer` phát file trong `fw_dir`, chạy thread riêng |

**Đề xuất đổi state** (CoreIOT/ThingsBoard dùng `fw_state` với các giá trị DOWNLOADING, DOWNLOADED, VERIFIED, UPDATING, UPDATED, FAILED; phần cảnh báo OTA của Khoa đang đọc `fw_state` = `UPDATED`/`FAILED`):
| `room1/ota/status` | Gửi lên `room1-node-b` |
|---|---|
| `downloading` | `fw_state: "DOWNLOADING"` |
| `verifying` | `fw_state: "DOWNLOADED"` |
| `rebooting` | `fw_state: "UPDATING"` |
| `success` | `fw_state: "UPDATED"` |
| `failed` | `fw_state: "FAILED"` + `fw_error` = mã lỗi |

Gửi qua `v1/gateway/telemetry` của `room1-node-b` (giống cách bridge tách telemetry). Mở trang OTA / tab Latest telemetry của `room1-node-b` trên CoreIOT để kiểm tên key có khớp không; khác thì sửa theo CoreIOT. Chốt xong thì **ghi bảng đổi state vào CONTRACT mục 9** (PR riêng hoặc cùng PR, ghi mục 14).

**Test `test_ota.py` tối thiểu:** `sha256` của `b"abc"` ra `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`; `map_ota_status` đủ 5 state; `make_ota_msg` đủ 4 key, `size` đúng số byte.

Lưu ý Windows: lần đầu chạy HTTP server, Firewall hỏi thì cho **Private** và **Public** (Node B trong hotspot phải tải được).

**Xong khi:** PR có: `test_ota.py` PASS; log `ota_publish.py` → fake tải → đủ chuỗi state → ảnh CoreIOT `room1-node-b` thấy `fw_state: UPDATED` và `fw_version` mới; ca `--ota-corrupt` ra `FAILED` + `checksum_mismatch`. Nếu #29 được: thêm ảnh gán firmware trên CoreIOT tự kích hoạt OTA.

---

## 5. Khoa (TV4 – CoreIOT + Webserver)

### #46 CoreIOT chuyển sang thiết bị thật + notification
Nhánh `46-coreiot-real-devices`. Đây là mục 6 trong `App/IOT/coreiot/SETUP_COREIOT.md` của Khoa, cộng notification cho T13.

1. Đổi alias `sensor_node` → `room1-node-a`, `actuator_node` → `room1-node-b` (chạy `fake_node_b` + `bridge.py` để 2 thiết bị có dữ liệu).
2. Server attribute `inactivityTimeout` = `60000` cho `room1-node-a`, `room1-node-b`, `room1-gateway`.
3. Đặt 8 ngưỡng mặc định (CONTRACT mục 7) làm shared attribute trên `room1-node-b`.
4. Sửa nhánh **Room Empty** trong `Room1 Alarms`: `presence` ở `room1-node-a`, `fan_level`/`light_on` ở `room1-node-b`. Gợi ý: node *Change originator* sang `room1-node-b` rồi *Originator attributes* lấy telemetry mới nhất; chọn cách khác cũng được, ghi lại trong SETUP_COREIOT.
5. **Notification (T13):** Notification center → rule gửi thông báo khi có alarm `High Temperature` và `Device Offline` (ít nhất thông báo trên web CoreIOT).
6. Export lại `dashboard_room1.json`, `rulechain_room1_alarms.json`; cập nhật SETUP_COREIOT.

**Xong khi:** PR có: ảnh dashboard chạy với `room1-node-a`/`room1-node-b`; ảnh notification; **tắt bật lại `bridge.py`, `mosquitto_sub -t room1/config -v` thấy đủ 8 ngưỡng** (kiểm luôn phần lấy ngưỡng lúc khởi động của bridge #40).

### #47 RuleChain T12: tự bật tắt quạt, đèn
Nhánh `47-coreiot-rulechain-t12`. CONTRACT 7, 11.4, 13. RuleChain **chỉ gửi `autoFan`, `autoLight`**, không bao giờ `setFan`/`setLight`.

Luồng gợi ý (theo slide thầy: Filter → Change originator → RPC tới thiết bị khác):
1. Telemetry của `room1-node-a` có `temperature` hoặc `light`.
2. Change originator sang `room1-node-b`.
3. Lấy shared attribute 8 ngưỡng và telemetry mới nhất `fan_level`, `light_on`, `mode` của `room1-node-b`. **Giá trị lấy vào metadata là chuỗi**, phải `parseFloat`/`parseInt` trước khi so sánh.
4. `mode` khác `"auto"` thì dừng (Node B cũng bỏ, nhưng đừng gửi thừa).
5. Tính mức quạt có hysteresis (f = `fan_level` hiện tại, t = `temperature`):
   ```
   next = f
   f == 0:  t >= fan_on_2 -> 2;  t >= fan_on_1 -> 1
   f == 1:  t >= fan_on_2 -> 2;  t <= fan_off_1 -> 0
   f == 2:  t <= fan_off_1 -> 0; t <= fan_off_2 -> 1
   ```
   Đèn: `presence` và `light < light_dark` → bật; `light > light_bright` → tắt; còn lại giữ nguyên. Phòng trống thì Node B tự tắt hết (CONTRACT 13), RuleChain không cần lo.
6. **Chỉ gửi RPC khi giá trị mới khác giá trị hiện tại** (telemetry về 2 giây/lần, không chặn sẽ gửi RPC liên tục). RPC `autoFan` params int, `autoLight` params bool.

**Kiểm** (với `fake_node_b` + `bridge.py`, xem log bridge):
| Tình huống | Phải thấy |
|---|---|
| `mode` auto, nhiệt lên ≥ `fan_on_2` | bridge log RPC `autoFan` 2, resp `ok: true` |
| Bấm `setFan` trên dashboard (vào manual) | không còn RPC `autoFan`; nếu có thì resp `ignored_not_auto` |
| Đổi `fan_on_1` trên widget ngưỡng | luật dùng ngưỡng mới ngay |

**Xong khi:** PR có ảnh RuleChain, log bridge 3 tình huống trên, file export JSON, SETUP_COREIOT cập nhật.

---

## 6. Phú (TV3 – AI + giao diện)

### #30, #31 (W1) – hạn **20:00 T4 18/11**
Yêu cầu giữ nguyên như `TASKS_W1.md` mục 6. Kẹt chỗ nào thì nhắn nhóm ngay trong ngày, đừng để tới hạn.

### #48 Thu dữ liệu TinyML trên giao diện
Nhánh `48-ui-data-collect` (tạo từ `main` sau khi #31 merge). Theo mục 10 tài liệu dự án: bật công tắc "đang tạo bất thường" rồi hơ nóng hoặc thổi hơi vào cảm biến, mẫu trong lúc đó nhãn 1, còn lại nhãn 0.

Thêm vào `App/IOT/gateway/ui/app.py`:
1. Nút **Bắt đầu ghi / Dừng ghi** và công tắc **Đang tạo bất thường**.
2. Ghi mỗi bản `room1/telemetry` vào `App/IOT/ai/data/raw/<YYYYMMDD_HHMM>.csv`, mỗi dòng `temperature,humidity,label`, **không có dòng tiêu đề**, giống hệt `sensor_data.csv` của thầy (script train đọc bằng `pd.read_csv(..., names=["temp", "humidity", "label"])`).
3. Bỏ bản tin không có `temperature`/`humidity` (Node A mất). Bỏ bản tin **trùng `seq`** với dòng vừa ghi (Node B gửi thêm telemetry mỗi khi quạt/đèn đổi, cùng số đo).
4. Hiện số mẫu nhãn 0 và nhãn 1 đã ghi.
5. Lưu ý Streamlit: callback MQTT chạy ở thread khác, **không đọc được `st.session_state`**. Để cờ "đang ghi" và nhãn trong dict `data` của `get_mqtt()` (`@st.cache_resource`), callback đọc từ đó.

Tuần này test với `fake_node_b` (dữ liệu giả, **không commit** CSV giả). Thu thật khi Node B thật chạy (cuối W2 / W3).

**Xong khi:** PR có video/ảnh: bật ghi, gạt công tắc, số mẫu nhãn 1 tăng; 10 dòng đầu file CSV; chạy `pd.read_csv(file, names=["temp", "humidity", "label"])` đọc được.

---

## 7. Issue cần tạo (Minh)

Tạo milestone **W2** trước (repo mới có W1), hạn CN 22/11. Nội dung mỗi issue: `Chi tiết: docs/TASKS_W2.md mục <số>`.
| Tiêu đề | Người | Label | Mục |
|---|---|---|---|
| Node B: nhận ESP-NOW + MQTT + điều khiển quạt/đèn | Minh | firmware | 3 |
| Node A nhận PKT_CONFIG | Minh | firmware | 3 |
| fake_node_b: chế độ mất Node A + giả OTA | Tuấn | gateway | 4 |
| OTA phía Gateway | Tuấn | gateway | 4 |
| CoreIOT chuyển sang thiết bị thật + notification | Khoa | coreiot | 5 |
| RuleChain T12: tự bật tắt quạt, đèn | Khoa | coreiot | 5 |
| Thu dữ liệu TinyML trên giao diện | Phú | ai-ui | 6 |

#29, #30, #31 giữ milestone W1, ghi hạn mới bằng comment trong issue.
