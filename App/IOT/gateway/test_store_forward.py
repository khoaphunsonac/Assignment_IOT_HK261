"""
test_store_forward.py - Kiểm thử tự động cho module lưu tạm telemetry và gửi bù (Issue #64 - Tiêu chí T15)
Khớp 100% CONTRACT mục 11.2 và TASKS_W3 mục 4.
Tác giả: Tuấn (TV2 - Gateway)
"""

import json
import tempfile
import time
import unittest
from pathlib import Path

from bridge import GatewayBridge, split_telemetry, DEVICE_NODE_A, DEVICE_NODE_B
from store_forward import StoreForwardBuffer


class TestStoreForward(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.buffer_file = Path(self.temp_dir.name) / "test_buffer.jsonl"

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_buffer_append_load_persistence(self):
        """Kiểm tra lưu trữ vào buffer, ghi ra đĩa và tải lại dữ liệu nguyên vẹn."""
        buf = StoreForwardBuffer(file_path=self.buffer_file, max_size=10, batch_size=5)
        self.assertTrue(buf.is_empty())
        self.assertEqual(buf.size(), 0)

        # Thêm 3 bản ghi
        for i in range(3):
            buf.append({"ts": 1000 + i, "data": f"sample_{i}"})

        self.assertEqual(buf.size(), 3)
        self.assertFalse(buf.is_empty())

        # Khởi tạo buffer mới trỏ vào cùng file để kiểm tra phục hồi sau khi tắt bridge
        buf_reloaded = StoreForwardBuffer(file_path=self.buffer_file, max_size=10, batch_size=5)
        self.assertEqual(buf_reloaded.size(), 3)
        self.assertEqual(buf_reloaded.queue[0]["ts"], 1000)
        self.assertEqual(buf_reloaded.queue[2]["ts"], 1002)

    def test_buffer_fifo_overflow(self):
        """Kiểm tra khi đầy (vượt max_size), bản ghi cũ nhất bị bỏ (FIFO)."""
        buf = StoreForwardBuffer(file_path=self.buffer_file, max_size=3, batch_size=2)

        # Thêm 5 bản ghi (vượt giới hạn max_size=3)
        for i in range(5):
            buf.append({"ts": i, "val": f"val_{i}"})

        # Chỉ còn 3 bản ghi mới nhất: 2, 3, 4
        self.assertEqual(buf.size(), 3)
        records = list(buf.queue)
        self.assertEqual(records[0]["ts"], 2)
        self.assertEqual(records[1]["ts"], 3)
        self.assertEqual(records[2]["ts"], 4)

    def test_buffer_pop_batch_order(self):
        """Kiểm tra pop_batch lấy đúng theo thứ tự từ cũ đến mới và cập nhật lại file đĩa."""
        buf = StoreForwardBuffer(file_path=self.buffer_file, max_size=10, batch_size=2)
        for i in range(5):
            buf.append({"ts": 10 + i})

        # Lấy lô đầu tiên (size=2) -> chứa 10, 11
        batch1 = buf.pop_batch(batch_size=2)
        self.assertEqual(len(batch1), 2)
        self.assertEqual(batch1[0]["ts"], 10)
        self.assertEqual(batch1[1]["ts"], 11)
        self.assertEqual(buf.size(), 3)

        # Lấy lô thứ hai (size=2) -> chứa 12, 13
        batch2 = buf.pop_batch(batch_size=2)
        self.assertEqual(len(batch2), 2)
        self.assertEqual(batch2[0]["ts"], 12)
        self.assertEqual(batch2[1]["ts"], 13)
        self.assertEqual(buf.size(), 1)

        # Lấy lô cuối cùng -> chứa 14
        batch3 = buf.pop_batch(batch_size=2)
        self.assertEqual(len(batch3), 1)
        self.assertEqual(batch3[0]["ts"], 14)
        self.assertTrue(buf.is_empty())

    def test_format_batch_payload_thingsboard(self):
        """Kiểm tra gộp lô telemetry đúng chuẩn ThingsBoard v1/gateway/telemetry."""
        record1 = {
            DEVICE_NODE_A: [{"ts": 1000, "values": {"temperature": 29.0}}],
            DEVICE_NODE_B: [{"ts": 1000, "values": {"fan_level": 1}}],
        }
        record2 = {
            DEVICE_NODE_A: [{"ts": 2000, "values": {"temperature": 30.0}}],
            DEVICE_NODE_B: [{"ts": 2000, "values": {"fan_level": 2}}],
        }

        payload = StoreForwardBuffer.format_batch_payload([record1, record2])

        self.assertIn(DEVICE_NODE_A, payload)
        self.assertIn(DEVICE_NODE_B, payload)
        self.assertEqual(len(payload[DEVICE_NODE_A]), 2)
        self.assertEqual(payload[DEVICE_NODE_A][0]["ts"], 1000)
        self.assertEqual(payload[DEVICE_NODE_A][1]["ts"], 2000)
        self.assertEqual(payload[DEVICE_NODE_B][1]["values"]["fan_level"], 2)

    def test_bridge_store_and_forward_flow(self):
        """
        Kiểm thử quy trình tích hợp trên Gateway Bridge:
        1. Khi mất kết nối (cloud_connected = False) -> lưu vào buffer.
        2. Khi có lại mạng (cloud_connected = True) -> gửi bù thành công theo lô.
        """
        buf = StoreForwardBuffer(file_path=self.buffer_file, max_size=50, batch_size=5)
        bridge = GatewayBridge(buffer=buf)

        published_cloud = []
        bridge.cloud_client.publish = lambda topic, payload, qos=0, retain=False: published_cloud.append(
            (topic, json.loads(payload))
        )

        # 1. Trạng thái mất mạng: cloud_connected = False
        bridge.cloud_connected = False

        sample_telemetry_1 = {
            "ts": 1700000000000,
            "temperature": 28.5,
            "humidity": 60.0,
            "fan_level": 1,
            "mode": "auto",
        }
        sample_telemetry_2 = {
            "ts": 1700000002000,
            "temperature": 29.0,
            "humidity": 59.0,
            "fan_level": 2,
            "mode": "manual",
        }

        bridge._handle_local_telemetry(sample_telemetry_1)
        bridge._handle_local_telemetry(sample_telemetry_2)

        # Chưa đẩy lên cloud mà phải nằm trong buffer
        self.assertEqual(len(published_cloud), 0)
        self.assertEqual(bridge.buffer.size(), 2)

        # 2. Khôi phục kết nối: cloud_connected = True và chạy gửi bù
        bridge.cloud_connected = True
        bridge._flush_buffer_worker()

        # Buffer đã được gửi sạch
        self.assertEqual(bridge.buffer.size(), 0)
        self.assertEqual(len(published_cloud), 1)

        topic, cloud_msg = published_cloud[0]
        self.assertEqual(topic, "v1/gateway/telemetry")
        self.assertIn(DEVICE_NODE_A, cloud_msg)
        self.assertIn(DEVICE_NODE_B, cloud_msg)
        # Kiểm tra giữ nguyên ts gốc
        self.assertEqual(cloud_msg[DEVICE_NODE_A][0]["ts"], 1700000000000)
        self.assertEqual(cloud_msg[DEVICE_NODE_A][1]["ts"], 1700000002000)


if __name__ == "__main__":
    unittest.main()
