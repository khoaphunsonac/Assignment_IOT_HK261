#!/usr/bin/env python3
"""
fake_node_b.py - Giả lập Node B cho bài tập lớn IoT (Nhóm 4)
Khớp 100% tài liệu CONTRACT.md (mục 4–9, 13).
Tác giả: Tuấn (TV2 - Gateway)
"""

import json
import logging
import os
import random
import sys
import threading
import time
from typing import Any, Dict, Optional

import paho.mqtt.client as mqtt

# Cấu hình logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger("fake_node_b")

# Cấu hình kết nối MQTT
BROKER = os.getenv("MQTT_HOST", "localhost")
PORT = int(os.getenv("MQTT_PORT", "1883"))
CLIENT_ID = "room1-fake-node-b"

# Trạng thái thiết bị (Node B)
state_lock = threading.RLock()
state = {
    "fan_level": 0,      # 0, 1, 2
    "light_on": False,   # bool
    "mode": "auto",      # "auto", "manual", "offline"
}

# Cấu hình ngưỡng mặc định (CONTRACT mục 7)
config = {
    "fan_on_1": 31.0,
    "fan_off_1": 29.0,
    "fan_on_2": 33.0,
    "fan_off_2": 31.0,
    "light_dark": 1500,
    "light_bright": 2500,
    "empty_timeout_s": 60,      # demo: 60s
    "manual_timeout_s": 120,    # demo: 120s (mặc định sản phẩm 1800s)
}

# Các biến trạng thái mô phỏng
seq = 0
start_time = time.time()
manual_start_time: Optional[float] = None
temp_history = [29.5, 30.0]  # Lưu 2 lần đo gần nhất để tính trung bình
presence_state = True
presence_toggle_counter = 0


def get_state() -> Dict[str, Any]:
    """Lấy bản sao trạng thái hiện tại (luôn có đủ 3 key)."""
    with state_lock:
        return {
            "fan_level": state["fan_level"],
            "light_on": state["light_on"],
            "mode": state["mode"],
        }


def make_telemetry() -> Dict[str, Any]:
    """Tạo gói telemetry đúng CONTRACT mục 5."""
    global seq, temp_history, presence_state, presence_toggle_counter

    seq += 1
    now_ms = int(time.time() * 1000)
    uptime_s = int(time.time() - start_time)

    # 1. Mô phỏng cảm biến Node A:
    # Nhiệt độ dao động quanh 27 - 34°C
    last_temp = temp_history[-1]
    new_raw_temp = round(last_temp + random.uniform(-0.4, 0.4), 1)
    new_raw_temp = max(26.0, min(35.0, new_raw_temp))
    temp_history.append(new_raw_temp)
    if len(temp_history) > 2:
        temp_history.pop(0)

    # CONTRACT mục 5: "Trung bình 2 lần đo gần nhất (Node B tính)"
    avg_temperature = round(sum(temp_history) / len(temp_history), 1)

    # Độ ẩm: 50% - 80%
    humidity = round(random.uniform(55.0, 75.0), 1)

    # Ánh sáng: 500 - 3500 ADC
    light = random.randint(1200, 2600)

    # Hiện diện người: hầu hết là True, thỉnh thoảng đổi
    presence_toggle_counter += 1
    if presence_toggle_counter >= 50:  # ~100s đổi 1 lần
        presence_state = not presence_state
        presence_toggle_counter = 0

    # CONTRACT mục 13: thứ tự ưu tiên 1:
    # "presence = false (phòng trống): tắt quạt, tắt đèn, xóa manual, về auto"
    with state_lock:
        if not presence_state and (state["fan_level"] != 0 or state["light_on"] or state["mode"] != "auto"):
            logger.info("[Priority 1] Phòng trống: Tắt quạt, tắt đèn, chuyển về chế độ auto")
            state["fan_level"] = 0
            state["light_on"] = False
            state["mode"] = "auto"

        curr_state = {
            "fan_level": state["fan_level"],
            "light_on": state["light_on"],
            "mode": state["mode"],
        }

    # Anomaly / TinyML mô phỏng
    anomaly = False
    anomaly_score = round(random.uniform(0.02, 0.12), 3)
    if avg_temperature >= 33.5 or random.random() < 0.03:
        anomaly = True
        anomaly_score = round(random.uniform(0.80, 0.96), 3)

    telemetry = {
        "ts": now_ms,
        "seq": seq,
        "temperature": avg_temperature,
        "humidity": humidity,
        "light": light,
        "presence": presence_state,
        "anomaly": anomaly,
        "anomaly_score": anomaly_score,
        "dht_fail": 0,
        "node_a_online": True,
        "espnow_loss": round(random.uniform(0.0, 1.2), 1),
        "fan_level": curr_state["fan_level"],
        "light_on": curr_state["light_on"],
        "mode": curr_state["mode"],
        "rssi": random.randint(-55, -45),
        "fw_version": "1.0.0",
        "uptime_s": uptime_s,
    }
    return telemetry


