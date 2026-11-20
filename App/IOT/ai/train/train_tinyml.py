"""Train model TinyML phát hiện bất thường nhiệt độ/độ ẩm (issue #30).

Cách chạy (ở thư mục gốc repo, đã activate .venv):
    python App/IOT/ai/train/train_tinyml.py
    python App/IOT/ai/train/train_tinyml.py --test-file mqtt_20261119_sang.csv

Đầu vào:
    Mọi file App/IOT/ai/data/raw/*.csv, mỗi dòng `temperature,humidity,label`,
    không có tiêu đề (do mqtt_to_csv.py / serial_to_csv.py tạo).

Đầu ra (App/IOT/ai/models/):
    tinyml_model.tflite  model để chạy thử trên PC (check_model.py)
    tinyml_model.h       mảng byte model + hằng số chuẩn hóa, Node A #include (W4)
    norm.json            mean/std dùng để chuẩn hóa, để người khác đọc lại

Khác script mẫu của thầy (08_Code/TinyML/TFL_For_MCU.py), và vì sao:
    1. Chia train/test THEO FILE (buổi thu), không chia ngẫu nhiên từng dòng.
       Hai mẫu liền nhau cách 2 s gần như giống hệt nhau, chia ngẫu nhiên thì tập test
       "đã thấy" gần hết dữ liệu -> accuracy đẹp nhưng không nói gì về buổi thu mới.
    2. Chuẩn hóa input (x - mean) / std, mean/std tính trên tập TRAIN.
       Nhiệt độ ~29 và độ ẩm ~45..100 lệch thang đo nhau, model nhỏ học chậm và lệch.
       Firmware phải chuẩn hóa y hệt, nên mean/std được ghi vào file .h.
    3. class_weight: dữ liệu 19/11 có 233 mẫu nhãn 0 và 750 mẫu nhãn 1, không cân bằng.
    4. Tên mảng trong .h giữ `dht_anomaly_model_tflite` giống model của thầy, để code
       TinyML mẫu trên ESP32 (tflite::GetModel(dht_anomaly_model_tflite)) không phải sửa.
"""

import argparse
import json
from pathlib import Path

import numpy as np
import pandas as pd

# Thư mục gốc phần AI: App/IOT/ai (file này nằm ở App/IOT/ai/train/)
AI_DIR = Path(__file__).resolve().parent.parent
RAW_DIR = AI_DIR / "data" / "raw"
MODEL_DIR = AI_DIR / "models"

COLS = ["temp", "humidity", "label"]  # thứ tự cột trong CSV, giống sensor_data.csv của thầy
FEATURES = ["temp", "humidity"]        # thứ tự input của model: [0] nhiệt độ, [1] độ ẩm
THRESHOLD = 0.5                        # score >= THRESHOLD -> anomaly = 1
SEED = 42                              # cố định để chạy lại ra cùng kết quả


def load_csv(path):
    """Đọc 1 file CSV dữ liệu thô.

    Đầu vào:  path - đường dẫn file .csv, không có dòng tiêu đề.
    Đầu ra:   DataFrame 3 cột temp, humidity, label.
    Logic:    pd.read_csv với names=COLS vì file không có tiêu đề (dòng đầu là dữ liệu).
              Nếu bỏ names, pandas lấy mẫu đầu tiên làm tên cột và mất 1 mẫu.
    """
    return pd.read_csv(path, names=COLS)


def split_by_file(paths, test_name):
    """Tách danh sách file CSV thành tập train và tập test.

    Đầu vào:
        paths     - list đường dẫn file CSV (str hoặc Path)
        test_name - TÊN file (không có thư mục) dùng làm tập test, vd "mqtt_20261119_sang2.csv"
    Đầu ra:
        (train_df, test_df) - 2 DataFrame, mỗi cái là các file tương ứng nối lại
        (pd.concat(..., ignore_index=True)), đọc bằng load_csv.
    Lỗi:
        Không có file nào tên test_name      -> raise ValueError
        Không còn file nào cho train         -> raise ValueError
    """
    train_paths = [p for p in paths if Path(p).name != test_name]
    test_paths = [p for p in paths if Path(p).name == test_name]

    if not test_paths:
        raise ValueError(f"Không thấy file test {test_name}")
    if not train_paths:
        raise ValueError("Không còn file nào để train")

    train_df = pd.concat([load_csv(p) for p in train_paths], ignore_index=True)
    test_df = pd.concat([load_csv(p) for p in test_paths], ignore_index=True)
    return train_df, test_df


