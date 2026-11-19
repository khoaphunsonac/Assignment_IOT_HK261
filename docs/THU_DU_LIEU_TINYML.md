# Thu dữ liệu TinyML (ai lên lab thì thu)

Phục vụ #30 (Phú train lại model). File của thầy chỉ có vài chục dòng, cần dữ liệu thật từ DHT20 của Node A.

**Mục tiêu trước CN 29/11:** >= 1500 mẫu nhãn 0 và >= 500 mẫu nhãn 1, từ >= 3 buổi lab khác nhau. Node A gửi 1 mẫu mỗi 2 s, nên 1 giờ chạy được khoảng 1800 mẫu.

| Nhãn | Nghĩa | Cách tạo |
|---|---|---|
| 0 | Phòng bình thường | Để Node A chạy, không đụng vào cảm biến |
| 1 | Bất thường | Nắm tay quanh DHT20 hoặc thổi hơi vào, mỗi lần 3-5 phút |

Có 2 cách thu. **Dùng cách 1 (MQTT)**. Cách 2 (Serial) chỉ dùng khi không có Node B.

---

## Cách 1: thu qua MQTT (khuyên dùng)

Lấy telemetry Node B gửi lên broker. Không cần cắm Node A vào laptop (cắm củ sạc cũng được), không bị đứt như Serial.

**Cần:** broker chạy trên laptop, Node B đã nối MQTT, Node A đã ghép cặp với Node B (telemetry có `temperature`, `humidity`).

1. Mở 1 tab terminal mới (PowerShell), chạy:
   ```powershell
   mkdir D:\tinyml_data -Force
   mosquitto_sub -h localhost -t room1/telemetry > D:\tinyml_data\mqtt_20261125_sang.log
   ```
   Đổi `20261125_sang` thành ngày và buổi của mình. Tab này không hiện gì, đó là bình thường: mọi dòng đi vào file. Muốn xem số đo thì mở tab khác chạy `mosquitto_sub -h localhost -t room1/telemetry`.
2. Mở Notepad, tạo file `D:\tinyml_data\mqtt_20261125_sang.txt` (cùng tên với file `.log`).
3. Để yên >= 30 phút (nhãn 0). Đừng cầm board, đừng đứng sát cảm biến.
4. Làm 2-3 lần nhãn 1, mỗi lần 3-5 phút, các lần cách nhau >= 5 phút. Mỗi lần ghi 1 dòng vào file `.txt`: giờ bắt đầu và giờ kết thúc, **theo đồng hồ laptop**, giờ Việt Nam:
   ```
   # nắm tay
   10:30:52 10:35:39
   # thổi
   10:48:57 10:53:14
   ```
5. Xong thì Ctrl+C tab `mosquitto_sub`.
6. Chép 2 file vào repo rồi chạy script (ở thư mục gốc repo):
   ```powershell
   cd D:\Assignment_IOT_HK261
   copy D:\tinyml_data\mqtt_20261125_sang.* App\IOT\ai\data\mqtt_log\
   python App/IOT/ai/tools/mqtt_to_csv.py App/IOT/ai/data/mqtt_log/mqtt_20261125_sang.log
   ```
   Ra `App/IOT/ai/data/raw/mqtt_20261125_sang.csv`, mỗi dòng `temperature,humidity,label`, không có tiêu đề. Script in số mẫu mỗi nhãn, ghi số đó vào bảng cuối file này.
7. Commit cả `.log`, `.txt`, `.csv` thẳng lên `main` (dữ liệu, không phải code). File `.log` bị `.gitignore` chặn nên phải `git add -f`:
   ```powershell
   git add -f App/IOT/ai/data/mqtt_log/mqtt_20261125_sang.log
   git add App/IOT/ai/data/mqtt_log/mqtt_20261125_sang.txt App/IOT/ai/data/raw/mqtt_20261125_sang.csv
   git commit -m "data(ai): mqtt 25/11 sang"
   git push origin main
   ```

