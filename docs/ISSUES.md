# Danh sách issue dự kiến

Bản nháp để tạo issue trên GitHub. Điểm số lấy theo đề bài BTL.

## Phân công theo vai trò

| Vai trò | Issue |
|---|---|
| Cả nhóm | PRE-01, PRE-02 |
| CE và Fullstack | ARCH-01 |
| CE | IOT-01, IOT-04, IOT-05, IOT-06 |
| Fullstack | IOT-02, IOT-03, IOT-07, BE-04, DOC-03 |
| BE | BE-01, BE-02, BE-03, BE-05, DOC-02 |
| FE | FE-01, FE-02, FE-03, FE-04, FE-05, DOC-01 |

Mọi yếu tố công nghệ cần có minh chứng (ảnh, log, video, mã nguồn) trong báo cáo thì mới được tính điểm.

## Đối chiếu với tính năng dự án cũ

Nguồn: báo cáo `DADN251_11.pdf`. Một số tính năng chỉ được mô tả trong báo cáo cũ mà chưa có trong code cũ, nên phải làm mới và có minh chứng.

| Tính năng | Issue |
|---|---|
| Đăng ký, đăng nhập, cập nhật thông tin cá nhân | BE-01, FE-02 |
| Quản lý phòng, trạng thái phòng | BE-03, FE-03 |
| Điều khiển thiết bị, chỉnh tốc độ và cường độ | IOT-01, BE-04, FE-04 |
| Nhật ký điều khiển thiết bị | BE-03, FE-04 |
| Dữ liệu thời gian thực, lịch sử ngày/tuần/tháng | BE-04, FE-04 |
| Lịch sử sử dụng phòng | BE-03, FE-03, FE-04 |
| Thông báo vượt ngưỡng, đánh dấu đã đọc | IOT-03, BE-05, FE-05 |
| Chế độ tự động, cấu hình ngưỡng | IOT-03, BE-04, FE-04, FE-05 |
| Lưu trữ dữ liệu tập trung | BE-02, BE-04 |
| Màn hình LCD, LED RGB | IOT-01 |
| Kiểm thử hiệu năng | DOC-02 |

## Ưu tiên cao nhất

Hoàn thành hai issue này trước, vì các issue còn lại phụ thuộc vào kết quả của chúng.

### PRE-01: Xác nhận mô hình kết nối với giảng viên
- Phụ trách: Cả nhóm
- Label: `priority:highest`, `architecture`
- Mô hình đề xuất: kết hợp. Node cảm biến gửi dữ liệu bằng ESP-NOW sang node trung tâm, node trung tâm gửi lên CoreIOT, tính năng thông minh chạy bằng RuleChain.
- Yếu tố công nghệ liên quan: T06, T12. Node trung tâm trên YoloUNO không tính là T08, vì T08 yêu cầu Gateway trên PC hoặc Raspberry Pi.
- [ ] Cả nhóm thống nhất mô hình đề xuất
- [ ] Hỏi giảng viên mô hình này có được chấp nhận không
- [ ] Lưu lại câu trả lời của giảng viên và ghi kết quả vào issue này
- [ ] Nếu không được chấp nhận, cả nhóm chốt mô hình khác và cập nhật các issue liên quan
- **Hoàn thành khi:** có câu trả lời của giảng viên và mô hình đã được chốt.

### PRE-02: Chuẩn bị ít nhất 2 mạch YoloUNO
- Phụ trách: Cả nhóm
- Label: `priority:highest`, `hardware`
- Lý do: yêu cầu nhiều thiết bị cùng hoạt động và T06 (ESP-NOW) đều cần ít nhất 2 mạch.
- [ ] Kiểm tra số mạch YoloUNO (ESP32-S3) nhóm đang có
- [ ] Mượn hoặc mua thêm nếu chưa đủ 2 mạch
- [ ] Kiểm tra cảm biến và thiết bị chấp hành cần dùng
- [ ] Nạp thử chương trình đơn giản lên từng mạch
- **Hoàn thành khi:** có ít nhất 2 mạch YoloUNO nạp và chạy được.

