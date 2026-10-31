# Report

Báo cáo LaTeX theo file mẫu của môn học.

## Biên dịch

Tải thư mục này lên Overleaf, chọn compiler **pdfLaTeX** và file chính `main.tex`.

Trên máy cá nhân cần TeX Live có gói tiếng Việt (`texlive-lang-other`):

```bash
cd Report
latexmk -pdf -outdir=build main.tex
```

File nộp đặt tên theo quy định: `[Mã lớp]_[Mã nhóm]_BaoCaoIoT.pdf`.

## Cấu trúc

| Đường dẫn | Nội dung |
|---|---|
| `main.tex` | File chính, khai báo tên đề tài, lớp, nhóm |
| `chapters/00-front.tex` | Bìa, thông tin nộp bài, phân công, bảng tự đánh giá, viết tắt |
| `chapters/01..06-*.tex` | Chương 1 đến Chương 6 |
| `chapters/references.tex` | Tài liệu tham khảo chuẩn IEEE, tối thiểu 5 mục |
| `chapters/appendix.tex` | Phụ lục, khai báo phạm vi dùng công cụ AI |
| `figures/` | Hình ảnh |

Các chỗ cần điền được đánh dấu màu đỏ bằng lệnh `\todo{...}`.
