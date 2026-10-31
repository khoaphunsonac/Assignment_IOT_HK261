# BE-web

Backend REST API, viết bằng Node.js, Express và Sequelize (MySQL).

Đây là sườn dự án: các API đang trả về `501 Not implemented`. Phần chi tiết xem tại [docs/ISSUES.md](../../docs/ISSUES.md).

## Cài đặt và chạy

Cần Node.js 18 trở lên.

```bash
cd App/BE-web
npm install
cp .env.example .env
npm run dev
```

Kiểm tra tại `http://localhost:5000/health`. Chạy test bằng `npm test`.

Để trống `DB_HOST` thì server chạy không cần database. Đặt `JWT_SECRET` trước khi dùng các API cần đăng nhập.

## Cấu trúc thư mục

| Đường dẫn | Nội dung |
|---|---|
| `app.js` | Cấu hình Express, gắn middleware và routes |
| `server.js` | Điểm khởi động server |
| `config/` | Kết nối database |
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