def compute_norm(X):
    """Tính mean và std từng cột của X (chỉ gọi với tập TRAIN).

    Đầu vào:  X - mảng N x 2, cột 0 nhiệt độ, cột 1 độ ẩm.
    Đầu ra:   dict 4 khóa, giá trị kiểu float của Python (để json.dump được):
                  {"temp_mean": ..., "temp_std": ..., "hum_mean": ..., "hum_std": ...}
    Biên:     std của 1 cột < 1e-6 (cột hằng số) -> dùng 1.0, tránh chia cho 0 ở normalize.
    """
    X = np.asarray(X, dtype=np.float64)
    mean = np.mean(X, axis=0)
    std = np.std(X, axis=0)
    std = np.where(std < 1e-6, 1.0, std)
    return {
        "temp_mean": float(mean[0]),
        "temp_std": float(std[0]),
        "hum_mean": float(mean[1]),
        "hum_std": float(std[1]),
    }


def normalize(X, norm):
    """Chuẩn hóa X bằng mean/std trong norm: (x - mean) / std, từng cột.

    Đầu vào:  X - mảng N x 2 (list hoặc numpy); norm - dict từ compute_norm.
    Đầu ra:   numpy float32 cùng shape N x 2 (TFLite input là float32).
    """
    X = np.asarray(X, dtype=np.float32)
    mean = np.array([norm["temp_mean"], norm["hum_mean"]], dtype=np.float32)
    std = np.array([norm["temp_std"], norm["hum_std"]], dtype=np.float32)
    return (X - mean) / std


def class_weights(y):
    """Trọng số mỗi nhãn để lớp ít mẫu không bị model bỏ qua.

    Công thức (giống sklearn class_weight="balanced"):  w_c = N / (2 * N_c)
    Đầu vào:  y - mảng nhãn 0/1.
    Đầu ra:   {0: w0, 1: w1}
    Lỗi:      thiếu hẳn một nhãn (N_c = 0) -> raise ValueError
    """
    y = np.asarray(y)
    n0 = int(np.sum(y == 0))
    n1 = int(np.sum(y == 1))
    if n0 == 0 or n1 == 0:
        raise ValueError("Dữ liệu thiếu một nhãn, không train được")
    n = len(y)
    return {0: float(n / (2 * n0)), 1: float(n / (2 * n1))}


def build_model():
    """Model giống hệt script của thầy (TFL_For_MCU.py): 2 input -> Dense 8 ReLU -> Dense 1 sigmoid.

    Đầu ra:  tf.keras.Model đã compile.
    Logic:
        Dense(8, relu): 2*8 + 8 = 24 tham số. Dense(1, sigmoid): 8 + 1 = 9 tham số.
        Tổng 33 tham số, file .tflite khoảng 1.5 KB, chạy trên ESP32 ~0.1 ms.
        sigmoid cho ra 0..1 = anomaly_score (CONTRACT mục 3, 5).
        binary_crossentropy + adam: cặp chuẩn cho phân loại 2 lớp.
    Vì sao giữ nguyên kiến trúc: model thầy đã chạy được trên board (lab 10/11, 142 us/lần)
    với TensorFlowLite_ESP32. Giữ cùng loại op (FULLY_CONNECTED) để không gặp lỗi
    "op không hỗ trợ" của thư viện TFLite cũ trên ESP32.
    """
    import tensorflow as tf  # import ở đây để pytest không phải nạp TensorFlow (chậm)

    model = tf.keras.Sequential([
        tf.keras.layers.Input(shape=(2,)),
        tf.keras.layers.Dense(8, activation="relu"),
        tf.keras.layers.Dense(1, activation="sigmoid"),
    ])
    model.compile(loss="binary_crossentropy", optimizer="adam", metrics=["accuracy"])
    return model


