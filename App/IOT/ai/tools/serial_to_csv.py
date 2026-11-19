"""Đổi log Serial của Node A thành CSV train TinyML.

Cách dùng (chạy ở thư mục gốc repo):
    python App/IOT/ai/tools/serial_to_csv.py App/IOT/ai/data/serial_log/a_minh_20261119_0915.log

Đầu vào:
  1. File .log: lưu từ `pio device monitor -f time`. Mỗi dòng số đo có dạng
         09:15:02.123 > [A] T: 27.4 H: 61.2 light: 812 presence: 1 valid: 1 dht_fail: 0
     Phần "09:15:02.123 > " do filter `time` của PlatformIO thêm vào.
     Các dòng khác (ch:, PKT_CONFIG, log boot) bị bỏ qua.
  2. File .txt CÙNG TÊN, cùng thư mục (a_minh_20261119_0915.txt): mỗi dòng là
     một khoảng thời gian có tác động lên cảm biến (nhãn 1):
         09:40 09:44
         10:02:30 10:06
     Giờ ghi theo đồng hồ máy tính (cùng đồng hồ với log). Dòng bắt đầu bằng #
     là ghi chú. Không có file .txt thì mọi mẫu là nhãn 0.

Đầu ra:
  App/IOT/ai/data/raw/serial_<tên log>.csv, mỗi dòng `temperature,humidity,label`,
  KHÔNG có dòng tiêu đề, giống sensor_data.csv của thầy và CSV của #48,
  để script train đọc chung bằng pd.read_csv(..., names=[...]).

Logic gán nhãn:
  - Bỏ mẫu valid: 0 (DHT20 đọc lỗi, số đo là giá trị cũ).
  - Mẫu nằm trong một khoảng của file .txt -> nhãn 1.
  - Mẫu nằm trong `--cooldown` giây SAU khi một khoảng kết thúc -> BỎ.
    Lý do: bỏ tay ra thì cảm biến cần vài phút mới về nhiệt độ phòng.
    Gán số đo đang nguội là nhãn 0 thì model học sai ranh giới; gán nhãn 1 cũng sai.
  - Còn lại -> nhãn 0.
"""

import argparse
import re
import sys
from pathlib import Path

# Giờ ở đầu dòng (do filter time thêm), rồi đúng format printf ở node_a/src/main.cpp.
# Bắt lỏng phần giữa (".*?") để không gãy nếu filter đổi dấu phân cách.
LINE_RE = re.compile(
    r"^(\d{1,2}):(\d{2}):(\d{2})(?:\.\d+)?.*?\[A\] T: (-?\d+(?:\.\d+)?) H: (-?\d+(?:\.\d+)?)"
    r" light: \d+ presence: \d+ valid: (\d)"
)


def to_sec(text):
    """'HH:MM' hoặc 'HH:MM:SS' -> số giây tính từ 00:00. Sai format thì báo lỗi rõ dòng nào."""
    parts = [int(p) for p in text.split(":")]
    if len(parts) == 2:
        parts.append(0)
    if len(parts) != 3:
        raise ValueError(f"giờ sai format: {text!r} (cần HH:MM hoặc HH:MM:SS)")
    h, m, s = parts
    return h * 3600 + m * 60 + s


def read_windows(txt_path):
    """Đọc file .txt -> list (bắt đầu, kết thúc) tính bằng giây. Không có file -> list rỗng."""
    if not txt_path.exists():
        return []
    windows = []
    for no, raw in enumerate(txt_path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        fields = line.split()
        if len(fields) != 2:
            sys.exit(f"{txt_path}:{no}: cần 2 mốc giờ 'bắt_đầu kết_thúc', đang có: {raw!r}")
        start, end = to_sec(fields[0]), to_sec(fields[1])
        if end <= start:
            sys.exit(f"{txt_path}:{no}: giờ kết thúc phải sau giờ bắt đầu: {raw!r}")
        windows.append((start, end))
    return windows


def label_of(t, windows, cooldown):
    """Trả 1, 0, hoặc None (= bỏ mẫu) theo logic ở đầu file."""
    for start, end in windows:
        if start <= t <= end:
            return 1
    for start, end in windows:
        if end < t <= end + cooldown:
            return None
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log", type=Path, help="file .log từ pio device monitor -f time")
    ap.add_argument("--out-dir", type=Path, default=Path("App/IOT/ai/data/raw"))
    ap.add_argument("--cooldown", type=int, default=180, help="giây bỏ sau mỗi khoảng nhãn 1 (mặc định 180)")
    args = ap.parse_args()

    windows = read_windows(args.log.with_suffix(".txt"))
    rows, invalid, dropped = [], 0, 0
    # errors="replace": log Serial hay có byte rác lúc reset board, không để script chết vì nó.
    for line in args.log.read_text(encoding="utf-8", errors="replace").splitlines():
        m = LINE_RE.search(line)
        if not m:
            continue
        hh, mm, ss, temp, hum, valid = m.groups()
        if valid != "1":
            invalid += 1
            continue
        label = label_of(int(hh) * 3600 + int(mm) * 60 + int(ss), windows, args.cooldown)
        if label is None:
            dropped += 1
            continue
        rows.append(f"{float(temp):.1f},{float(hum):.1f},{label}")

    if not rows:
        sys.exit("Không có mẫu nào. Kiểm tra log có dòng '[A] T: ...' và được lưu bằng -f time.")

    args.out_dir.mkdir(parents=True, exist_ok=True)
    out = args.out_dir / f"serial_{args.log.stem}.csv"
    out.write_text("\n".join(rows) + "\n", encoding="utf-8")
    n1 = sum(r.endswith(",1") for r in rows)
    print(f"{out}: {len(rows)} mẫu (nhãn 0: {len(rows) - n1}, nhãn 1: {n1}); "
          f"bỏ {invalid} mẫu valid=0, {dropped} mẫu đang nguội; {len(windows)} khoảng nhãn 1")


if __name__ == "__main__":
    main()
