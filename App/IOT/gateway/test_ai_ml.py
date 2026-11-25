"""
test_ai_ml.py - Kiểm thử tự động cho module AI Isolation Forest trên Gateway (Issue #66 - Tiêu chí T09)
Khớp 100% CONTRACT mục 10 và TASKS_W3 mục 4.
Tác giả: Tuấn (TV2 - Gateway)
"""

import time
import unittest
from pathlib import Path

from ai_ml import check_anomaly_ml, load_model, reset_anomaly_state


class TestGatewayAIML(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Đảm bảo mô hình được nạp sẵn
        cls.model = load_model()
        if cls.model is None:
            # Nếu chưa train, tự động train nhanh một mô hình tạm thời
            from train_iforest import train
            cls.model = train()

    def setUp(self):
        reset_anomaly_state()

    def test_missing_sensor_keys_returns_none(self):
        """Khi Node A mất kết nối (thiếu key nhiệt độ, độ ẩm hoặc ánh sáng) -> trả về None."""
        # 1. Thiếu temperature
        res1 = check_anomaly_ml({"humidity": 50.0, "light": 1000, "ts": 1700000000000}, model=self.model)
        self.assertIsNone(res1)

        # 2. Thiếu humidity
        res2 = check_anomaly_ml({"temperature": 29.0, "light": 1000, "ts": 1700000000000}, model=self.model)
        self.assertIsNone(res2)

        # 3. Thiếu light
        res3 = check_anomaly_ml({"temperature": 29.0, "humidity": 50.0, "ts": 1700000000000}, model=self.model)
        self.assertIsNone(res3)

        # 4. Khi Node A offline (chỉ có key actuator của Node B)
        res_offline = check_anomaly_ml({"node_a_online": False, "fan_level": 1, "light_on": True}, model=self.model)
        self.assertIsNone(res_offline)

    def test_normal_classroom_sample_returns_none(self):
        """Mẫu dữ liệu phòng bình thường (29.3 độ C, 45% ẩm, 1000 lux lúc 10h sáng) -> không bắn sự kiện (None)."""
        normal_sample = {
            "ts": 1795058815430,  # Khoảng 10h sáng theo log thật
            "temperature": 29.3,
            "humidity": 45.9,
            "light": 1119,
        }
        event = check_anomaly_ml(normal_sample, model=self.model)
        self.assertIsNone(event)

    def test_abnormal_sample_triggers_event(self):
        """Mẫu vô lý/bất thường (48 độ C, 10% ẩm lúc 3 giờ sáng) -> bắn sự kiện anomaly_ml."""
        # 3:00 sáng UTC+7 -> epoch ms tương ứng
        ts_3am = 1764014400000  # 3:00 AM
        abnormal_sample = {
            "ts": ts_3am,
            "temperature": 48.0,
            "humidity": 10.0,
            "light": 3500,
        }
        event = check_anomaly_ml(abnormal_sample, model=self.model)

        self.assertIsNotNone(event)
        self.assertEqual(event["type"], "anomaly_ml")
        self.assertEqual(event["level"], "warning")
        self.assertEqual(event["ts"], ts_3am)
        self.assertIn("Nhiệt độ: 48.0°C", event["msg"])

    def test_report_once_until_recovered(self):
        """Chỉ bắn sự kiện 1 lần khi bắt đầu bất thường, không bắn liên tục mỗi 2s, khi hết thì cho báo lại."""
        ts = int(time.time() * 1000)
        abnormal_sample = {
            "ts": 1764014400000,  # 3:00 AM
            "temperature": 50.0,
            "humidity": 10.0,
            "light": 4000,
        }
        normal_sample = {
            "ts": 1795058815430,  # 10:26 AM (khớp phân phối giờ huấn luyện)
            "temperature": 29.3,
            "humidity": 45.9,
            "light": 1119,
        }

        # Lần 1: Bắt đầu bất thường -> Phải bắn sự kiện
        event1 = check_anomaly_ml(abnormal_sample, model=self.model)
        self.assertIsNotNone(event1)

        # Lần 2: Vẫn tiếp tục bất thường -> Không bắn lặp lại (trả về None)
        event2 = check_anomaly_ml(abnormal_sample, model=self.model)
        self.assertIsNone(event2)

        # Lần 3: Trở về bình thường -> Trả về None và reset trạng thái
        event3 = check_anomaly_ml(normal_sample, model=self.model)
        self.assertIsNone(event3)

        # Lần 4: Lại xảy ra đợt bất thường mới -> Bắn sự kiện mới
        event4 = check_anomaly_ml(abnormal_sample, model=self.model)
        self.assertIsNotNone(event4)


if __name__ == "__main__":
    unittest.main()