def to_tflite(model):
    """Đổi model Keras sang byte .tflite, giống script của thầy.

    Đầu vào: model Keras đã train.  Đầu ra: bytes.
    Logic: TFLiteConverter.from_keras_model + Optimize.DEFAULT như thầy. Model nhỏ
    (dưới 1024 trọng số mỗi lớp) nên converter KHÔNG lượng tử hóa, input/output vẫn float32,
    op vẫn FULLY_CONNECTED version 1 giống model thầy (đã kiểm 20/11 với TF 2.21).
    """
    import tensorflow as tf

    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    return converter.convert()


def to_c_header(tflite_bytes, norm, threshold, array_name="dht_anomaly_model_tflite"):
    """Tạo nội dung file .h chứa model và hằng số chuẩn hóa.

    Đầu vào:
        tflite_bytes - bytes từ to_tflite
        norm         - dict từ compute_norm
        threshold    - ngưỡng anomaly (float)
        array_name   - tên mảng, mặc định giống model thầy
    Đầu ra: chuỗi (str) nội dung file .h, PHẢI có đủ, theo đúng thứ tự:
        1. "#pragma once" và "#include <stdint.h>"
        2. 5 hằng số, số thực 6 chữ số thập phân, có hậu tố f (float, không phải double):
               #define TINYML_TEMP_MEAN 29.000000f
               #define TINYML_TEMP_STD 1.000000f
               #define TINYML_HUM_MEAN 50.000000f
               #define TINYML_HUM_STD 10.000000f
               #define TINYML_THRESHOLD 0.500000f
        3. Mảng byte, có alignas(16), mỗi byte dạng 0x1c, 12 byte một dòng:
               alignas(16) const unsigned char dht_anomaly_model_tflite[] = {
                 0x1c, 0x00, 0x00, 0x00, 0x54, 0x46, 0x4c, 0x33, 0x14, 0x00, 0x20, 0x00,
                 ...
               };
        4. Độ dài mảng:
               const unsigned int dht_anomaly_model_tflite_len = 1552;

    Vì sao:
        - alignas(16): TFLite Micro đọc model trực tiếp từ flash theo kiểu flatbuffer,
          cần địa chỉ đầu mảng căn lề. Script thầy thiếu dòng này, chạy được là do may.
        - hậu tố f: 29.0 không có f là double, ESP32-S3 tính double bằng phần mềm (chậm).
        - _len: firmware in ra log để so với kích thước file .tflite, biết đã nạp đúng model.
    """
    lines = [
        "#pragma once",
        "#include <stdint.h>",
        "",
        f"#define TINYML_TEMP_MEAN {norm['temp_mean']:.6f}f",
        f"#define TINYML_TEMP_STD {norm['temp_std']:.6f}f",
        f"#define TINYML_HUM_MEAN {norm['hum_mean']:.6f}f",
        f"#define TINYML_HUM_STD {norm['hum_std']:.6f}f",
        f"#define TINYML_THRESHOLD {threshold:.6f}f",
        "",
        f"alignas(16) const unsigned char {array_name}[] = {{",
    ]

    for i in range(0, len(tflite_bytes), 12):
        chunk = tflite_bytes[i:i + 12]
        hex_bytes = ", ".join(f"0x{b:02x}" for b in chunk)
        comma = "," if i + 12 < len(tflite_bytes) else ""
        lines.append(f"  {hex_bytes}{comma}")

    lines.append("};")
    lines.append(f"const unsigned int {array_name}_len = {len(tflite_bytes)};")

    return "\n".join(lines) + "\n"

