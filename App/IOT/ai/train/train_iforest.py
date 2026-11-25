#!/usr/bin/env python3
"""
train_iforest.py - Huấn luyện mô hình Isolation Forest phát hiện bất thường môi trường trên Gateway (Issue #66 - Tiêu chí T09)
Khớp 100% CONTRACT.md (mục 10) và TASKS_W3.md (mục 4).
Tác giả: Tuấn (TV2 - Gateway)
"""

import json
import logging
from datetime import datetime, timezone, timedelta
from pathlib import Path
from typing import List, Tuple

import joblib
import numpy as np
from sklearn.ensemble import IsolationForest

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] [Train-IForest] %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger("train_iforest")

DEFAULT_MODEL_PATH = Path(__file__).resolve().parent.parent / "models" / "iforest.joblib"
DEFAULT_DATA_DIR = Path(__file__).resolve().parent.parent / "data"


def extract_features_from_sample(temp: float, hum: float, light: float, ts_ms: int) -> List[float]:
    """Trích xuất 4 đặc trưng: [temperature, humidity, light, hour_of_day] (UTC+7)."""
    dt = datetime.fromtimestamp(ts_ms / 1000.0, tz=timezone.utc) + timedelta(hours=7)
    hour_float = dt.hour + (dt.minute / 60.0)
    return [float(temp), float(hum), float(light), float(hour_float)]


def load_training_data(data_dir: Path) -> np.ndarray:
    """
    Đọc dữ liệu từ log MQTT (App/IOT/ai/data/mqtt_log/*.log) và file raw CSV.
    """
    samples = []

    # 1. Quét các file .log trong mqtt_log/
    log_dir = data_dir / "mqtt_log"
    if log_dir.is_dir():
        for log_file in log_dir.glob("*.log"):
            logger.info("Đang đọc dữ liệu từ: %s", log_file.name)
            try:
                # Đọc với utf-8 hoặc utf-16 (tùy theo PowerShell ghi)
                content = ""
                for enc in ("utf-8-sig", "utf-16", "utf-8"):
                    try:
                        content = log_file.read_text(encoding=enc)
                        break
                    except Exception:
                        continue

                for line in content.splitlines():
                    line = line.strip()
                    if not line or not line.startswith("{"):
                        continue
                    try:
                        data = json.loads(line)
                        if (
                            "temperature" in data
                            and "humidity" in data
                            and "light" in data
                            and "ts" in data
                        ):
                            feat = extract_features_from_sample(
                                data["temperature"],
                                data["humidity"],
                                data["light"],
                                data["ts"],
                            )
                            samples.append(feat)
                    except json.JSONDecodeError:
                        continue
            except Exception as e:
                logger.warning("Không thể đọc file %s: %s", log_file, e)

    # 2. Nếu thiếu dữ liệu log, bổ sung các mẫu môi trường phòng học chuẩn để mô hình ổn định
    if len(samples) < 100:
        logger.info("Số mẫu thực tế thấp (%d mẫu), sinh thêm mẫu đối chứng chuẩn...", len(samples))
        np.random.seed(42)
        base_time = int(datetime(2026, 11, 25, 8, 0, tzinfo=timezone.utc).timestamp() * 1000)
        for i in range(500):
            t = float(np.random.normal(29.0, 1.2))       # Nhiệt độ 27-31 độ C
            h = float(np.random.normal(55.0, 5.0))       # Độ ẩm 45-65%
            l = float(np.random.normal(1000.0, 200.0))   # Ánh sáng phòng 700-1300 lux
            ts = base_time + i * 2000
            samples.append(extract_features_from_sample(t, h, l, ts))

    return np.array(samples)


def train(data_dir: Path = DEFAULT_DATA_DIR, model_out: Path = DEFAULT_MODEL_PATH) -> IsolationForest:
    """Huấn luyện mô hình Isolation Forest và lưu ra file joblib."""
    logger.info("Bắt đầu nạp dữ liệu từ: %s", data_dir)
    X = load_training_data(data_dir)
    n_samples = len(X)
    logger.info("Tổng số mẫu huấn luyện: %d", n_samples)

    contamination = 0.05  # Tỉ lệ bất thường giả định 5%
    clf = IsolationForest(
        n_estimators=100,
        max_samples="auto",
        contamination=contamination,
        random_state=42,
    )
    clf.fit(X)

    preds = clf.predict(X)
    n_anomalies = int(np.sum(preds == -1))
    ratio = (n_anomalies / n_samples) * 100.0

    logger.info("Kết quả huấn luyện:")
    logger.info("  - Tổng mẫu      : %d", n_samples)
    logger.info("  - Mẫu bất thường : %d", n_anomalies)
    logger.info("  - Tỉ lệ bất thường: %.2f%% (Mục tiêu: ~%.1f%%)", ratio, contamination * 100)

    model_out.parent.mkdir(parents=True, exist_ok=True)
    joblib.dump(clf, model_out)
    logger.info("Đã lưu mô hình thành công vào: %s", model_out)

    return clf


if __name__ == "__main__":
    train()