**Script tự xử lý (không cần làm tay):**
- File UTF-16 do PowerShell 5 ghi bằng `>`: tự nhận.
- `ts` trong telemetry là giờ UTC: tự cộng 7 giờ để so với file `.txt`.
- Dòng không có số đo (Node A mất, `node_a_online: false`): bỏ.
- Bỏ 5 phút sau mỗi lần nhãn 1 (cảm biến đang nguội, không phải 0 cũng không phải 1). Số đo 19/11: sau khi bỏ tay 3 phút nhiệt độ vẫn cao hơn nền khoảng 1 °C.

**Khác với Serial:** `temperature` trong telemetry là trung bình 2 lần đo gần nhất (Node B tính), không phải số đo thô. Chênh rất nhỏ vì nhiệt độ đổi chậm.

---

## Cách 2: thu qua Serial (khi không có Node B)

Node A cắm laptop, log lấy qua USB. Trên một số laptop monitor hay đứt (xem `docs/LOI_THUONG_GAP.md`), mất đoạn nào là mất mẫu đoạn đó.

1. Mở terminal ở `App/IOT/firmware/node_a`, chạy:
   ```powershell
   cd D:\Assignment_IOT_HK261\App\IOT\firmware\node_a
   & "$HOME\.platformio\penv\Scripts\pio.exe" device monitor -f time -f log2file
   ```
   PlatformIO ghi file `logs\device-monitor-<ngày-giờ>.log` trong thư mục `node_a`, mỗi dòng có giờ ở đầu. Đúng thứ tự `-f time` trước `-f log2file`, đảo lại thì file không có giờ.
2. Bước nhãn 0, nhãn 1 và file `.txt` giống cách 1 (giờ theo đồng hồ laptop).
3. Ctrl+C. Đổi tên file log thành `a_<tên>_<YYYYMMDD_HHMM>.log`, file `.txt` cùng tên, chép vào `App/IOT/ai/data/serial_log/`, rồi chạy:
   ```powershell
   cd D:\Assignment_IOT_HK261
   python App/IOT/ai/tools/serial_to_csv.py App/IOT/ai/data/serial_log/a_minh_20261125_0915.log
   ```
   Ra `App/IOT/ai/data/raw/serial_a_minh_20261125_0915.csv`. Script bỏ mẫu `valid: 0` và bỏ 3 phút sau mỗi lần nhãn 1.
4. Commit như bước 7 cách 1 (nhớ `git add -f` cho `.log`).

---

## Lỗi hay gặp khi thu

- **Quên ghi giờ nhãn 1:** bỏ cả đoạn đó, coi như chỉ có nhãn 0. Không đoán giờ.
- **Ghi giờ theo điện thoại:** lệch vài chục giây so với laptop là nhãn sai ở 2 đầu. Ghi theo đồng hồ laptop.
- **Script báo "Không có mẫu nào":**
  - MQTT: Node A chưa ghép cặp (mọi dòng đều `node_a_online: false`), hoặc lệnh `mosquitto_sub` sai topic.
  - Serial: sai thứ tự filter, file không có giờ ở đầu dòng.
- **Mất mạng hotspot giữa chừng:** file MQTT hụt đoạn đó, không sao. Ghi chú giờ mất mạng vào `.txt` (dòng bắt đầu bằng `#`).
- **Node A nằm cạnh quạt Node B đang chạy:** vẫn là nhãn 0, nhưng ghi chú trong `.txt` để Phú biết.
- **Thở/thổi liên tục 5 phút rất mệt:** thổi ngắt quãng cũng được, ghi giờ bắt đầu và kết thúc cả đoạn; hoặc đổi sang nắm tay.

---

## Bảng thu (ai thu thì tự thêm dòng)

| Ngày | Người | File | Nhãn 0 | Nhãn 1 | Ghi chú |
|---|---|---|---|---|---|
| 19/11 | Minh | mqtt_20261119_sang | 89 | 531 | 4 lần nhãn 1 (nắm tay, thổi); mất mạng 10:53-10:56 |
| 19/11 | Minh | mqtt_20261119_sang2 | 144 | 219 | 2 lần nhãn 1 (nắm tay, thổi ngắt quãng) |
| **Tổng** | | | **233** | **750** | Nhãn 1 đã đủ; còn thiếu nhãn 0 (cần chạy nền lâu, ít tác động) |
