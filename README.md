# Assignment IoT HK261

Bài tập lớn môn Internet of Things, học kỳ 261.

```
.
├── App/
│   ├── BE-web/   # Backend web
│   ├── FE-web/   # Frontend web
│   └── IOT/      # Firmware, AI, cấu hình CoreIOT
└── Report/       # Báo cáo LaTeX
```

| Phần | Hướng dẫn |
|---|---|
| Ứng dụng | [App/README.md](App/README.md) |
| Backend | [App/BE-web/README.md](App/BE-web/README.md) |
| Frontend | [App/FE-web/README.md](App/FE-web/README.md) |
| IoT | [App/IOT/README.md](App/IOT/README.md) |
| Báo cáo | [Report/README.md](Report/README.md) |

## Chạy phần web

```bash
npm install
(cd App/BE-web && npm install && cp .env.example .env && npm run db:up && npm run db:migrate)
(cd App/FE-web && npm install && cp .env.example .env.local)
npm run dev
```

Cần cài Docker để chạy MySQL local. Xem thêm [App/BE-web/README.md](App/BE-web/README.md).

Danh sách công việc: [docs/ISSUES.md](docs/ISSUES.md).

Không commit mật khẩu, token hoặc API key. Dùng file `.env` hoặc `*.local.json`, đã được Git bỏ qua.
