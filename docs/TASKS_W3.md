# TASKS tuần W3 (23/11 - CN 29/11/2026): Tính năng + hoàn thiện

> TinyML trên Node A và chế độ offline Node B để W4 (mục 9). Phần gửi bù khi mất internet (T15) và script đo đạc kéo lên tuần này vì không cần board.
> **Hạn mở PR: 20:00 T4 25/11.** Lab T5 26/11 - T7 28/11 chỉ để nạp và test board, không code mới trên lab.
> **Hạn minh chứng + merge: 20:00 CN 29/11.** Issue có ghi "Cần lên lab: có" chỉ merge khi có minh chứng board thật.
> Quy trình Git, cài Mosquitto/Python: giữ như [`TASKS_W1.md`](TASKS_W1.md) mục 1 và 3. Cách chạy cả hệ thống: [`HUONG_DAN_CHAY_HE_THONG.md`](HUONG_DAN_CHAY_HE_THONG.md).
> Đọc [`CONTRACT.md`](CONTRACT.md) **trước khi code**. Cần thêm key/topic thì sửa CONTRACT trước và báo cả nhóm.

Mục tiêu tuần (kế hoạch mục 15): webserver và LCD trên Node B, OTA thật từ CoreIOT xuống Node B, AI trên Gateway gửi sự kiện lên CoreIOT, giao diện có nhật ký sự kiện, bridge gửi bù khi mất internet.

---

## 1. Trước khi làm

```bash
git checkout main
git pull origin main
git checkout -b <số issue>-<tên ngắn>
```

Không cần board thì test bằng `fake_node_b.py` như W2. Cần board thì **viết và build xong ở nhà** (`pio run`), lên lab chỉ nạp và test.

**Node B có 3 người cùng sửa** (Minh: OTA, offline; Khoa: webserver, LCD). Để tránh conflict:
- Mỗi việc nằm trong file riêng (`ota.cpp`, `web.cpp`, `lcd.cpp`...). Trong `main.cpp` chỉ thêm dòng gọi hàm khởi động / tạo task.
- Không sửa `lib/node_b_logic/`, `control.cpp`, `net.cpp` nếu issue không ghi. Cần thì nhắn Minh.
- PR nào merge sau thì người đó `git pull origin main` vào nhánh mình, sửa conflict, build lại.

---

## 2. Phân chia W3

