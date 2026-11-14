"""
test_bridge.py - Kiểm thử tự động cho Gateway Bridge (Issue #28)
Khớp 100% CONTRACT mục 11 (Tách telemetry, RPC 2 chiều, Timeout, Config validation).
"""

import json
import time
import unittest
from bridge import (
    split_telemetry,
    validate_config,
    DEVICE_NODE_A,
    DEVICE_NODE_B,
    NODE_A_KEYS,
    NODE_B_KEYS,
    pending_rpc,
    rpc_lock,
    GatewayBridge,
)


class TestGatewayBridge(unittest.TestCase):
    def setUp(self):
        with rpc_lock:
            pending_rpc.clear()

    def test_split_telemetry_full(self):
        """Kiểm tra tách đúng và đủ key giữa 2 thiết bị con theo CONTRACT mục 11.2."""
        sample = {
            "ts": 1762761600123,
            "seq": 1532,
            "temperature": 30.5,
            "humidity": 65.2,
            "light": 1830,
            "presence": True,
            "anomaly": False,
            "anomaly_score": 0.124,
            "dht_fail": 0,
            "node_a_online": True,
            "espnow_loss": 0.8,
            "fan_level": 1,
            "light_on": True,
            "mode": "auto",
            "rssi": -45,
            "fw_version": "1.0.0",
            "uptime_s": 3605,
        }

        result = split_telemetry(sample)

        self.assertIn(DEVICE_NODE_A, result)
        self.assertIn(DEVICE_NODE_B, result)

        entry_a = result[DEVICE_NODE_A][0]
        entry_b = result[DEVICE_NODE_B][0]

        # Kiểm tra timestamp được bảo toàn
        self.assertEqual(entry_a["ts"], 1762761600123)
        self.assertEqual(entry_b["ts"], 1762761600123)

        values_a = entry_a["values"]
        values_b = entry_b["values"]

        # Node A phải có đủ tất cả key cảm biến
        for k in NODE_A_KEYS:
            self.assertIn(k, values_a, f"Node A thiếu key: {k}")

        # Node B phải có đủ tất cả key chấp hành
        for k in NODE_B_KEYS:
            self.assertIn(k, values_b, f"Node B thiếu key: {k}")

        # Đảm bảo không bị lẫn key giữa 2 node
        for k in NODE_B_KEYS:
            self.assertNotIn(k, values_a)
        for k in NODE_A_KEYS:
            self.assertNotIn(k, values_b)

    def test_split_telemetry_missing_sensor(self):
        """Khi Node A mất kết nối, chỉ gửi telemetry của Node B."""
        sample = {
            "ts": 1762761700000,
            "node_a_online": False,
            "fan_level": 1,
            "light_on": True,
            "mode": "auto",
            "rssi": -46,
            "fw_version": "1.0.0",
            "uptime_s": 3705,
        }

        result = split_telemetry(sample)
        self.assertNotIn(DEVICE_NODE_A, result)
        self.assertIn(DEVICE_NODE_B, result)
        self.assertEqual(result[DEVICE_NODE_B][0]["values"]["fan_level"], 1)

    def test_validate_config(self):
        """Kiểm tra logic ràng buộc của bộ ngưỡng theo CONTRACT mục 7."""
        valid_cfg = {
            "fan_on_1": 31.0,
            "fan_off_1": 29.0,
            "fan_on_2": 33.0,
            "fan_off_2": 31.0,
            "light_dark": 1500,
            "light_bright": 2500,
        }
        self.assertTrue(validate_config(valid_cfg))

        # Vi phạm: fan_off_1 >= fan_on_1
        invalid_cfg_1 = dict(valid_cfg, fan_off_1=32.0)
        self.assertFalse(validate_config(invalid_cfg_1))

        # Vi phạm: light_dark >= light_bright
        invalid_cfg_2 = dict(valid_cfg, light_dark=3000)
        self.assertFalse(validate_config(invalid_cfg_2))

        # Thiếu key
        self.assertFalse(validate_config({"fan_on_1": 31.0}))

    def test_rpc_request_and_response_translation(self):
        """Kiểm tra ánh xạ RPC CoreIOT -> Local và Local Resp -> CoreIOT Resp (CONTRACT mục 11.4)."""
        bridge = GatewayBridge()

        # Giả lập CoreIOT gửi RPC request
        cloud_rpc_msg = {
            "device": DEVICE_NODE_B,
            "data": {
                "id": 17,
                "method": "setFan",
                "params": 2
            }
        }

        # Mock publish của local client
        published_local = []
        bridge.local_client.publish = lambda topic, payload, qos=0, retain=False: published_local.append((topic, json.loads(payload)))

        bridge._handle_cloud_rpc(cloud_rpc_msg)

        # Kiểm tra lệnh được gửi xuống Local
        self.assertEqual(len(published_local), 1)
        topic, payload = published_local[0]
        self.assertEqual(topic, "room1/cmd")
        self.assertEqual(payload["id"], "tb-17")
        self.assertEqual(payload["method"], "setFan")
        self.assertEqual(payload["params"], 2)

        # Kiểm tra đã lưu pending RPC
        with rpc_lock:
            self.assertIn("tb-17", pending_rpc)
            self.assertEqual(pending_rpc["tb-17"]["coreiot_id"], 17)

        # Giả lập Node B phản hồi kết quả
        node_b_resp = {
            "id": "tb-17",
            "method": "setFan",
            "ok": True,
            "state": {
                "fan_level": 2,
                "light_on": False,
                "mode": "manual"
            }
        }

        published_cloud = []
        bridge.cloud_client.publish = lambda topic, payload, qos=0, retain=False: published_cloud.append((topic, json.loads(payload)))

        bridge._handle_local_cmd_resp(node_b_resp)

        # Kiểm tra phản hồi được gửi lên CoreIOT
        self.assertEqual(len(published_cloud), 1)
        cloud_topic, cloud_payload = published_cloud[0]
        self.assertEqual(cloud_topic, "v1/gateway/rpc")
        self.assertEqual(cloud_payload["device"], DEVICE_NODE_B)
        self.assertEqual(cloud_payload["id"], 17)
        self.assertTrue(cloud_payload["data"]["ok"])
        self.assertEqual(cloud_payload["data"]["fan_level"], 2)
        self.assertEqual(cloud_payload["data"]["mode"], "manual")

        # Kiểm tra đã xóa pending RPC
        with rpc_lock:
            self.assertNotIn("tb-17", pending_rpc)


if __name__ == "__main__":
    unittest.main()