def handle_cmd(cmd_raw: Any) -> Dict[str, Any]:
    """
    Xử lý lệnh từ room1/cmd đúng CONTRACT mục 6.2, 6.3 và mục 13.
    """
    global manual_start_time

    if not isinstance(cmd_raw, dict):
        return {
            "id": None,
            "error": "bad_json",
            "state": get_state(),
        }

    cmd_id = cmd_raw.get("id")
    method = cmd_raw.get("method")
    params = cmd_raw.get("params")

    # Kiểm tra method hợp lệ
    valid_methods = {"setFan", "setLight", "setMode", "autoFan", "autoLight", "getState"}
    if method not in valid_methods:
        return {
            "id": cmd_id,
            "method": method,
            "ok": False,
            "error": "unknown_method",
            "state": get_state(),
        }

    with state_lock:
        # 1. setFan (Lệnh tay từ người bấm)
        if method == "setFan":
            # params phải là int 0, 1, 2 (loại trừ bool vì isinstance(True, int) là True)
            if isinstance(params, bool) or not isinstance(params, int) or params not in (0, 1, 2):
                return {
                    "id": cmd_id,
                    "method": method,
                    "ok": False,
                    "error": "bad_params",
                    "state": get_state(),
                }
            state["fan_level"] = params
            state["mode"] = "manual"
            manual_start_time = time.time()
            logger.info("Executed setFan -> fan_level: %s, mode: manual", params)
            return {
                "id": cmd_id,
                "method": method,
                "ok": True,
                "state": get_state(),
            }

        # 2. setLight (Lệnh tay từ người bấm)
        if method == "setLight":
            if not isinstance(params, bool):
                return {
                    "id": cmd_id,
                    "method": method,
                    "ok": False,
                    "error": "bad_params",
                    "state": get_state(),
                }
            state["light_on"] = params
            state["mode"] = "manual"
            manual_start_time = time.time()
            logger.info("Executed setLight -> light_on: %s, mode: manual", params)
            return {
                "id": cmd_id,
                "method": method,
                "ok": True,
                "state": get_state(),
            }

        # 3. setMode (Người bấm đổi mode: "auto" / "manual")
        if method == "setMode":
            if params not in ("auto", "manual"):
                return {
                    "id": cmd_id,
                    "method": method,
                    "ok": False,
                    "error": "bad_params",
                    "state": get_state(),
                }
            state["mode"] = params
            if params == "manual":
                manual_start_time = time.time()
            else:
                manual_start_time = None
            logger.info("Executed setMode -> mode: %s", params)
            return {
                "id": cmd_id,
                "method": method,
                "ok": True,
                "state": get_state(),
            }

        # 4. autoFan (Chỉ dành cho RuleChain)
        if method == "autoFan":
            # Chỉ thực hiện khi mode == auto
            if state["mode"] != "auto":
                return {
                    "id": cmd_id,
                    "method": method,
                    "ok": False,
                    "error": "ignored_not_auto",
                    "state": get_state(),
                }
            if isinstance(params, bool) or not isinstance(params, int) or params not in (0, 1, 2):
                return {
                    "id": cmd_id,
                    "method": method,
                    "ok": False,
                    "error": "bad_params",
                    "state": get_state(),
                }
            state["fan_level"] = params
            logger.info("Executed autoFan -> fan_level: %s", params)
            return {
                "id": cmd_id,
                "method": method,
                "ok": True,
                "state": get_state(),
            }

        # 5. autoLight (Chỉ dành cho RuleChain)
        if method == "autoLight":
            if state["mode"] != "auto":
                return {
                    "id": cmd_id,
                    "method": method,
                    "ok": False,
                    "error": "ignored_not_auto",
                    "state": get_state(),
                }
            if not isinstance(params, bool):
                return {
                    "id": cmd_id,
                    "method": method,
                    "ok": False,
                    "error": "bad_params",
                    "state": get_state(),
                }
            state["light_on"] = params
            logger.info("Executed autoLight -> light_on: %s", params)
            return {
                "id": cmd_id,
                "method": method,
                "ok": True,
                "state": get_state(),
            }

        # 6. getState
        if method == "getState":
            return {
                "id": cmd_id,
                "method": method,
                "ok": True,
                "state": get_state(),
            }

    # Mặc định phòng ngừa
    return {
        "id": cmd_id,
        "method": method,
        "ok": False,
        "error": "unknown_method",
        "state": get_state(),
    }


