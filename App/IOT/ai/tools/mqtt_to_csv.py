"""Đổi log MQTT room1/telemetry thành CSV train TinyML.

Dùng khi thu dữ liệu qua MQTT thay cho Serial (Serial Node A hay đứt trên một số laptop).

Cách thu (laptop đang chạy broker, Node B đã nối MQTT):
    mosquitto_sub -h localhost -t room1/telemetry > D:\\tinyml_data\\mqtt_<YYYYMMDD>_<buổi>.log

Cách dùng (chạy ở thư mục gốc repo):
    python App/IOT/ai/tools/mqtt_to_csv.py App/IOT/ai/data/mqtt_log/mqtt_20261119_sang.log

Đầu vào:
  1. File .log: mỗi dòng là 1 JSON telemetry của Node B (CONTRACT mục 5), vd
         {"ts":1795058815430,"seq":192,"temperature":29.3,"humidity":45.9,...}
     Không có tên topic ở đầu dòng (mosquitto_sub không có -v). Nếu có (chạy với -v)
     thì script tự bỏ phần topic.
     Encoding: PowerShell 5 ghi file bằng `>` theo UTF-16, PowerShell 7 / cmd ghi UTF-8.
     Script tự nhận ra theo BOM, không cần chuyển đổi tay.
  2. File .txt CÙNG TÊN, cùng thư mục: mỗi dòng là một khoảng có tác động (nhãn 1),
     giờ Việt Nam (UTC+7), giống file .txt của serial_to_csv.py:
         10:30:52 10:35:39
     Dòng bắt đầu bằng # là ghi chú.

Đầu ra:
  App/IOT/ai/data/raw/mqtt_<tên log>.csv, mỗi dòng `temperature,humidity,label`,
  không có tiêu đề, cùng format với serial_to_csv.py.

Khác với log Serial, cần biết:
  - `ts` là giờ NTP của Node B (epoch ms, UTC). Script cộng 7 giờ để so với file .txt.
  - `temperature` trong telemetry là TRUNG BÌNH 2 lần đo gần nhất (Node B tính,
    control.cpp control_on_node_a), không phải số đo thô như trên Serial. Chênh lệch
    nhỏ (nhiệt độ đổi chậm, 2 mẫu cách nhau 2 s) nhưng người train cần biết khi so với Serial.
  - Mẫu DHT20 lỗi (valid = 0) Node A không gửi, nên log MQTT không có mẫu lỗi.
  - Khi Node A mất, Node B vẫn gửi telemetry nhưng không có key temperature/humidity
    (node_a_online = false). Script bỏ các dòng này.
  - Node B gửi telemetry mỗi khi nhận 1 gói ESP-NOW, nên mỗi dòng là 1 mẫu mới.
    Không lọc trùng theo `seq` vì `seq` quay về 0 mỗi lần Node A khởi động lại.
    Lọc trùng theo `ts` (2 dòng cùng ts là cùng 1 bản tin).

Logic gán nhãn giống serial_to_csv.py:
  - Mẫu nằm trong một khoảng của file .txt -> nhãn 1.
  - Mẫu nằm trong `--cooldown` giây SAU khi một khoảng kết thúc -> BỎ.
    Mặc định 300 s (serial_to_csv.py dùng 180 s). Lý do: số đo 19/11 cho thấy sau
    khi áp tay 4 phút, độ ẩm về nền sau khoảng 1 phút nhưng nhiệt độ còn cao hơn
    nền khoảng 1 °C ở phút thứ 3 và khoảng 0.5 °C ở phút thứ 5.
  - Còn lại -> nhãn 0.
"""

import argparse
import json
import sys
from pathlib import Path

VN_OFFSET_S = 7 * 3600  # Giờ Việt Nam = UTC + 7


def to_sec(text):
    """'HH:MM' hoặc 'HH:MM:SS' -> số giây tính từ 00:00. Sai format thì báo lỗi rõ."""
    parts = [int(p) for p in text.split(":")]
    if len(parts) == 2:
        parts.append(0)
    if len(parts) != 3:
        raise ValueError(f"giờ sai format: {text!r} (cần HH:MM hoặc HH:MM:SS)")
    h, m, s = parts
    return h * 3600 + m * 60 + s


def read_windows(txt_path):
    """Đọc file .txt -> list (bắt đầu, kết thúc) tính bằng giây trong ngày. Không có file -> rỗng."""
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


def read_text_any(path):
    """Đọc file text, tự nhận UTF-16 (có BOM, do PowerShell 5 ghi) hoặc UTF-8.

    Vào: đường dẫn file. Ra: chuỗi.
    BOM là 2 byte đầu FF FE (UTF-16 LE) hoặc FE FF (UTF-16 BE); không có thì coi là UTF-8.
    """
    raw = path.read_bytes()
    if raw[:2] in (b"\xff\xfe", b"\xfe\xff"):
        return raw.decode("utf-16")
    return raw.decode("utf-8-sig", errors="replace")


def parse_line(line):
    """1 dòng log -> dict JSON, hoặc None nếu dòng rỗng/hỏng.

    Chấp nhận cả dạng có tên topic ở đầu ('room1/telemetry {...}', khi chạy mosquitto_sub -v).
    """
    line = line.strip()
    if not line:
        return None
    if not line.startswith("{"):
        brace = line.find("{")
        if brace < 0:
            return None
        line = line[brace:]
    try:
        return json.loads(line)
    except json.JSONDecodeError:
        return None


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
    ap.add_argument("log", type=Path, help="file .log từ mosquitto_sub -t room1/telemetry")
    ap.add_argument("--out-dir", type=Path, default=Path("App/IOT/ai/data/raw"))
    ap.add_argument("--cooldown", type=int, default=300, help="giây bỏ sau mỗi khoảng nhãn 1 (mặc định 300)")
    args = ap.parse_args()

    windows = read_windows(args.log.with_suffix(".txt"))
    rows, seen_ts = [], set()
    broken = no_sensor = dup = dropped = 0
    for line in read_text_any(args.log).splitlines():
        if not line.strip():
            continue
        j = parse_line(line)
        if j is None:
            broken += 1
            continue
        # Node A mất hoặc chưa có gói: không có số đo, bỏ.
        if j.get("node_a_online") is not True or "temperature" not in j or "humidity" not in j or "ts" not in j:
            no_sensor += 1
            continue
        if j["ts"] in seen_ts:
            dup += 1
            continue
        seen_ts.add(j["ts"])
        # epoch ms (UTC) -> giây trong ngày theo giờ VN. Không có buổi thu nào qua nửa đêm.
        t = (j["ts"] // 1000 + VN_OFFSET_S) % 86400
        label = label_of(t, windows, args.cooldown)
        if label is None:
            dropped += 1
            continue
        rows.append(f"{float(j['temperature']):.1f},{float(j['humidity']):.1f},{label}")

    if not rows:
        sys.exit("Không có mẫu nào. Kiểm tra file có JSON telemetry chứa temperature/humidity.")

    args.out_dir.mkdir(parents=True, exist_ok=True)
    out = args.out_dir / f"{args.log.stem}.csv"
    out.write_text("\n".join(rows) + "\n", encoding="utf-8")
    n1 = sum(r.endswith(",1") for r in rows)
    print(f"{out}: {len(rows)} mẫu (nhãn 0: {len(rows) - n1}, nhãn 1: {n1}); "
          f"bỏ {no_sensor} dòng không có số đo, {dup} dòng trùng, {broken} dòng hỏng, "
          f"{dropped} mẫu đang nguội; {len(windows)} khoảng nhãn 1")


if __name__ == "__main__":
    main()
