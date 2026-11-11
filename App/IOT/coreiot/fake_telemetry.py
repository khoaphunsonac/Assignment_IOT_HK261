"""Gửi telemetry giả lên CoreIOT cho device tạm `room1-test` (issue #32).

Key và kiểu dữ liệu đúng docs/CONTRACT.md mục 5. Dùng để dựng dashboard
khi chưa có Gateway. Đồng thời nhận RPC (setFan, setLight, setMode, getState)
để thử các nút điều khiển trên dashboard.

Cách chạy:
    pip install paho-mqtt python-dotenv
    copy .env.example .env      # rồi dán Access Token của room1-test
    python fake_telemetry.py
"""

import json
import os
import random
import time
from pathlib import Path

import paho.mqtt.client as mqtt

try:
    from dotenv import load_dotenv

    load_dotenv(Path(__file__).with_name(".env"))
except ImportError:
    pass

HOST = os.getenv("COREIOT_HOST", "app.coreiot.io")
PORT = int(os.getenv("COREIOT_PORT", "1883"))
TOKEN = os.getenv("COREIOT_TEST_TOKEN", "")
PERIOD_S = 2.0
FW_VERSION = "1.0.0"

TOPIC_TELEMETRY = "v1/devices/me/telemetry"
TOPIC_RPC_REQ = "v1/devices/me/rpc/request/+"
TOPIC_RPC_RESP = "v1/devices/me/rpc/response/"

state = {"fan_level": 0, "light_on": False, "mode": "auto"}
seq = 0
start = time.time()
temperature = 30.0


def make_telemetry() -> dict:
    """Một bản tin đúng CONTRACT mục 5 (đủ key, khi Node A online)."""
    global seq, temperature
    seq += 1
    temperature = min(35.0, max(26.0, temperature + random.uniform(-0.3, 0.3)))
    score = random.uniform(0.0, 0.3)
    anomaly = random.random() < 0.05
    if anomaly:
        score = random.uniform(0.7, 1.0)
    return {
        "ts": int(time.time() * 1000),
        "seq": seq,
        "temperature": round(temperature, 1),
        "humidity": round(random.uniform(50, 80), 1),
        "light": random.randint(500, 3500),
        "presence": random.random() < 0.8,
        "anomaly": anomaly,
        "anomaly_score": round(score, 3),
        "dht_fail": 0,
        "node_a_online": True,
        "espnow_loss": round(random.uniform(0, 2), 1),
        "fan_level": state["fan_level"],
        "light_on": state["light_on"],
        "mode": state["mode"],
        "rssi": random.randint(-70, -40),
        "fw_version": FW_VERSION,
        "uptime_s": int(time.time() - start),
    }


def to_thingsboard(payload: dict) -> str:
    """ThingsBoard cần {"ts": ..., "values": {...}}; gửi phẳng thì `ts` thành một key."""
    values = dict(payload)
    ts = values.pop("ts")
    return json.dumps({"ts": ts, "values": values})


def handle_rpc(method: str, params) -> dict:
    """Xử lý RPC theo CONTRACT mục 6.2, trả dữ liệu giống mục 11.4."""
    ok, error = True, None
    if method == "setFan":
        if isinstance(params, int) and not isinstance(params, bool) and 0 <= params <= 2:
            state["fan_level"] = params
            state["mode"] = "manual"
        else:
            ok, error = False, "bad_params"
    elif method == "setLight":
        if isinstance(params, bool):
            state["light_on"] = params
            state["mode"] = "manual"
        else:
            ok, error = False, "bad_params"
    elif method == "setMode":
        if params in ("auto", "manual"):
            state["mode"] = params
        else:
            ok, error = False, "bad_params"
    elif method != "getState":
        ok, error = False, "unknown_method"
    data = {"ok": ok, **state}
    if error:
        data["error"] = error
    return data


def on_connect(client, userdata, flags, rc, *args):
    if rc == 0:
        print(f"Đã nối {HOST}:{PORT}")
        client.subscribe(TOPIC_RPC_REQ, qos=1)
    else:
        print(f"Nối thất bại, rc={rc} (kiểm tra COREIOT_TEST_TOKEN)")


def on_message(client, userdata, msg):
    req_id = msg.topic.rsplit("/", 1)[-1]
    try:
        req = json.loads(msg.payload)
    except ValueError:
        return
    method, params = req.get("method"), req.get("params")
    # Widget gửi params dạng chuỗi JSON ("2", "true") thì đổi lại thành số/bool.
    if isinstance(params, str):
        try:
            params = json.loads(params)
        except ValueError:
            pass
    resp = handle_rpc(method, params)
    client.publish(TOPIC_RPC_RESP + req_id, json.dumps(resp), qos=1)
    print(f"RPC {method}({params!r}) -> {resp}")
    # Gửi ngay trạng thái mới để dashboard cập nhật nhanh (CONTRACT mục 5).
    client.publish(TOPIC_TELEMETRY, to_thingsboard(make_telemetry()), qos=0)


def make_client() -> mqtt.Client:
    # Chạy được với paho-mqtt 1.6.1 (bản nhóm ghim) lẫn 2.x.
    if hasattr(mqtt, "CallbackAPIVersion"):
        return mqtt.Client(mqtt.CallbackAPIVersion.VERSION1, client_id="room1-test-fake")
    return mqtt.Client(client_id="room1-test-fake")


def main():
    if not TOKEN:
        raise SystemExit("Thiếu COREIOT_TEST_TOKEN trong App/IOT/coreiot/.env")
    client = make_client()
    client.username_pw_set(TOKEN)
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(HOST, PORT, 60)
    client.loop_start()
    try:
        while True:
            payload = make_telemetry()
            client.publish(TOPIC_TELEMETRY, to_thingsboard(payload), qos=0)
            print(f"seq={payload['seq']} t={payload['temperature']} fan={state['fan_level']} mode={state['mode']}")
            time.sleep(PERIOD_S)
    except KeyboardInterrupt:
        pass
    finally:
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()
