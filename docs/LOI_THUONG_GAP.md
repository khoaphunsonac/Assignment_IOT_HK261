# Lỗi thường gặp khi lên lab

Gom từ các buổi lab 10/11 đến 19/11. Gặp lỗi mới thì thêm dòng vào đúng mục.

Nguyên tắc chung: gặp lỗi lạ thì **chụp nguyên log** (monitor + `mosquitto_sub`) trước khi sửa. Đừng sửa code mò tại lab.

---

## 1. USB, cổng COM, nạp code

| Triệu chứng | Nguyên nhân | Làm gì |
|---|---|---|
| Monitor đứng hẳn, báo `Write timeout` hoặc `PermissionError(31)`, đụng vào cáp là đứt | Cổng USB laptop chập chờn (19/11: cổng USB-A + cáp A-to-C). Board vẫn chạy bình thường (telemetry trên broker vẫn đều, seq liên tục) | Đổi cổng hoặc đổi cáp. Đứt thì Ctrl+C, rút cắm cáp, mở lại monitor. Kiểm board còn sống bằng `mosquitto_sub`, đừng tin monitor |
| Bấm RST trên board thì monitor treo luôn | USB-CDC của ESP32-S3 không tự nối lại sau reset | Bấm 1 phím trong monitor. Không được thì Ctrl+C, rút cắm cáp, mở lại |
| Cắm board vào cổng Thunderbolt/USB-C mà máy không nhận | Một số cổng/cáp không nhận board (19/11: Thunderbolt nhận board B nhưng không nhận board A) | Đổi cổng hoặc cáp khác có data. `pio device list` để xem board có hiện không |
| Upload báo `port doesn't exist` hoặc `busy` | Monitor đang giữ cổng, hoặc số COM đổi sau khi reset | Tắt monitor trước khi upload. Mở terminal mới làm lại. `pio device list` xem COM mới |
| Upload không vào được (board mượn đang chạy code cũ của nhóm khác) | Code cũ chiếm USB, không vào chế độ nạp | Giữ nút BOOT, bấm RST, thả BOOT, rồi upload lại |
| Tắt Serial Monitor của extension thì board đơ | Tín hiệu DTR/RTS reset chip vào chế độ nạp | Không dùng extension Serial Monitor. Dùng `pio device monitor` (platformio.ini đã có `monitor_dtr = 0`, `monitor_rts = 0`), thoát bằng Ctrl+C |
| PowerShell báo không có lệnh `pio` | `pio` không nằm trong PATH | Dùng `& "$HOME\.platformio\penv\Scripts\pio.exe" ...` |
| Log boot có `nvs_open failed: NOT_FOUND` | Lần đầu chạy trên board, NVS chưa có gì | Bình thường, chỉ xuất hiện lần đầu |

## 2. Mạng: hotspot, WiFi, broker

| Triệu chứng | Nguyên nhân | Làm gì |
|---|---|---|
| `[MQTT] Ket noi ... FAIL rc=-2` lặp lại | IP laptop đổi (hotspot cấp IP mới mỗi lần bật lại), broker chưa chạy, chạy mosquitto thiếu `-c ...mosquitto.conf` (Mosquitto 2.x mặc định chỉ nghe localhost), hoặc Firewall chặn | `ipconfig` so với `MQTT_HOST` trong `secrets.h`, khác thì sửa rồi nạp lại. Chạy broker đúng lệnh `mosquitto -c App\IOT\gateway\mosquitto.conf -v`. Firewall cho `mosquitto.exe` cả Private và Public |
| mosquitto báo `Only one usage of each socket address` | Service Mosquitto của Windows hoặc app khác đang giữ cổng 1883 | `netstat -ano \| findstr 1883` lấy PID rồi tắt. Nếu là service: PowerShell admin `net stop mosquitto` |
| `[WIFI]` in dấu chấm mãi không OK | Hotspot 5 GHz, sai mật khẩu, hoặc điện thoại tắt hotspot khi tắt màn hình | Bật băng tần 2.4 GHz / "tương thích tối đa". Giữ màn hình điện thoại sáng |
| Hotspot rớt rồi bật lại, Node A mất liên lạc khoảng 10 s | Hotspot bật lại có thể đổi channel WiFi | Bình thường: Node A tự quét lại channel (khoảng 11 s) |
| Telemetry không có key `ts` | Hotspot không có Internet nên NTP chưa đồng bộ | Đúng CONTRACT (không có giờ thì bỏ key). Bật data di động cho điện thoại nếu cần `ts` |

## 3. Lệnh MQTT trên PowerShell