## Hạ tầng và IoT

### ARCH-01: Chốt kiến trúc, tính năng và mục tiêu đo
- Phụ trách: Fullstack và CE, cả nhóm cùng duyệt
- Label: `report`, `priority:high`
- [ ] Ghi mô hình kết nối đã chốt ở PRE-01 và lý do lựa chọn
- [ ] Xác định vai trò từng mạch, cảm biến và thiết bị chấp hành gắn ở đâu
- [ ] Vẽ sơ đồ kiến trúc có luồng telemetry, RPC và giao thức trên từng kết nối
- [ ] Chốt danh sách tính năng F01, F02… và mã công nghệ đăng ký tính điểm
- [ ] Chốt mục tiêu đo được cho Mục 1.3 (độ trễ, mất gói, độ chính xác…)
- Phụ thuộc: PRE-01, PRE-02
- **Hoàn thành khi:** cả nhóm thống nhất, trước khi bắt đầu các việc còn lại.

### IOT-01: Chuẩn bị phần cứng và kết nối YoloUNO với CoreIOT
- Phụ trách: CE
- Label: `iot`, `priority:high`
- Phụ thuộc: PRE-02, ARCH-01
- [ ] Tạo project firmware trong `App/IOT/firmware/`, có file cấu hình mẫu cho Wi-Fi và token
- [ ] Gửi telemetry nhiệt độ, độ ẩm, ánh sáng lên CoreIOT qua MQTT
- [ ] Nhận lệnh RPC để điều khiển đèn, quạt, máy phun
- [ ] Điều chỉnh tốc độ quạt và cường độ máy phun theo mức 0–100% bằng PWM
- [ ] Điều khiển LED RGB
- [ ] Hiển thị nhiệt độ, độ ẩm, ánh sáng và chế độ tự động trên LCD1602
- [ ] Vẽ sơ đồ đấu nối, lưu vào `App/IOT/docs/`
- **Hoàn thành khi:** dashboard CoreIOT thấy dữ liệu thật và điều khiển được thiết bị.

### IOT-02: Dashboard CoreIOT (T11, 1.0 điểm)
- Phụ trách: Fullstack
- Label: `iot`, `coreiot`
- [ ] Tạo device profile, template và dashboard giám sát, điều khiển từ xa
- [ ] Export dashboard vào `App/IOT/coreiot/`
- [ ] Lấy public link dashboard, nếu có, để ghi vào báo cáo

### IOT-03: RuleChain và cảnh báo (T12 1.0, T13 0.5 điểm)
- Phụ trách: Fullstack
- Label: `iot`, `coreiot`
- [ ] Hiện thực logic tự động bằng RuleChain
- [ ] Có chế độ tự động và thủ công, logic tự động chỉ chạy khi bật chế độ tự động
- [ ] Đọc ngưỡng từ attribute của thiết bị để cấu hình được từ web
- [ ] Gửi notification khi dữ liệu vượt ngưỡng
- [ ] Export RuleChain vào `App/IOT/coreiot/`
- [ ] Đo thời gian từ lúc vượt ngưỡng đến lúc nhận cảnh báo

### IOT-04: Cập nhật firmware OTA (T14, 2.0 điểm)
- Phụ trách: CE
- Label: `iot`, `coreiot`
- [ ] Tải firmware lên CoreIOT và gán cho thiết bị
- [ ] Ghi log phiên bản trước và sau khi cập nhật
- [ ] Đo thời gian cập nhật

### IOT-05: Giao tiếp ESP-NOW giữa các node (T06, 1.5 điểm)
- Phụ trách: CE
- Label: `iot`
- [ ] Node cảm biến gửi dữ liệu sang node trung tâm bằng ESP-NOW
- [ ] Có bước ghép cặp giữa các node
- [ ] Có cấu trúc gói tin, cơ chế xác nhận và gửi lại
- [ ] Đo tỉ lệ mất gói và tầm xa

