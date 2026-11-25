"""Kiểm model đã xuất (issue #30): chạy đúng file .tflite sẽ nạp lên Node A, trên PC.

Cách chạy (gốc repo, sau train_tinyml.py):
    python App/IOT/ai/train/check_model.py
    python App/IOT/ai/train/check_model.py --test-file mqtt_20261119_sang.csv

Kiểm 3 việc, in PASS/FAIL từng việc, output này dán vào PR:
    1. File .h chứa ĐÚNG các byte của file .tflite (đọc lại mảng trong .h, so từng byte)
       và hằng số chuẩn hóa trong .h khớp norm.json.
       Lý do: Node A chỉ thấy file .h. Sai 1 byte là GetModel lỗi hoặc ra số rác.
    2. Chạy .tflite trên tập test: in confusion matrix (dùng lại evaluate của train_tinyml.py).
       Lý do: kiểm cả bước chuyển Keras -> TFLite, không chỉ model Keras.
    3. Hai mẫu đại diện, lấy từ dữ liệu thật 19/11:
           (29.0, 45.0) phòng bình thường   -> score < ngưỡng
           (31.5, 95.0) đang áp tay / thổi  -> score >= ngưỡng
       Hai mẫu này cũng là mẫu sẽ chạy lại trên board ở lab, so số với PC.
       (Bảng (80, 20) / (25, 60) trong TASKS_W1 là dữ liệu giả của thầy, không dùng cho
       dữ liệu thật: độ ẩm 60 % cao hơn nền phòng lab ~45 %, model đúng sẽ báo bất thường.)
"""

import argparse
import json
import re
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from train_tinyml import (FEATURES, MODEL_DIR, RAW_DIR, THRESHOLD,  # noqa: E402
                          evaluate, load_csv, normalize)

SAMPLES = [((29.0, 45.0), 0), ((31.5, 95.0), 1)]  # (nhiệt, ẩm), nhãn mong đợi


def bytes_from_header(text):
    """Đọc lại mảng byte trong nội dung file .h.

    Đầu vào: text - nội dung file .h.  Đầu ra: bytes.
    Logic: lấy phần giữa '{' và '};' của mảng, rồi bắt mọi '0xHH'. Chỉ lấy trong mảng
    để không bắt nhầm số hex ở chỗ khác (nếu sau này thêm).
    """
    body = text[text.index("{") + 1: text.index("};")]
    return bytes(int(h, 16) for h in re.findall(r"0x([0-9a-fA-F]{2})", body))


def define_value(text, name):
    """Lấy giá trị số của '#define <name> <số>f' trong file .h. Không có -> None."""
    m = re.search(rf"#define\s+{name}\s+(-?[0-9.]+)f", text)
    return float(m.group(1)) if m else None


def make_interpreter(tflite_bytes):
    """Tạo TFLite interpreter trên PC từ bytes model.

    Ưu tiên ai_edge_litert (tên mới), không có thì dùng tf.lite.Interpreter
    (TensorFlow 2.x vẫn có, có thể in cảnh báo deprecated, bỏ qua được).
    """
    try:
        from ai_edge_litert.interpreter import Interpreter
    except ImportError:
        import tensorflow as tf
        Interpreter = tf.lite.Interpreter
    itp = Interpreter(model_content=tflite_bytes)
    itp.allocate_tensors()
    return itp


def run_tflite(itp, X):
    """Chạy model từng mẫu một, giống firmware (input [1, 2] float32, output [1, 1]).

    Đầu vào: X - mảng N x 2 ĐÃ chuẩn hóa.  Đầu ra: numpy N score 0..1.
    """
    inp = itp.get_input_details()[0]
    out = itp.get_output_details()[0]
    scores = []
    for row in np.asarray(X, dtype=np.float32):
        itp.set_tensor(inp["index"], row.reshape(1, 2))
        itp.invoke()
        scores.append(float(itp.get_tensor(out["index"])[0][0]))
    return np.array(scores)


def main():
    parser = argparse.ArgumentParser(description="Kiểm model TinyML đã xuất (#30)")
    parser.add_argument("--test-file", default="mqtt_20261119_sang2.csv")
    args = parser.parse_args()

    tflite_bytes = (MODEL_DIR / "tinyml_model.tflite").read_bytes()
    header = (MODEL_DIR / "tinyml_model.h").read_text(encoding="utf-8")
    norm = json.loads((MODEL_DIR / "norm.json").read_text(encoding="utf-8"))
    ok_all = True

    # 1. .h khớp .tflite và norm.json
    same = bytes_from_header(header) == tflite_bytes
    keys = {"TINYML_TEMP_MEAN": "temp_mean", "TINYML_TEMP_STD": "temp_std",
            "TINYML_HUM_MEAN": "hum_mean", "TINYML_HUM_STD": "hum_std",
            "TINYML_THRESHOLD": "threshold"}
    consts_ok = all(define_value(header, k) is not None and abs(define_value(header, k) - norm[v]) < 1e-5
                    for k, v in keys.items())
    print(f"[1] .h = .tflite ({len(tflite_bytes)} byte): {'PASS' if same else 'FAIL'}; "
          f"hằng số .h = norm.json: {'PASS' if consts_ok else 'FAIL'}")
    ok_all &= same and consts_ok

    itp = make_interpreter(tflite_bytes)

    # 2. tập test
    df = load_csv(RAW_DIR / args.test_file)
    print(f"[2] Chạy .tflite trên {args.test_file} ({len(df)} mẫu)")
    evaluate(df["label"].values, run_tflite(itp, normalize(df[FEATURES].values, norm)), THRESHOLD)

    # 3. mẫu đại diện
    for (t, h), expect in SAMPLES:
        s = run_tflite(itp, normalize([[t, h]], norm))[0]
        good = (s >= THRESHOLD) == bool(expect)
        ok_all &= good
        print(f"[3] ({t}, {h}) -> score {s:.3f}, mong đợi nhãn {expect}: {'PASS' if good else 'FAIL'}")

    print("KẾT QUẢ:", "PASS" if ok_all else "FAIL")
    sys.exit(0 if ok_all else 1)


if __name__ == "__main__":
    main()