| Triệu chứng | Nguyên nhân | Làm gì |
|---|---|---|
| Mọi lệnh `mosquitto_pub` đều trả `bad_json` | PowerShell 7.3+ truyền nguyên `\"` (PowerShell 5 thì bỏ `\`) | Kiểm `$PSVersionTable.PSVersion`. Cách chắc ăn: ghi JSON ra file rồi gửi file: `'{"id":"test-1","method":"setFan","params":2}' \| Out-File -Encoding ascii c1.json` rồi `mosquitto_pub -h localhost -t room1/cmd -f c1.json` |
| File `.log` lưu bằng `>` mở ra toàn ký tự lạ, nặng gấp đôi | PowerShell 5 ghi `>` theo UTF-16 | Không sao: `mqtt_to_csv.py` tự đọc được. Mở bằng VS Code thì vẫn đọc bình thường |
| Gửi 1 lệnh mà board nhận 2 lần, lần 2 khác tham số | Bấm mũi tên lên rồi Enter nhầm lệnh cũ | Mỗi lần gửi xem lại lệnh trước khi Enter. Test có tính giờ thì gửi lại từ đầu |
| Board vừa nối broker đã nhận `room1/config` cũ | Config cũ còn retained trên broker (do test trước để lại) | Dọn sau mỗi buổi test: `mosquitto_pub -h localhost -t room1/config -r -n` |

## 4. Node A / Node B chạy sai mà code đúng

| Triệu chứng | Nguyên nhân | Làm gì |
|---|---|---|
| `setFan 2` trả ok nhưng quạt tắt ngay; `autoFan` trả ok thay vì `ignored_not_auto` | Luật phòng trống: Node A online, PIR không thấy ai, Node B ép tắt và về auto | Đúng thiết kế. Test lệnh khi chưa cắm Node A, hoặc vẫy tay trước PIR |
| Test hết giờ manual mà không bao giờ về auto | Mỗi lệnh tay đặt lại đồng hồ; hoặc `manual_timeout_s` thật không phải số mình nghĩ (mặc định trong firmware là 1800 s, CoreIOT có thể đẩy số khác) | Xem dòng `[CFG]` lúc boot để biết timeout. Gửi đúng 1 lệnh rồi chờ, không gửi thêm |
| `temperature` trên telemetry lệch so với `T=` trong log | Telemetry gửi trung bình 2 lần đo | Đúng thiết kế, so với số `avg` trong log |
| Rút DHT20 thì Node A treo | Thư viện DHT20 của môn: `read()` chờ vô hạn khi mất cảm biến | Firmware đã gọi `isConnected()` trước `read()`. Nếu tự viết code đọc DHT20 thì cũng phải làm vậy |
| Board reset liên tục, log có `Stack canary watchpoint triggered` hoặc `Guru Meditation` | Tràn stack task (LAB3: task 2048 byte không đủ) | Chụp nguyên đoạn backtrace. Tăng stack task đó (vd 2048 -> 4096) |
| Node B không thấy `xin ghep cap` | Node A chưa nạp / nạp nhầm board, hoặc đang quét channel | `pio device list`, xem log Node A, đợi 15 s |
| Ghép cặp xong nhưng `seq` không lên, `bad=` tăng | Node A và Node B build từ code lệch nhau (`PktData` khác) | Build và nạp lại cả 2 node từ cùng 1 nhánh |

## 5. Bridge và CoreIOT

| Triệu chứng | Nguyên nhân | Làm gì |
|---|---|---|
| Bridge in `rc=7` lặp mỗi 2-3 s | Có máy khác dùng chung token `room1-gateway` | Chỉ 1 máy chạy bridge. Vẫn bị thì nhờ Khoa đổi token |
| `[CFG] room1/config sai, giu bo cu` | Bộ ngưỡng trên CoreIOT thiếu key hoặc sai ràng buộc (vd `fan_on_1 > fan_off_2`) | Chụp dòng `room1/config`, báo Khoa sửa trên CoreIOT. Node B vẫn chạy bằng bộ cũ |
| Bấm dashboard nhưng resp `bad_params` | Widget gửi `params` sai kiểu (vd `2.0`) | Chụp dòng `room1/cmd`, báo Khoa |
| Nhiều phút không có RPC nào, rồi đổ dồn hàng trăm lệnh `autoLight` cùng lúc | CoreIOT giao RPC trễ (19/11 khoảng 10:56-11:07). RuleChain gửi lại mỗi lần có telemetry nên dồn lại | Không phải lỗi firmware hay bridge. Node B vẫn trả lời đủ. Ghi giờ lại, chờ CoreIOT ổn |
| Dashboard quay vòng mãi không load | CoreIOT đang chậm/quá tải | Thử lại sau vài phút. Trong lúc đó test bằng `mosquitto_pub` trực tiếp |
| Gửi link dashboard cho người khác thì họ bị `Permission Denied` | Link nội bộ, không phải link public | Dashboards: bấm share, "Make dashboard public", copy link có `?publicId=...`. Devices: "Make device public" từng thiết bị dashboard dùng |