### IOT-06: Webserver trên thiết bị (T07, 1.0 điểm)
- Phụ trách: CE
- Label: `iot`
- [ ] Tự hiện thực trang web trên mạch để hiển thị và điều khiển local
- [ ] Chụp ảnh giao diện làm minh chứng

### IOT-07: TinyML (T04 1.0, T05 1.0 điểm)
- Phụ trách: Fullstack (CE hỗ trợ nạp model)
- Label: `iot`, `ai`
- [ ] Chọn bài toán AI, thu thập và gán nhãn dữ liệu từ cảm biến thật
- [ ] Có công cụ thu thập dữ liệu, ghi rõ số mẫu, số lớp và cách gán nhãn
- [ ] Huấn luyện, đánh giá (độ chính xác, ma trận nhầm lẫn), export model
- [ ] Chạy suy luận trên mạch, gửi kết quả lên CoreIOT
- [ ] Ghi kích thước model và thời gian suy luận trên mạch
- [ ] Lưu dữ liệu, notebook và model vào `App/IOT/ai/`

## Backend

### BE-01: Xác thực người dùng
- Phụ trách: BE
- Label: `backend`
- [ ] Đăng ký, đăng nhập, `/api/auth/me` bằng JWT và bcrypt
- [ ] Kiểm tra dữ liệu đầu vào bằng express-validator
- [ ] Xem và cập nhật thông tin cá nhân qua `/api/users/profile`
- [ ] Viết test

### BE-02: Model và database
- Phụ trách: BE
- Label: `backend`
- [ ] Tạo model User, Room, Device, EnvironmentData, Notification, UsageHistory và quan hệ
- [ ] Viết migration đầu tiên trong `App/BE-web/migrations/`, có `up` và `down`
- [ ] Viết seeder dữ liệu mẫu cho FE, không chứa mật khẩu thật
- [ ] Viết tài liệu API tại `App/BE-web/docs/API.md`

### BE-03: API phòng, thiết bị, lịch sử sử dụng
- Phụ trách: BE
- Label: `backend`
- [ ] Hiện thực `/api/rooms`, `/api/devices`, `/api/usage-history`, có phân trang
- [ ] Mỗi người dùng chỉ truy cập được dữ liệu của mình
- [ ] Thêm, xem, sửa, xóa phòng; gắn thiết bị CoreIOT với phòng
- [ ] Ghi nhật ký cho mọi thao tác điều khiển thiết bị
- [ ] Ghi thời gian bắt đầu, kết thúc và thời lượng mỗi phiên sử dụng phòng

### BE-04: Kết nối CoreIOT
- Phụ trách: Fullstack
- Label: `backend`, `coreiot`
- [ ] Viết service đọc telemetry và gửi RPC qua API CoreIOT
- [ ] Hiện thực `/api/coreiot` và `/api/environment`
- [ ] Lưu telemetry vào database, kể cả khi không ai mở web
- [ ] Lọc lịch sử dữ liệu môi trường theo ngày, tuần, tháng
- [ ] Gửi chế độ tự động/thủ công và ngưỡng từ web xuống CoreIOT
- [ ] Token CoreIOT chỉ lưu trong `.env` hoặc database

### BE-05: Phát hiện và ghi nhận sự kiện (N06 cùng FE-05, 1.0 điểm)
- Phụ trách: BE
- Label: `backend`
- [ ] Tác vụ nền nhận dữ liệu, kể cả khi không ai mở web
- [ ] Tự phát hiện vượt ngưỡng, thiết bị mất kết nối và ghi vào database
- [ ] Hiện thực `/api/notifications`

## Frontend

### FE-01: Giao diện dùng chung
- Phụ trách: FE
- Label: `frontend`
- [ ] Layout, màu sắc, component dùng chung, hiển thị tốt trên điện thoại

