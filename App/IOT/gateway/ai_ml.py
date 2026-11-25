#!/usr/bin/env python3
"""
ai_ml.py - Module phát hiện bất thường môi trường bằng Isolation Forest trên Gateway (Issue #66 - Tiêu chí T09)
Khớp 100% CONTRACT.md (mục 10) và TASKS_W3.md (mục 4).
Tác giả: Tuấn (TV2 - Gateway)
"""

import logging
import time
from datetime import datetime, timezone, timedelta
from pathlib import Path
from typing import Any, Dict, Optional, Union

import joblib

logger = logging.getLogger("gateway_ai_ml")

DEFAULT_MODEL_FILE = (
    Path(__file__).resolve().parent.parent / "ai" / "models" / "iforest.joblib"
)

# Trạng thái theo dõi bất thường: Chỉ bắn sự kiện một lần khi bắt đầu bất thường, hết thì cho phép bắn lại
_is_currently_anomaly: bool = False
_loaded_model = None


def load_model(path: Optional[Union[str, Path]] = None):
    """Nạp mô hình Isolation Forest từ file joblib."""
    global _loaded_model
    target_path = Path(path).resolve() if path else DEFAULT_MODEL_FILE
    if not target_path.is_file():
        logger.warning("Chưa tìm thấy file mô hình tại %s. Đang nạp None.", target_path)
        _loaded_model = None
        return None

    try:
        _loaded_model = joblib.load(target_path)
        logger.info("Đã nạp mô hình Isolation Forest từ: %s", target_path)
        return _loaded_model
    except Exception as e:
        logger.error("Lỗi khi nạp mô hình %s: %s", target_path, e)
        _loaded_model = None
        return None


def reset_anomaly_state():
    """Reset cờ trạng thái bất thường (dùng cho unit test hoặc khi khởi động lại)."""
    global _is_currently_anomaly
    _is_currently_anomaly = False


def check_anomaly_ml(sample: Dict[str, Any], model=None) -> Optional[Dict[str, Any]]:
    """
    Kiểm tra bất thường môi trường bằng học máy Isolation Forest.
    - Đầu vào: bản tin telemetry local (room1/telemetry) có chứa temperature, humidity, light, ts.
    - Điều kiện bỏ qua: thiếu một trong các key cảm biến (vd khi Node A mất kết nối) -> trả về None.
    - Đầu ra: Bản tin sự kiện room1/event đúng CONTRACT mục 10:
        {"ts": ..., "type": "anomaly_ml", "level": "warning", "msg": "..."}
      hoặc None nếu bình thường / đã báo rồi.
    """
    global _is_currently_anomaly, _loaded_model

    clf = model or _loaded_model
    if clf is None:
        clf = load_model()
        if clf is None:
            return None

    # 1. Kiểm tra đủ key cảm biến (Node A mất thì bỏ qua)
    temp = sample.get("temperature")
    hum = sample.get("humidity")
    light = sample.get("light")

    if temp is None or hum is None or light is None:
        return None

    try:
        temp_val = float(temp)
        hum_val = float(hum)
        light_val = float(light)
    except (ValueError, TypeError):
        return None

    # 2. Trích xuất giờ trong ngày (UTC+7)
    ts_ms = sample.get("ts")
    if ts_ms is not None:
        try:
            dt = datetime.fromtimestamp(float(ts_ms) / 1000.0, tz=timezone.utc) + timedelta(hours=7)
            hour_val = dt.hour + (dt.minute / 60.0)
        except Exception:
            hour_val = 12.0
    else:
        now_dt = datetime.now()
        hour_val = now_dt.hour + (now_dt.minute / 60.0)

    # 3. Dự đoán bất thường bằng mô hình Isolation Forest
    try:
        features = [[temp_val, hum_val, light_val, hour_val]]
        pred = clf.predict(features)[0]  # 1: bình thường, -1: bất thường
    except Exception as e:
        logger.error("Lỗi khi dự đoán Isolation Forest: %s", e)
        return None

    # 4. Quản lý trạng thái thông báo (chỉ bắn 1 lần)
    ts_event = int(ts_ms) if ts_ms is not None else int(time.time() * 1000)

    if pred == -1:
        if not _is_currently_anomaly:
            _is_currently_anomaly = True
            event = {
                "ts": ts_event,
                "type": "anomaly_ml",
                "level": "warning",
                "msg": (
                    f"Bất thường môi trường phát hiện bởi Isolation Forest "
                    f"(Nhiệt độ: {temp_val:.1f}°C, Độ ẩm: {hum_val:.1f}%, Ánh sáng: {int(light_val)} lux, Giờ: {hour_val:.1f}h)"
                ),
            }
            logger.warning("AI Gateway phát hiện bất thường: %s", event["msg"])
            return event
        else:
            # Đang trong đợt bất thường, không báo lặp lại mỗi chu kỳ
            return None
    else:
        # Trở về trạng thái bình thường -> giải phóng cờ để lần sau có thể báo tiếp
        if _is_currently_anomaly:
            logger.info("Chỉ số môi trường đã trở về trạng thái bình thường.")
            _is_currently_anomaly = False
        return None
