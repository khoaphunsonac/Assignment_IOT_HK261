# Assignment IoT HK261 – Nhóm 4

Bài tập lớn môn Phát triển ứng dụng IoT (CO3038), học kỳ 261.

**Đề tài:** Giám sát và điều hòa phòng học, dùng ESP-NOW, TinyML và OTA.

Node A đo nhiệt độ, độ ẩm, ánh sáng, chuyển động và chạy TinyML. Node A gửi số đo qua ESP-NOW cho Node B. Node B điều khiển quạt, đèn, LCD, chạy webserver và gửi dữ liệu qua MQTT cho Gateway Python trên laptop. Gateway đẩy dữ liệu lên CoreIOT, nhận lệnh và ngưỡng từ dashboard và RuleChain, chạy AI phát hiện sự kiện, phân phối OTA cho Node B.

```
Node A ──ESP-NOW──> Node B ──MQTT (Mosquitto)──> Gateway Python ──> CoreIOT
(cảm biến, TinyML)  (quạt, đèn, LCD, web)          │                (dashboard, RuleChain)
                                                   └──> Giao diện local
```

## Đọc trước khi code

| File | Nội dung |
|---|---|
| [docs/CONTRACT.md](docs/CONTRACT.md) | **Bắt buộc.** Topic MQTT, key, lệnh, gói ESP-NOW. Code của mọi người phải khớp file này |
| [GitHub Issues](https://github.com/khoaphunsonac/Assignment_IOT_HK261/issues) | Việc từng tuần, milestone W1–W5 |
| [App/IOT/README.md](App/IOT/README.md) | Phần cứng, chân, cách build và nạp firmware |

## Thành viên

| Vai | Người | Phần giữ |
|---|---|---|
| TV1 – Lead + Firmware | Trần Lê Minh | Node A, Node B, ESP-NOW, TinyML trên chip, OTA phía thiết bị |
| TV2 – Gateway | Mai Anh Tuấn | Broker MQTT, cầu nối CoreIOT, phân phối OTA, `fake_node_b.py` |
| TV3 – AI + giao diện | Hà Minh Phú | AI trên Gateway, giao diện local, thu dữ liệu và train TinyML |
| TV4 – CoreIOT + Webserver | Trần Đăng Khoa | Webserver Node B, dashboard, RuleChain, cảnh báo |

## Cấu trúc thư mục

```
docs/CONTRACT.md            quy ước dữ liệu và giao thức dùng chung
App/
  IOT/
    firmware/
      common/               espnow_packet.h dùng chung 2 node
      node_a/               project PlatformIO Node A
      node_b/               project PlatformIO Node B
    gateway/                Python: bridge CoreIOT, OTA server, AI, giao diện, fake_node_b.py
    ai/
      data/                 CSV thu được
      train/                script train TinyML
      models/               .tflite và .h đã xuất
    coreiot/                export JSON của dashboard, RuleChain
    docs/                   pinout, ghi chú phần cứng
Report/                     báo cáo LaTeX, Report/data/ chứa số liệu đo
```

## Quy ước làm việc

- **Docs** (`docs/`, `Report/`, README): push thẳng `main`. Nhớ `git pull` trước khi sửa.
- **Code:** mỗi issue một branch, tên theo số issue, ví dụ `26-node-a-sensor-espnow`. Xong mở PR có dòng `Closes #26` và minh chứng (log, ảnh, video). Minh duyệt rồi merge.
- **Commit** bằng tài khoản GitHub của chính mình (thông báo BTL yêu cầu lịch sử commit thể hiện ai làm gì).
- **Bí mật:** mật khẩu WiFi và token CoreIOT chỉ để trong `secrets.h` hoặc `.env` (đã có trong `.gitignore`). Repo public, commit nhầm là lộ.
- **Dùng AI:** ai dùng cho phần nào thì ghi lại để đưa vào báo cáo.

## Nộp bài

- Báo cáo PDF `[Mã lớp]_[Mã nhóm]_BaoCaoIoT.pdf`: xem [Report/README.md](Report/README.md).
- Video demo 5–10 phút.
- Repo này, có README.