def handle_config(payload: Dict[str, Any]):
    """Xử lý cập nhật ngưỡng từ room1/config (CONTRACT mục 7)."""
    global config
    required_keys = [
        "fan_on_1", "fan_off_1", "fan_on_2", "fan_off_2",
        "light_dark", "light_bright"
    ]
    if not all(k in payload for k in required_keys):
        logger.warning("Config thieu key bat buoc, bo qua: %s", payload)
        return

    # Kiểm tra ràng buộc: fan_off_1 < fan_on_1 <= fan_off_2 < fan_on_2 và light_dark < light_bright
    f_off_1 = float(payload["fan_off_1"])
    f_on_1 = float(payload["fan_on_1"])
    f_off_2 = float(payload["fan_off_2"])
    f_on_2 = float(payload["fan_on_2"])
    l_dark = int(payload["light_dark"])
    l_bright = int(payload["light_bright"])

    if not (f_off_1 < f_on_1 <= f_off_2 < f_on_2 and l_dark < l_bright):
        logger.warning("Config khong thoa man rang buoc CONTRACT muc 7, bo qua!")
        return

    config.update(payload)
    logger.info("Cap nhat bo nguong room1/config thanh cong: %s", config)


def on_connect(client: mqtt.Client, userdata, flags, rc):
    """Callback khi ket noi thanh cong voi MQTT broker."""
    if rc == 0:
        logger.info("Da ket noi thanh cong toi Broker %s:%s", BROKER, PORT)
        # Subscribe cac topic theo CONTRACT mục 4
        client.subscribe("room1/cmd", qos=1)
        client.subscribe("room1/config", qos=1)
        logger.info("Da subscribe room1/cmd va room1/config")

        # Publish retained room1/status online (CONTRACT mục 8)
        status_payload = {
            "state": "online",
            "fw_version": "1.0.0",
            "ip": "127.0.0.1",
        }
        client.publish("room1/status", json.dumps(status_payload), qos=1, retain=True)
        logger.info("Da publish retained room1/status: online")
    else:
        logger.error("Ket noi MQTT that bai voi ma loi rc=%s", rc)


