# BE-web

Backend REST API, viết bằng Node.js, Express và Sequelize (MySQL).

Đây là sườn dự án: các API đang trả về `501 Not implemented`. Web app chỉ làm nếu phần IoT chạy đủ trước CN 29/11 (xem README gốc).

## Cài đặt và chạy

Cần Node.js 18 trở lên và Docker.

```bash
cd App/BE-web
npm install
cp .env.example .env     # đặt thêm JWT_SECRET
npm run db:up            # chạy MySQL bằng Docker
npm run db:migrate       # tạo hoặc cập nhật bảng
npm run dev
```

Kiểm tra tại `http://localhost:5000/health`. Chạy test bằng `npm test`.

Mật khẩu trong `.env.example` chỉ dùng cho MySQL local. Nếu cổng 3306 đã bị dùng, đổi `DB_PORT` trong `.env`. Để trống `DB_HOST` thì server chạy không cần database.

## Database

| Lệnh | Tác dụng |
|---|---|
| `npm run db:up` | Bật MySQL và chờ đến khi sẵn sàng |
| `npm run db:down` | Tắt MySQL, giữ dữ liệu |
| `npm run db:reset` | **Xóa toàn bộ dữ liệu local**, tạo lại MySQL trống |
| `npm run db:migrate` | Chạy các migration còn thiếu |
| `npm run db:migrate:status` | Xem migration đã chạy và chưa chạy |
| `npm run db:migrate:undo` | Hoàn tác migration gần nhất |
| `npm run db:seed` | Thêm dữ liệu mẫu |

### Khi BE thay đổi database

1. Tạo migration: `npm run db:migration:create -- --name add-threshold-to-device`
2. Viết `up` và `down` trong file vừa tạo ở `migrations/`, rồi sửa model tương ứng.
3. Chạy thử `db:migrate`, `db:migrate:undo`, rồi `db:migrate` lại.
4. Trong PR, ghi rõ có migration và API nào thay đổi.

### Khi người khác đã đổi database

```bash
git pull
npm install
npm run db:migrate
```

### Quy ước

- Không sửa migration đã merge, muốn đổi thì tạo migration mới.
- Không dùng `sequelize.sync({ alter: true })`.
- Dữ liệu mẫu không chứa mật khẩu hay token thật.

## Cấu trúc thư mục

| Đường dẫn | Nội dung |
|---|---|
| `app.js` | Cấu hình Express, gắn middleware và routes |
| `server.js` | Điểm khởi động server |
| `config/` | Kết nối database, cấu hình Sequelize CLI |
| `migrations/` | Lịch sử thay đổi database |
| `seeders/` | Dữ liệu mẫu |
| `docker-compose.yml` | MySQL local |
| `middleware/` | Xác thực JWT, xử lý lỗi |
| `models/` | Model Sequelize |
| `routes/` | Khai báo API |
| `utils/` | JWT, định dạng phản hồi |
| `tests/` | Test bằng Jest, Supertest |

## Nhóm API

| Đường dẫn | Chức năng |
|---|---|
| `/api/auth` | Đăng ký, đăng nhập |
| `/api/users` | Thông tin người dùng |
| `/api/rooms` | Quản lý phòng |
| `/api/devices` | Quản lý, điều khiển thiết bị |
| `/api/environment` | Dữ liệu môi trường |
| `/api/notifications` | Thông báo |
| `/api/usage-history` | Lịch sử sử dụng |
| `/api/coreiot` | Kết nối CoreIOT |
