# FE-web

Giao diện web, viết bằng Next.js 14, React 18, TypeScript và Tailwind CSS.

Đây là sườn dự án: các trang mới là trang chờ. Phần chi tiết xem tại [docs/ISSUES.md](../../docs/ISSUES.md).

## Cài đặt và chạy

Cần Node.js 18 trở lên. Chạy BE-web trước.

```bash
cd App/FE-web
npm install
cp .env.example .env.local
npm run dev
```

Mở `http://localhost:3000`.

| Biến môi trường | Ý nghĩa | Mặc định |
|---|---|---|
| `NEXT_PUBLIC_API_URL` | Địa chỉ BE-web | `http://localhost:5000` |

## Cấu trúc thư mục

| Đường dẫn | Nội dung |
|---|---|
| `app/login`, `app/register` | Đăng nhập, đăng ký |
| `app/dashboard` | Trang tổng quan |
| `app/dashboard/create-room` | Tạo phòng |
| `app/dashboard/room/[id]` | Chi tiết phòng, điều khiển thiết bị |
| `components/` | Component dùng chung |
| `contexts/AuthContext.tsx` | Trạng thái đăng nhập |
| `lib/api.ts` | Axios client, tự gắn token |
