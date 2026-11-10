"""
test_fake_node_b.py - Kiem thu tu dong cac yeu cau CONTRACT va 5 ca test trong TASKS_W1.md
"""

import json
import unittest
from fake_node_b import (
    handle_cmd,
    make_telemetry,
    handle_config,
    state,
    state_lock,
    config,
    get_state,
)


class TestFakeNodeB(unittest.TestCase):
    def setUp(self):
        # Reset state truoc moi test
        with state_lock:
            state["fan_level"] = 0
            state["light_on"] = False
            state["mode"] = "auto"

    def test_case_1_set_fan_valid(self):
        """Case 1: setFan 2 -> resp ok: true, mode: manual, telemetry co fan_level: 2"""
        cmd = {"id": "test-1", "method": "setFan", "params": 2}
        resp = handle_cmd(cmd)

        self.assertEqual(resp["id"], "test-1")
        self.assertEqual(resp["method"], "setFan")
        self.assertTrue(resp["ok"])
        self.assertEqual(resp["state"]["fan_level"], 2)
        self.assertEqual(resp["state"]["mode"], "manual")

        # Kiem tra telemetry ngay sau do
        telem = make_telemetry()
        self.assertEqual(telem["fan_level"], 2)
        self.assertEqual(telem["mode"], "manual")

    def test_case_2_autofan_ignored_when_manual(self):
        """Case 2: autoFan 1 khi dang manual -> resp ok: false, error: ignored_not_auto"""
        # Chuyen sang manual
        handle_cmd({"id": "test-prep", "method": "setFan", "params": 2})

        cmd = {"id": "test-2", "method": "autoFan", "params": 1}
        resp = handle_cmd(cmd)

        self.assertEqual(resp["id"], "test-2")
        self.assertEqual(resp["method"], "autoFan")
        self.assertFalse(resp["ok"])
        self.assertEqual(resp["error"], "ignored_not_auto")
        self.assertEqual(resp["state"]["fan_level"], 2)
        self.assertEqual(resp["state"]["mode"], "manual")

    def test_case_3_set_fan_bad_params(self):
        """Case 3: setFan 5 -> error: bad_params"""
        cmd = {"id": "test-3", "method": "setFan", "params": 5}
        resp = handle_cmd(cmd)

        self.assertEqual(resp["id"], "test-3")
        self.assertEqual(resp["method"], "setFan")
        self.assertFalse(resp["ok"])
        self.assertEqual(resp["error"], "bad_params")
        self.assertIn("fan_level", resp["state"])

    def test_case_4_bad_json(self):
        """Case 4: abc (khong phai JSON) -> error: bad_json, id: null"""
        resp = handle_cmd("not a dict")
        self.assertIsNone(resp["id"])
        self.assertEqual(resp["error"], "bad_json")
        self.assertIn("state", resp)

    def test_set_light_and_auto_light(self):
        """Kiem tra setLight va autoLight"""
        # setLight true -> manual
        resp = handle_cmd({"id": "light-1", "method": "setLight", "params": True})
        self.assertTrue(resp["ok"])
        self.assertTrue(resp["state"]["light_on"])
        self.assertEqual(resp["state"]["mode"], "manual")

        # autoLight bi ignore vi manual
        resp2 = handle_cmd({"id": "light-2", "method": "autoLight", "params": False})
        self.assertFalse(resp2["ok"])
        self.assertEqual(resp2["error"], "ignored_not_auto")

        # Chuyen ve auto -> autoLight thanh cong
        handle_cmd({"id": "mode-1", "method": "setMode", "params": "auto"})
        resp3 = handle_cmd({"id": "light-3", "method": "autoLight", "params": False})
        self.assertTrue(resp3["ok"])
        self.assertFalse(resp3["state"]["light_on"])

    def test_get_state(self):
        """Kiem tra method getState"""
        resp = handle_cmd({"id": "get-1", "method": "getState"})
        self.assertTrue(resp["ok"])
        self.assertEqual(resp["method"], "getState")
        self.assertIn("fan_level", resp["state"])
        self.assertIn("light_on", resp["state"])
        self.assertIn("mode", resp["state"])

    def test_telemetry_contract_compliance(self):
        """Kiem tra day du cac key va kieu du lieu cua telemetry theo CONTRACT muc 5"""
        telem = make_telemetry()
        required_keys = [
            "ts", "seq", "temperature", "humidity", "light", "presence",
            "anomaly", "anomaly_score", "dht_fail", "node_a_online",
            "espnow_loss", "fan_level", "light_on", "mode", "rssi",
            "fw_version", "uptime_s"
        ]
        for key in required_keys:
            self.assertIn(key, telem, f"Thieu key bat buoc: {key}")

        # Kiem tra kieu du lieu
        self.assertIsInstance(telem["ts"], int)
        self.assertIsInstance(telem["seq"], int)
        self.assertIsInstance(telem["temperature"], float)
        self.assertIsInstance(telem["humidity"], float)
        self.assertIsInstance(telem["light"], int)
        self.assertIsInstance(telem["presence"], bool)
        self.assertIsInstance(telem["anomaly"], bool)
        self.assertIsInstance(telem["anomaly_score"], float)
        self.assertIsInstance(telem["dht_fail"], int)
        self.assertIsInstance(telem["node_a_online"], bool)
        self.assertIsInstance(telem["espnow_loss"], float)
        self.assertIsInstance(telem["fan_level"], int)
        self.assertIsInstance(telem["light_on"], bool)
        self.assertIsInstance(telem["mode"], str)
        self.assertIsInstance(telem["rssi"], int)
        self.assertIsInstance(telem["fw_version"], str)
        self.assertIsInstance(telem["uptime_s"], int)

        # Kiem tra so thap phan
        # temperature va humidity co toi da 1 chu so thap phan
        self.assertEqual(telem["temperature"], round(telem["temperature"], 1))
        self.assertEqual(telem["humidity"], round(telem["humidity"], 1))
        # anomaly_score co toi da 3 chu so thap phan
        self.assertEqual(telem["anomaly_score"], round(telem["anomaly_score"], 3))


if __name__ == "__main__":
    unittest.main()