### FE-02: Trang chủ, đăng nhập, đăng ký
- Phụ trách: FE
- Label: `frontend`
- [ ] Hoàn thiện `AuthContext`, form và bảo vệ trang cần đăng nhập
- [ ] Thêm trang xem và cập nhật thông tin cá nhân
- Phụ thuộc: BE-01

### FE-03: Dashboard và tạo phòng
- Phụ trách: FE
- Label: `frontend`
- [ ] Danh sách phòng và trạng thái, form tạo phòng
- [ ] Thẻ phòng hiển thị nhiệt độ, độ ẩm, ánh sáng và trạng thái online/offline
- [ ] Hiển thị lịch sử sử dụng gần đây và thông báo mới ngay trên dashboard
- [ ] Sửa và xóa phòng
- Phụ thuộc: BE-03

### FE-04: Chi tiết phòng
- Phụ trách: FE
- Label: `frontend`
- [ ] Biểu đồ môi trường, điều khiển thiết bị, lịch sử sử dụng
- [ ] Biểu đồ thời gian thực và biểu đồ lịch sử theo ngày, tuần, tháng
- [ ] Công tắc bật/tắt, thanh chỉnh tốc độ quạt và cường độ máy phun
- [ ] Bật/tắt chế độ tự động, xem nhật ký điều khiển thiết bị
- Phụ thuộc: BE-03, BE-04

### FE-05: Giao diện sự kiện và thông báo (N06 cùng BE-05)
- Phụ trách: FE
- Label: `frontend`
- [ ] Danh sách sự kiện, cấu hình ngưỡng, đánh dấu đã đọc
- Phụ thuộc: BE-05

## Báo cáo và nộp bài

### DOC-01: Viết báo cáo LaTeX
- Phụ trách: FE (mỗi người viết phần mình làm)
- Label: `report`
- [ ] Hoàn thành Chương 1 đến Chương 6 trong `Report/`, tối thiểu 5 tài liệu tham khảo
- [ ] Viết bảng tính năng F01, F02… ở Mục 1.4 và bảng ánh xạ với mã công nghệ ở Mục 2.4
- [ ] Cập nhật bảng phân công theo công việc thực tế, gắn mã công nghệ và tỉ lệ đóng góp, tổng 100%
- [ ] Bảng tự đánh giá, bảng phân công, khai báo phạm vi dùng công cụ AI
- [ ] Ghi rõ phần kế thừa từ dự án mẫu hoặc dự án trước

### DOC-02: Đo đạc và thử nghiệm
- Phụ trách: BE (mỗi người đo phần mình làm)
- Label: `report`, `test`
- [ ] Đo độ trễ đầu-cuối, độ trễ điều khiển, mất gói, OTA, độ chính xác AI
- [ ] Thử các kịch bản: chạy liên tục dài giờ, mất Wi-Fi rồi kết nối lại, giá trị vượt ngưỡng
- [ ] Đo thời gian phản hồi cảnh báo; đo công suất tiêu thụ nếu có dụng cụ
- [ ] So sánh với mục tiêu ở Mục 1.3, giải thích trường hợp chưa đạt
- [ ] Mỗi chỉ số đo nhiều lần, ghi trung bình, nhỏ nhất, lớn nhất
- [ ] Đo hiệu năng API bằng JMeter, lưu kịch bản vào repo; đo giao diện bằng Lighthouse

### DOC-03: Video demo và đóng gói nộp bài
- Phụ trách: Fullstack
- Label: `report`
- [ ] Video 5–10 phút trình bày từng tính năng đăng ký tính điểm
- [ ] Cập nhật các README để người khác chạy lại được
- [ ] Xuất PDF `[Mã lớp]_[Mã nhóm]_BaoCaoIoT.pdf`
- [ ] Ghi link GitHub và video vào báo cáo; repo public hoặc cấp quyền cho giảng viên
- [ ] Điền minh chứng cho từng mã trong bảng tự đánh giá; bỏ các mã không có minh chứng