def on_message(client: mqtt.Client, userdata, msg):
    """Callback khi nhan tin nhan MQTT."""
    topic = msg.topic
    payload_raw = msg.payload.decode("utf-8", errors="replace")
    logger.debug("Nhan tin topic [%s]: %s", topic, payload_raw)

    if topic == "room1/cmd":
        try:
            cmd = json.loads(payload_raw)
        except Exception:
            resp = {
                "id": None,
                "error": "bad_json",
                "state": get_state(),
            }
            client.publish("room1/cmd/resp", json.dumps(resp), qos=0)
            logger.warning("room1/cmd: Loi JSON khong hop le -> Da tra bad_json")
            return

        resp = handle_cmd(cmd)
        client.publish("room1/cmd/resp", json.dumps(resp), qos=0)
        logger.info("room1/cmd/resp: %s", json.dumps(resp))

        # CONTRACT mục 5: "Ngay khi quạt, đèn hoặc chế độ đổi (để dashboard cập nhật nhanh)"
        if resp.get("ok"):
            telem = make_telemetry()
            client.publish("room1/telemetry", json.dumps(telem), qos=0)
            logger.info("Gui telemetry ngay lap tuc sau lenh")

    elif topic == "room1/config":
        try:
            cfg = json.loads(payload_raw)
            handle_config(cfg)
        except Exception as e:
            logger.warning("Loi doc room1/config: %s", e)


def create_mqtt_client() -> mqtt.Client:
    """Khoi tao MQTT client tuong thich ca paho-mqtt 1.6 va 2.x."""
    if hasattr(mqtt, "CallbackAPIVersion"):
        c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1, client_id=CLIENT_ID)
    else:
        c = mqtt.Client(client_id=CLIENT_ID)

    # Last Will: CONTRACT mục 8
    will_payload = json.dumps({"state": "offline"})
    c.will_set("room1/status", will_payload, qos=1, retain=True)

    c.on_connect = on_connect
    c.on_message = on_message
    return c


def main():
    logger.info("=== Khoi dong fake_node_b (Tuấn - TV2) ===")
    logger.info("MQTT Broker: %s:%s | Client ID: %s", BROKER, PORT, CLIENT_ID)

    client = create_mqtt_client()

    try:
        client.connect(BROKER, PORT, 60)
    except Exception as e:
        logger.error("Khong the ket noi broker MQTT: %s", e)
        logger.error("Vui long dam bao Mosquitto dang chay!")
        sys.exit(1)

    client.loop_start()

    global manual_start_time
    try:
        while True:
            # Kiem tra timeout che do manual (CONTRACT mục 13)
            with state_lock:
                if state["mode"] == "manual" and manual_start_time is not None:
                    elapsed = time.time() - manual_start_time
                    timeout = config.get("manual_timeout_s", 120)
                    if elapsed >= timeout:
                        logger.info("[Timeout] Het %ss manual -> Tro ve mode auto", timeout)
                        state["mode"] = "auto"
                        manual_start_time = None

            # Gui telemetry moi 2 giay (CONTRACT mục 5)
            telemetry_data = make_telemetry()
            client.publish("room1/telemetry", json.dumps(telemetry_data), qos=0)
            logger.info(
                "Telemetry [seq=%d]: temp=%.1f°C, hum=%.1f%%, light=%d, fan=%d, light_on=%s, mode=%s",
                telemetry_data["seq"],
                telemetry_data["temperature"],
                telemetry_data["humidity"],
                telemetry_data["light"],
                telemetry_data["fan_level"],
                telemetry_data["light_on"],
                telemetry_data["mode"],
            )

            time.sleep(2)
    except KeyboardInterrupt:
        logger.info("Nhan tin hieu dung script (Ctrl+C)...")
    finally:
        logger.info("Gui trang thai offline truoc khi dong...")
        client.publish("room1/status", json.dumps({"state": "offline"}), qos=1, retain=True)
        time.sleep(0.5)
        client.loop_stop()
        client.disconnect()
        logger.info("fake_node_b da tat.")


if __name__ == "__main__":
    main()