def evaluate(y_true, score, threshold):
    """In confusion matrix và các chỉ số trên tập test.

    Đầu vào:  y_true - nhãn thật; score - output model 0..1; threshold - ngưỡng.
    Đầu ra:   dict {"accuracy", "recall_1", "false_alarm"} để main in và ghi vào PR.
    Logic:
        y_pred = score >= threshold.
        confusion_matrix: hàng = nhãn thật, cột = dự đoán: [[TN, FP], [FN, TP]].
        recall_1   = TP / (TP + FN): trong các lần bất thường thật, bắt được bao nhiêu.
        false_alarm = FP / (FP + TN): phòng bình thường mà báo nhầm bao nhiêu.
        Với hệ thống cảnh báo, 2 số này quan trọng hơn accuracy chung.
    """
    from sklearn.metrics import confusion_matrix

    y_true = np.asarray(y_true).astype(int)
    y_pred = (np.asarray(score).ravel() >= threshold).astype(int)
    tn, fp, fn, tp = confusion_matrix(y_true, y_pred, labels=[0, 1]).ravel()
    print("Confusion matrix (hàng: nhãn thật 0/1, cột: dự đoán 0/1):")
    print(f"  [[{tn:4d} {fp:4d}]\n   [{fn:4d} {tp:4d}]]")
    result = {
        "accuracy": float((tp + tn) / len(y_true)),
        "recall_1": float(tp / (tp + fn)) if (tp + fn) else 0.0,
        "false_alarm": float(fp / (fp + tn)) if (fp + tn) else 0.0,
    }
    print(f"accuracy {result['accuracy']:.3f}  recall nhãn 1 {result['recall_1']:.3f}  "
          f"báo nhầm {result['false_alarm']:.3f}")
    return result


def main():
    """Luồng chính: đọc dữ liệu -> chia theo file -> chuẩn hóa -> train -> đánh giá -> xuất file.

    Mọi bước dùng các hàm ở trên, main chỉ nối chúng lại và in kết quả để dán vào PR.
    """
    parser = argparse.ArgumentParser(description="Train model TinyML (#30)")
    parser.add_argument("--test-file", default="mqtt_20261119_sang2.csv",
                        help="tên file trong data/raw dùng làm tập test (mặc định buổi sang2 19/11)")
    parser.add_argument("--epochs", type=int, default=100)
    args = parser.parse_args()

    import tensorflow as tf

    tf.keras.utils.set_random_seed(SEED)  # cố định numpy/python/tf random -> chạy lại ra cùng số

    paths = sorted(RAW_DIR.glob("*.csv"))
    print(f"Dữ liệu: {len(paths)} file trong {RAW_DIR}")
    train_df, test_df = split_by_file(paths, args.test_file)
    print(f"Train: {len(train_df)} mẫu (nhãn 0: {(train_df.label == 0).sum()}, nhãn 1: {(train_df.label == 1).sum()})")
    print(f"Test : {len(test_df)} mẫu (nhãn 0: {(test_df.label == 0).sum()}, nhãn 1: {(test_df.label == 1).sum()}) <- {args.test_file}")

    norm = compute_norm(train_df[FEATURES].values)  # CHỈ tập train, không nhìn tập test
    X_train = normalize(train_df[FEATURES].values, norm)
    X_test = normalize(test_df[FEATURES].values, norm)
    y_train = train_df["label"].values
    y_test = test_df["label"].values
    weights = class_weights(y_train)
    print(f"norm: {norm}")
    print(f"class_weight: {weights}")

    model = build_model()
    model.fit(X_train, y_train, epochs=args.epochs, batch_size=32,
              class_weight=weights, verbose=0)

    score = model.predict(X_test, verbose=0).ravel()
    evaluate(y_test, score, THRESHOLD)

    MODEL_DIR.mkdir(parents=True, exist_ok=True)
    tflite_bytes = to_tflite(model)
    (MODEL_DIR / "tinyml_model.tflite").write_bytes(tflite_bytes)
    (MODEL_DIR / "tinyml_model.h").write_text(to_c_header(tflite_bytes, norm, THRESHOLD), encoding="utf-8")
    (MODEL_DIR / "norm.json").write_text(json.dumps({**norm, "threshold": THRESHOLD}, indent=2), encoding="utf-8")
    print(f"Đã ghi {MODEL_DIR}: tinyml_model.tflite ({len(tflite_bytes)} byte), tinyml_model.h, norm.json")


if __name__ == "__main__":
    main()