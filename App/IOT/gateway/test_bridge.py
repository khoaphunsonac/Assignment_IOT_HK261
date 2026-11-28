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

    def test_rpc_string_params_normalization(self):
        """Kiểm tra Gateway Bridge tự động chuẩn hóa chuỗi params (int, bool, strip nháy mode)."""
        bridge = GatewayBridge()
        published_local = []
        bridge.local_client.publish = lambda topic, payload, qos=0, retain=False: published_local.append((topic, json.loads(payload)))

        # 1. setFan với chuỗi "2" -> int 2
        bridge._handle_cloud_rpc({"device": DEVICE_NODE_B, "data": {"id": 1, "method": "setFan", "params": "2"}})
        self.assertEqual(published_local[-1][1]["params"], 2)

        # 2. setLight với chuỗi "true" -> bool True
        bridge._handle_cloud_rpc({"device": DEVICE_NODE_B, "data": {"id": 2, "method": "setLight", "params": "true"}})
        self.assertEqual(published_local[-1][1]["params"], True)

        # 3. setMode với chuỗi có dấu nháy '"manual"' -> chuỗi "manual"
        bridge._handle_cloud_rpc({"device": DEVICE_NODE_B, "data": {"id": 3, "method": "setMode", "params": '"manual"'}})
        self.assertEqual(published_local[-1][1]["params"], "manual")

    def test_handle_cloud_attributes_values_format(self):
        """Kiểm tra xử lý phản hồi attributes dạng values từ CoreIOT (CONTRACT mục 11.5)."""
        bridge = GatewayBridge()
        published_local = []
        bridge.local_client.publish = lambda topic, payload, qos=0, retain=False: published_local.append((topic, json.loads(payload)))

        resp_msg = {
            "id": 1,
            "device": DEVICE_NODE_B,
            "values": {
                "fan_on_1": 30.5,
                "fan_off_1": 28.0,
            }
        }
        bridge._handle_cloud_attributes(resp_msg)

        self.assertEqual(len(published_local), 1)
        topic, payload = published_local[0]
        self.assertEqual(topic, "room1/config")
        self.assertEqual(payload["fan_on_1"], 30.5)
        self.assertEqual(payload["fan_off_1"], 28.0)

    def test_bridge_ota_status_forwarding(self):
        """Kiểm tra bridge chuyển tiếp room1/ota/status sang telemetry CoreIOT (Issue #45)."""
        bridge = GatewayBridge()
        published_cloud = []
        bridge.cloud_client.publish = lambda topic, payload, qos=0, retain=False: published_cloud.append((topic, json.loads(payload)))

        # Giả lập Node B gửi downloading 50%
        status_msg = {"version": "1.0.1", "state": "downloading", "progress": 50, "ts": 1762761800000}
        bridge._handle_local_ota_status(status_msg)

        self.assertEqual(len(published_cloud), 1)
        topic, payload = published_cloud[0]
        self.assertEqual(topic, "v1/gateway/telemetry")
        self.assertIn(DEVICE_NODE_B, payload)
        self.assertEqual(payload[DEVICE_NODE_B][0]["values"]["fw_state"], "DOWNLOADING")

    def test_bridge_cloud_ota_trigger(self):
        """Kiểm tra CoreIOT gán firmware -> bridge tự động phát hành room1/ota (Issue #29 & #45)."""
        bridge = GatewayBridge()
        published_local = []
        bridge.local_client.publish = lambda topic, payload, qos=0, retain=False: published_local.append((topic, json.loads(payload)))

        from pathlib import Path
        fw_dir = Path(__file__).parent / "fw"
        fw_dir.mkdir(parents=True, exist_ok=True)
        test_bin = fw_dir / "node_b_1.0.1.bin"
        test_bin.write_bytes(b"mock firmware binary")

        try:
            cloud_attr_event = {
                "device": DEVICE_NODE_B,
                "data": {
                    "fw_title": "node-b-fw",
                    "fw_version": "1.0.1",
                    "fw_size": 20,
                }
            }
            bridge._handle_cloud_attributes(cloud_attr_event)

            ota_msgs = [p for t, p in published_local if t == "room1/ota"]
            self.assertEqual(len(ota_msgs), 1)
            self.assertEqual(ota_msgs[0]["version"], "1.0.1")
            self.assertIn("node_b_1.0.1.bin", ota_msgs[0]["url"])
            self.assertIn("sha256", ota_msgs[0])

            # Test ca không có file khớp version -> cảnh báo và dừng, không phát hành nhầm file khác
            published_local.clear()
            cloud_attr_unmatched = {
                "device": DEVICE_NODE_B,
                "data": {
                    "fw_title": "node-b-fw",
                    "fw_version": "1.0.2",
                }
            }
            bridge._handle_cloud_attributes(cloud_attr_unmatched)
            self.assertEqual(len([p for t, p in published_local if t == "room1/ota"]), 0)

            # Test ca sai checksum từ CoreIOT -> cảnh báo và dừng
            published_local.clear()
            cloud_attr_bad_checksum = {
                "device": DEVICE_NODE_B,
                "data": {
                    "fw_title": "node-b-fw",
                    "fw_version": "1.0.1",
                    "fw_checksum": "wrong_checksum_value",
                }
            }
            bridge._handle_cloud_attributes(cloud_attr_bad_checksum)
            self.assertEqual(len([p for t, p in published_local if t == "room1/ota"]), 0)
        finally:
            if test_bin.exists():
                test_bin.unlink()


if __name__ == "__main__":
    unittest.main()
