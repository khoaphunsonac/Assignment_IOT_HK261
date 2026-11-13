# espnow_rx_test

Node B giả để test Node A (#26) khi chưa có firmware Node B thật.
Nối hotspot (để đứng đúng channel), trả lời ghép cặp, kiểm `magic`/`version`/`len`,
đếm gói nhận/mất/trùng theo `seq` đúng quy tắc nhận ở CONTRACT mục 3.

```bash
cd App/IOT/firmware/espnow_rx_test
pio run -t upload
pio device monitor
```
Cần `App/IOT/firmware/common/secrets.h` (WIFI_SSID, WIFI_PASS).
