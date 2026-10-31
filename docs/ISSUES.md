# Danh sách issue dự kiến

Bản nháp để tạo issue trên GitHub. Người phụ trách sẽ được gán khi chia task. Điểm số lấy theo đề bài BTL.

Mọi yếu tố công nghệ cần có minh chứng (ảnh, log, video, mã nguồn) trong báo cáo thì mới được tính điểm.

## Hạ tầng và IoT

### IOT-01: Chuẩn bị phần cứng và kết nối YoloUNO với CoreIOT
- Label: `iot`, `priority:high`
- [ ] Chuẩn bị ít nhất 2 mạch YoloUNO (ESP32-S3)
- [ ] Tạo project firmware trong `App/IOT/firmware/`, có file cấu hình mẫu cho Wi-Fi và token
- [ ] Gửi telemetry nhiệt độ, độ ẩm, ánh sáng lên CoreIOT qua MQTT
- [ ] Nhận lệnh RPC để điều khiển đèn, quạt, máy phun
- [ ] Vẽ sơ đồ đấu nối, lưu vào `App/IOT/docs/`
- **Hoàn thành khi:** dashboard CoreIOT thấy dữ liệu thật và điều khiển được thiết bị.

### IOT-02: Dashboard CoreIOT (T11, 1.0 điểm)
- Label: `iot`, `coreiot`
- [ ] Tạo device profile, template và dashboard giám sát, điều khiển từ xa
- [ ] Export dashboard vào `App/IOT/coreiot/`

### IOT-03: RuleChain và cảnh báo (T12 1.0, T13 0.5 điểm)
- Label: `iot`, `coreiot`
- [ ] Hiện thực logic tự động bằng RuleChain
- [ ] Gửi notification khi dữ liệu vượt ngưỡng
- [ ] Export RuleChain vào `App/IOT/coreiot/`
- [ ] Đo thời gian từ lúc vượt ngưỡng đến lúc nhận cảnh báo

### IOT-04: Cập nhật firmware OTA (T14, 2.0 điểm)
- Label: `iot`, `coreiot`
- [ ] Tải firmware lên CoreIOT và gán cho thiết bị
- [ ] Ghi log phiên bản trước và sau khi cập nhật
- [ ] Đo thời gian cập nhật

### IOT-05: Giao tiếp ESP-NOW giữa các node (T06, 1.5 điểm)
- Label: `iot`
- [ ] Node cảm biến gửi dữ liệu sang node trung tâm bằng ESP-NOW
- [ ] Có cấu trúc gói tin, cơ chế xác nhận và gửi lại
- [ ] Đo tỉ lệ mất gói và tầm xa

### IOT-06: Webserver trên thiết bị (T07, 1.0 điểm)
- Label: `iot`
- [ ] Tự hiện thực trang web trên mạch để hiển thị và điều khiển local
- [ ] Chụp ảnh giao diện làm minh chứng

### IOT-07: TinyML (T04 1.0, T05 1.0 điểm)
- Label: `iot`, `ai`
- [ ] Chọn bài toán AI, thu thập và gán nhãn dữ liệu từ cảm biến thật
- [ ] Huấn luyện, đánh giá (độ chính xác, ma trận nhầm lẫn), export model
- [ ] Chạy suy luận trên mạch, gửi kết quả lên CoreIOT
- [ ] Lưu dữ liệu, notebook và model vào `App/IOT/ai/`

## Backend

### BE-01: Xác thực người dùng
- Label: `backend`
- [ ] Đăng ký, đăng nhập, `/api/auth/me` bằng JWT và bcrypt
- [ ] Kiểm tra dữ liệu đầu vào bằng express-validator
- [ ] Viết test

### BE-02: Model và database
- Label: `backend`
- [ ] Tạo model User, Room, Device, EnvironmentData, Notification, UsageHistory và quan hệ
- [ ] Viết script khởi tạo database

### BE-03: API phòng, thiết bị, lịch sử sử dụng
- Label: `backend`
- [ ] Hiện thực `/api/rooms`, `/api/devices`, `/api/usage-history`, có phân trang
- [ ] Mỗi người dùng chỉ truy cập được dữ liệu của mình

### BE-04: Kết nối CoreIOT
- Label: `backend`, `coreiot`
- [ ] Viết service đọc telemetry và gửi RPC qua API CoreIOT
- [ ] Hiện thực `/api/coreiot` và `/api/environment`
- [ ] Token CoreIOT chỉ lưu trong `.env` hoặc database

### BE-05: Phát hiện và ghi nhận sự kiện (N06 cùng FE-05, 1.0 điểm)
- Label: `backend`
- [ ] Tác vụ nền nhận dữ liệu, kể cả khi không ai mở web
- [ ] Tự phát hiện vượt ngưỡng, thiết bị mất kết nối và ghi vào database
- [ ] Hiện thực `/api/notifications`

## Frontend

### FE-01: Giao diện dùng chung
- Label: `frontend`
- [ ] Layout, màu sắc, component dùng chung, hiển thị tốt trên điện thoại

### FE-02: Trang chủ, đăng nhập, đăng ký
- Label: `frontend`
- [ ] Hoàn thiện `AuthContext`, form và bảo vệ trang cần đăng nhập
- Phụ thuộc: BE-01

### FE-03: Dashboard và tạo phòng
- Label: `frontend`
- [ ] Danh sách phòng và trạng thái, form tạo phòng
- Phụ thuộc: BE-03

### FE-04: Chi tiết phòng
- Label: `frontend`
- [ ] Biểu đồ môi trường, điều khiển thiết bị, lịch sử sử dụng
- Phụ thuộc: BE-03, BE-04

### FE-05: Giao diện sự kiện và thông báo (N06 cùng BE-05)
- Label: `frontend`
- [ ] Danh sách sự kiện, cấu hình ngưỡng, đánh dấu đã đọc
- Phụ thuộc: BE-05

## Báo cáo và nộp bài

### DOC-01: Viết báo cáo LaTeX
- Label: `report`
- [ ] Hoàn thành Chương 1 đến Chương 6 trong `Report/`, tối thiểu 5 tài liệu tham khảo
- [ ] Bảng tự đánh giá, bảng phân công, khai báo phạm vi dùng công cụ AI
- [ ] Ghi rõ phần kế thừa từ dự án mẫu hoặc dự án trước

### DOC-02: Đo đạc và thử nghiệm
- Label: `report`, `test`
- [ ] Đo độ trễ đầu-cuối, độ trễ điều khiển, mất gói, OTA, độ chính xác AI
- [ ] Mỗi chỉ số đo nhiều lần, ghi trung bình, nhỏ nhất, lớn nhất

### DOC-03: Video demo và đóng gói nộp bài
- Label: `report`
- [ ] Video 5–10 phút trình bày từng tính năng đăng ký tính điểm
- [ ] Cập nhật các README để người khác chạy lại được
- [ ] Xuất PDF `[Mã lớp]_[Mã nhóm]_BaoCaoIoT.pdf`