Số issue mới (#58 trở đi) là số dự kiến theo thứ tự tạo. Lệch thì đặt tên nhánh theo **số thật** trên GitHub.

| # | Việc | Người | Giờ | Lên lab | Hạn PR | Hạn test board |
|---|---|---|---|---|---|---|
| 45 | (W2) OTA phía Gateway: sửa PR #56 theo comment | Tuấn | 1-2h | không | như comment PR | - |
| 48 | (W2) Thu dữ liệu TinyML trên giao diện: sửa PR #57 | Phú | 1-2h | không | như comment PR | - |
| 30 | (W1) Model TinyML: PR #55, minh chứng chạy trên Node A | Minh | 1h | có | đã mở | CN 29/11 |
| 58 | OTA trên Node B | Minh | 8-10h | có | T4 25/11 | CN 29/11 |
| 59 | Webserver trong Node B (`task_web`) | Khoa | 4-5h | có | T4 25/11 | CN 29/11 |
| 60 | LCD 16x2 trên Node B | Khoa | 2-3h | có | T4 25/11 | CN 29/11 |
| 61 | Dashboard: widget OTA + nhật ký sự kiện AI | Khoa | 2h | không | T4 25/11 | - |
| 62 | AI trên Gateway: 3 sự kiện theo luật (T09) | Phú | 4-5h | không | T4 25/11 | - |
| 63 | Giao diện: nhật ký sự kiện + trạng thái kết nối (T10) | Phú | 2-3h | không | T4 25/11 | - |
| 64 | Bridge lưu tạm khi mất internet, gửi bù (T15) | Tuấn | 4-5h | không | T4 25/11 | - |
| 65 | Test OTA thật + đo thời gian OTA | Tuấn | 1-2h | có | - | CN 29/11 |
| 66 | AI trên Gateway: Isolation Forest (T09) | Tuấn | 3-4h | không | T4 25/11 | - |
| 67 | Script đo đạc chương 4 | Tuấn | 3h | không | T4 25/11 | - |
| 68 | Hướng dẫn chạy phần mình | cả nhóm | 1h/người | không | CN 29/11 | - |

Hạn là **20:00** ngày ghi. Tổng giờ: Minh 9-11h, Khoa 9-11h, Phú 7-10h, Tuấn 13-17h.

**Lịch lab đề xuất** (ai lên buổi nào thì nhắn nhóm trước tối T4):
| Buổi | Việc |
|---|---|
| T5 26/11 | Minh #30 (model trên Node A), #58 (Node B); Khoa #59, #60 |
| T6 27/11 | Tuấn + Minh #65 (OTA thật từ CoreIOT); thu thêm dữ liệu nhãn 0 chạy nền |
| T7 28/11 | Test lại các mục chưa PASS; chạy thử cả hệ thống theo kịch bản mục 5 tài liệu dự án, quay từng đoạn đã chạy |

Trước khi rút board mỗi buổi: rà lại bảng trên, mục nào chưa PASS thì ghi lại.

---

## 3. Minh (TV1 - Lead + Firmware)

### #58 OTA trên Node B (T14)
Nhánh `58-node-b-ota`. CONTRACT mục 9. Cần lên lab: **có**.
- `node_b/src/ota.{h,cpp}`: nghe `room1/ota`; `version` trùng `FW_VERSION` thì bỏ; tải `url` bằng HTTP, tính sha256 trong lúc tải, ghi vào phân vùng OTA còn trống (`Update`), gửi `room1/ota/status` `downloading` mỗi 10%, `verifying`, `rebooting`.
- Lỗi: `download_failed`, `checksum_mismatch`, `write_failed`. Sai sha256 thì **không** đổi phân vùng khởi động.
- Sau khởi động lại: nối được WiFi + MQTT trong 60 s thì đánh dấu bản mới tốt, gửi `success` + `room1/status` có `fw_version` mới; không thì quay về bản cũ, lần sau gửi `failed` + `rolled_back`.
- OTA chạy trong task riêng, không tải file trong callback MQTT.

**Xong khi:** PR có log Node B 1.0.0 -> 1.0.1 qua `ota_publish.py` của #45, log ca `checksum_mismatch` (sửa 1 ký tự sha256 trong lệnh), ảnh CoreIOT `room1-node-b` có `fw_version` mới.

---

## 4. Tuấn (TV2 - Gateway)

### #45 (W2) - sửa PR #56
Làm theo comment trong PR #56. Đây là điều kiện để test #58 và #65.

### #64 Bridge lưu tạm khi mất internet, gửi bù (T15)
Nhánh `64-bridge-store-forward`. Cần lên lab: **không** (test bằng `fake_node_b.py`). Tài liệu dự án mục 9: "Mất internet thì lưu tạm, có mạng lại gửi bù kèm thời điểm gốc".
- Mất kết nối CoreIOT (`on_disconnect` của cloud client): telemetry đã tách (CONTRACT 11.2) cất vào hàng đợi, **giữ `ts` gốc**. Giới hạn số bản (vd 1800 bản = 1 giờ với 2 s/lần), đầy thì bỏ bản cũ nhất.
- Có lại kết nối: gửi bù theo thứ tự cũ -> mới, mỗi lần gửi một lô (ThingsBoard nhận mảng nhiều `{"ts", "values"}` trong một bản tin), không dồn hết một lúc.
- Lưu ra file (vd `gateway/buffer.jsonl`) để tắt bridge giữa chừng không mất. Thêm file này vào `.gitignore`.
- Trong lúc mất mạng vẫn chuyển lệnh local bình thường (giao diện của Phú vẫn điều khiển được).
- Unit test: hàm thêm/lấy hàng đợi, đầy thì bỏ bản cũ, lô gửi bù đúng thứ tự.

**Xong khi:** PR có test PASS; log: ngắt WiFi laptop khỏi internet (hoặc đổi `COREIOT_HOST` sai) 2-3 phút -> bridge báo lưu N bản -> có mạng lại -> gửi bù N bản; ảnh biểu đồ dashboard **không bị khoảng trống** trong đoạn mất mạng.

### #65 Test OTA thật + đo thời gian OTA
Cần lên lab: **có** (cùng Minh, sau khi #45 và #58 có PR). Không có code mới, kết quả ghi vào comment issue.
- Upload `.bin` 1.0.1 lên CoreIOT, gán cho `room1-node-b` -> bridge tự phát hành -> Node B cập nhật.
- Đo 5 lần: thời gian từ lúc gán trên CoreIOT tới khi `fw_version` đổi trên dashboard; số lần thành công / số lần thử. Thêm 1 lần ca lỗi (sai sha256).
- Số liệu thô ghi CSV vào `Report/data/ota.csv`.

**Xong khi:** comment issue có video từ lúc gán firmware tới khi `fw_version` đổi (minh chứng T14), bảng 5 lần đo.

### #66 AI trên Gateway: Isolation Forest (T09)
Nhánh `66-gateway-ai-ml`. Cần lên lab: **không**. Phần học máy của T09, chạy chung `ai_events.py` của Phú (#62). Báo cáo phải tách rõ 3 sự kiện luật (Phú) và 1 sự kiện học máy (Tuấn).
- `App/IOT/ai/train/train_iforest.py`: đọc CSV thật đã thu (`ai/data/raw/`, `ai/data/mqtt_log/`), đặc trưng `temperature`, `humidity`, `light`, giờ trong ngày; train `IsolationForest` (scikit-learn), lưu `ai/models/iforest.joblib` (`joblib`). In số mẫu, tỉ lệ mẫu bị gắn bất thường trên tập train.
- `App/IOT/gateway/ai_ml.py`: `load_model(path)`, `check_anomaly_ml(sample) -> dict | None`. Bất thường thì trả `{"ts", "type": "anomaly_ml", "level": "warning", "msg"}` đúng CONTRACT 10; thiếu key cảm biến (Node A mất) thì trả `None`. Chỉ báo một lần khi bắt đầu bất thường, hết thì cho báo lại.
- Ghép vào `ai_events.py` sau khi #62 có PR: base nhánh `66-...` từ nhánh `62-...` nếu #62 chưa merge (khi #62 merge thì đổi base sang `main`, link issue tay ở Development).
- Unit test: mẫu giống dữ liệu phòng bình thường -> `None`; mẫu vô lý (vd 45 độ C, 10% ẩm lúc 3 giờ sáng) -> có sự kiện; thiếu key -> `None`.

**Xong khi:** PR có test PASS, log train (số mẫu, tỉ lệ bất thường), log chạy với `fake_node_b` có sự kiện `anomaly_ml`, ảnh CoreIOT thấy sự kiện.

### #67 Script đo đạc chương 4
Nhánh `67-measure-scripts`. Cần lên lab: **không** để viết (test bằng `fake_node_b` + `bridge.py`); chạy đo thật ở W4. Tài liệu dự án mục 13.
- `App/IOT/tools/measure.py` với 3 lệnh con, mỗi lệnh ghi CSV vào `Report/data/`:
  | Lệnh | Đo | Cách |
  |---|---|---|
  | `latency_up` | Độ trễ Node B -> CoreIOT | Đọc telemetry mới nhất của `room1-node-b` qua REST API CoreIOT (poll khoảng 200 ms), độ trễ = giờ máy lúc thấy bản mới - `ts` trong bản tin (Node B lấy giờ NTP). Laptop phải đồng bộ giờ Internet |
  | `latency_cmd` | Độ trễ lệnh -> Node B thực hiện | Gửi RPC `setFan` hai chiều qua REST API CoreIOT, đo tới khi có trả lời; song song ghi giờ thấy `room1/cmd` và `room1/cmd/resp` ở broker local để tách đoạn cloud và đoạn local |
  | `recovery` | Thời gian tự phục hồi | Bấm Enter lúc tắt hotspot/bridge, bấm Enter lúc bật lại, script đo tới khi có telemetry mới trên CoreIOT |
- Mỗi phép đo chạy `--n` lần (mặc định 5), in trung bình và lớn nhất.
- Token/tài khoản REST để trong `.env` (thêm biến vào `.env.example`, không commit giá trị thật).

**Xong khi:** PR có CSV mẫu chạy với `fake_node_b` (đặt tên `*_fake.csv`, không dùng làm số liệu báo cáo) và README cách chạy.

---

## 5. Khoa (TV4 - CoreIOT + Webserver)

### #59 Webserver trong Node B (T07)
Nhánh `59-node-b-web`. Cần lên lab: **có**.
- Chép `web.cpp`, `web.h` từ `firmware/webserver_test` (#33) vào `node_b/src/`, dùng `control.h` thật của Node B (bỏ `control_stub.cpp`).
- Webserver chạy trong task riêng (`task_web`), gọi `web_start()` sau khi WiFi nối được. Lệnh từ web dùng `CmdSource::Web`.
- Kiểm: bấm quạt/đèn trên điện thoại -> Node B vào `manual`, dashboard CoreIOT thấy đổi; đang `manual` thì RuleChain không đè.

**Xong khi:** PR có video điều khiển quạt, đèn từ điện thoại cùng hotspot (minh chứng T07), ảnh dashboard CoreIOT đổi theo.

### #60 LCD 16x2 trên Node B
Nhánh `60-node-b-lcd` (tạo từ main sau khi #59 merge, hoặc cùng nhánh #59 nếu tiện, ghi rõ trong PR). Cần lên lab: **có**.
- LCD I2C địa chỉ **0x21**, chân I2C 11/12 (README pinout). Code mẫu: `LiquidCrystal_I2C` như lab 10/11.
- `task_lcd` 2 dòng, đổi màn mỗi 3 s: màn 1 nhiệt độ/độ ẩm; màn 2 quạt/đèn/chế độ. Đọc số liệu qua `control_get_state`/`control_get_sensors`, không đọc biến toàn cục trực tiếp.
- Mất Node A thì in `Node A: mat ket noi` thay số đo.

**Xong khi:** PR có ảnh LCD 2 màn hình và ảnh lúc mất Node A.

### #61 Dashboard: widget OTA + nhật ký sự kiện AI
Nhánh `61-dashboard-ota-event`. Cần lên lab: **không** (test bằng `fake_node_b --ota` và `mosquitto_pub room1/event`).
- Widget `fw_version`, `fw_state` của `room1-node-b`.
- Bảng nhật ký sự kiện AI: `event_type`, `event_level`, `event_msg` trên `room1-gateway` (CONTRACT 11.6), mới nhất ở trên.
- Kiểm alarm `OTA Success`/`OTA Failed` chạy với `fw_state` thật của #45; khác tên key thì sửa RuleChain.
- Export lại `dashboard_room1.json`, cập nhật SETUP_COREIOT.

**Xong khi:** PR có ảnh dashboard có 2 widget mới chạy với dữ liệu thật.

---

## 6. Phú (TV3 - AI + giao diện)

### #48 (W2) - sửa PR #57
Làm theo comment trong PR #57.

### #62 AI trên Gateway: 3 sự kiện theo luật (T09)
Nhánh `62-gateway-ai-rules`. Cần lên lab: **không**. Tài liệu dự án mục 9, CONTRACT mục 10. Phần học máy (`anomaly_ml`) do Tuấn làm ở #66, chạy chung file này.

**File:** `App/IOT/gateway/ai_events.py` (hàm thuần + vòng chạy MQTT), `App/IOT/gateway/test_ai_events.py`.

| Sự kiện | Loại | Luật / cách làm |
|---|---|---|
| `waste_energy` | Luật | `presence = false` mà `fan_level > 0` hoặc `light_on = true` liên tục quá N giây (mặc định 120 s) |
| `sensor_stuck` | Luật | `temperature` (hoặc `humidity`, `light`) giống hệt nhau K bản liên tiếp (mặc định 30 bản = 1 phút) |
| `node_offline` | Luật | `node_a_online = false`, hoặc không có `room1/telemetry` quá 30 s |
| `anomaly_ml` | Học máy | **Tuấn làm (#66)**. Phú chỉ chừa chỗ gọi `check_anomaly_ml(sample)` trong vòng chạy, tạm trả `None` |

- Mỗi sự kiện chỉ báo **một lần** khi bắt đầu (không báo lại mỗi 2 s), hết điều kiện thì cho phép báo lại.
- Publish `room1/event` đúng CONTRACT 10 (`ts`, `type`, `level`, `msg`). Bridge đã chuyển `room1/event` lên CoreIOT (`_handle_local_event`), không cần sửa bridge.
- Mở PR sớm nhất có thể (kể cả chưa xong hết) để Tuấn ghép #66 vào cùng `ai_events.py`.
- Unit test: mỗi luật có ca bắn và ca không bắn; báo một lần rồi thôi.

**Xong khi:** PR có test PASS; log chạy với `fake_node_b` (dùng `--node-a-off-after` cho `node_offline`); ảnh CoreIOT có sự kiện + alarm `AI Event`.

### #63 Giao diện: nhật ký sự kiện + trạng thái kết nối (T10)
Nhánh `63-ui-events` (tạo từ main sau khi #48 merge). Cần lên lab: **không**.
- Thêm vào `ui/app.py`: subscribe `room1/event`, bảng 20 sự kiện gần nhất (giờ, loại, mức, nội dung).
- Trạng thái kết nối: Node A (`node_a_online`), Node B (`room1/status`), Gateway (`room1/gateway/status`).
- Nút `setMode auto` (chưa có ở #31).

**Xong khi:** PR có ảnh/video giao diện có sự kiện mới hiện ra và trạng thái đổi khi tắt `fake_node_b`.

---

## 7. #68 Hướng dẫn chạy phần mình (cả nhóm)

Mỗi người viết 1 file `docs/huong_dan/<phần>.md` (docs, push thẳng main), hạn **20:00 CN 29/11**. Minh gộp vào `HUONG_DAN_CHAY_HE_THONG.md`, người khác không sửa file tổng.
| File | Người | Nội dung tối thiểu |
|---|---|---|
| `firmware.md` | Minh | Nạp Node A/B, OTA |
| `gateway.md` | Tuấn | bridge, OTA publish, gửi bù, `.env`, Isolation Forest, script đo |
| `ai_ui.md` | Phú | Streamlit, `ai_events.py` (3 luật), thu dữ liệu |
| `coreiot_web.md` | Khoa | Import dashboard/RuleChain, webserver, LCD |

Mỗi file: cần cài gì, lệnh chạy (copy được), kết quả đúng trông thế nào, 2-3 lỗi hay gặp.

---

## 8. Issue cần tạo (Minh)

Tạo milestone **W3**, hạn CN 29/11. Nội dung mỗi issue: `Chi tiết: docs/TASKS_W3.md mục <số>. Cần lên lab: có/không. Hạn PR: ... Hạn test board: ...`
| Tiêu đề | Người | Label | Mục |
|---|---|---|---|
| OTA trên Node B | Minh | firmware | 3 |
| Webserver trong Node B | Khoa | firmware | 5 |
| LCD 16x2 trên Node B | Khoa | firmware | 5 |
| Dashboard: widget OTA + nhật ký sự kiện AI | Khoa | coreiot | 5 |
| AI trên Gateway: 3 sự kiện theo luật | Phú | ai-ui | 6 |
| Giao diện: nhật ký sự kiện + trạng thái kết nối | Phú | ai-ui | 6 |
| Bridge lưu tạm khi mất internet, gửi bù | Tuấn | gateway | 4 |
| Test OTA thật + đo thời gian OTA | Tuấn | test | 4 |
| AI trên Gateway: Isolation Forest | Tuấn | ai-ui | 4 |
| Script đo đạc chương 4 | Tuấn | test | 4 |
| Hướng dẫn chạy phần mình | cả nhóm | report | 7 |

#30, #45, #48 giữ milestone cũ.

---

## 9. W4 (30/11 - 6/12) dự kiến

Issue W4 tạo đầu tuần sau. Ai xong W3 sớm thì đọc trước. Việc của W5 (báo cáo, đo đạc, video) kéo lên W4, để W5 chỉ còn ghép, sửa và nộp.

| Việc | Người | Giờ | Lên lab |
|---|---|---|---|
| TinyML trên Node A (T04) | Minh | 3-4h | có |
| Node B chế độ offline + watchdog 2 node | Minh | 5-6h | có |
| Chạy đo chương 4 bằng script #67, ghi CSV vào `Report/data/` | Tuấn, Khoa | 2-3h | có |
| Viết phần báo cáo lớp mình giữ (LaTeX trong `Report/`), kèm ảnh/log minh chứng từ PR | Tuấn, Khoa, Phú | 4-5h/người | không |
| Quay đoạn video phần mình theo kịch bản mục 5 tài liệu dự án | cả nhóm | 1-2h/người | có |
| Khung báo cáo theo mẫu, chương 1 (giới thiệu, kiến trúc) | Minh | 2-3h | không |

W5 (7/12 - 13/12): Minh ghép báo cáo, Khoa dựng video 5-10 phút, README, nộp LMS. Phần firmware của Minh (TinyML, offline) báo cáo viết ở W5.

### TinyML trên Node A (T04)
Cần lên lab: **có** (buổi lab W4, chốt lịch sau).
- Dùng model `.h` của #30 (PR #55). Chuẩn hóa nhiệt độ, độ ẩm bằng `TINYML_*_MEAN/STD`, đầu ra > `TINYML_THRESHOLD` thì `anomaly = 1`.
- Gửi `anomaly`, `anomaly_score` thật trong `PktData` (thay `-1` hiện tại). DHT20 lỗi thì vẫn gửi `-1`.
- Đo thời gian 1 lần suy luận (`micros()`), in ra Serial.

**Xong khi:** PR có log Node A lúc bình thường (score thấp) và lúc áp tay/thổi hơi (score cao, `anomaly = 1`), thời gian suy luận trung bình và lớn nhất, ảnh CoreIOT có alarm `Anomaly`.

### Node B chế độ offline + watchdog 2 node
CONTRACT mục 13. Cần lên lab: **có** (buổi lab W4, chốt lịch sau).
- Mất broker hoặc `room1/gateway/status` = offline quá 30 s -> `mode = "offline"`, Node B tự chạy luật quạt/đèn (cùng hysteresis RuleChain của #47) với bộ ngưỡng đã lưu NVS.
- Có lại cả broker và Gateway -> về chế độ trước đó. Lệnh tay từ webserver vẫn dùng được khi offline.
- Bật Task Watchdog cho các task chính trên cả Node A và Node B.
- Logic luật viết trong `lib/node_b_logic/` + testcase Unity như #42.

**Xong khi:** PR có `pio test -e native` PASS; log tắt `bridge.py` (hoặc hotspot) -> Node B vào `offline` sau khoảng 30 s, áp tay DHT20 thì quạt tự bật; bật lại -> về `auto`.
