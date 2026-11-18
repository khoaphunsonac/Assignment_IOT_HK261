# 3 lệnh sau để chạy local UI (khi đã vào venv và thiết lập các chương trình cần thiết):
# & "C:\Program Files\mosquitto\mosquitto.exe" -c App\IOT\gateway\mosquitto.conf -v
# python App\IOT\gateway\fake_node_b.py
# streamlit run App\IOT\gateway\ui\app.py

import json
import os
import threading
import time

import paho.mqtt.client as mqtt
import streamlit as st

MQTT_HOST = os.getenv("MQTT_HOST", "localhost")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
CLIENT_ID = "room1-ui"

st.set_page_config(page_title="Room 1 - Local UI", layout="wide")
st.title("Room 1 - Giám sát & Điều khiển Local")


@st.cache_resource
def get_mqtt():
    lock = threading.Lock()
    data = {
        "telemetry": {},
        "status": "unknown",
        "last_resp": None,
        "connected": False,
        "error": None,
        "lock": lock,
    }

    def on_connect(client, userdata, flags, rc, *args):
        with lock:
            data["connected"] = (rc == 0)
            data["error"] = None if rc == 0 else f"MQTT connect lỗi rc={rc}"
        if rc == 0:
            client.subscribe("room1/telemetry", qos=0)
            client.subscribe("room1/status", qos=1)
            client.subscribe("room1/cmd/resp", qos=0)

    def on_disconnect(client, userdata, rc, *args):
        with lock:
            data["connected"] = False

    def on_message(client, userdata, msg):
        try:
            payload = json.loads(msg.payload.decode("utf-8", errors="replace"))
        except json.JSONDecodeError:
            return

        with lock:
            if msg.topic == "room1/telemetry":
                data["telemetry"] = payload
                data["telemetry_ts"] = time.time()
            elif msg.topic == "room1/status":
                data["status"] = payload.get("state", "unknown")
            elif msg.topic == "room1/cmd/resp":
                data["last_resp"] = payload

    # Tương thích cả paho-mqtt 1.6.1 và 2.x
    if hasattr(mqtt, "CallbackAPIVersion"):
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1, client_id=CLIENT_ID)
    else:
        client = mqtt.Client(client_id=CLIENT_ID)

    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message

    try:
        client.connect(MQTT_HOST, MQTT_PORT, keepalive=30)
        client.loop_start()
    except Exception as e:
        with lock:
            data["error"] = f"Không kết nối được {MQTT_HOST}:{MQTT_PORT} ({e})"

    return client, data


client, data = get_mqtt()
st.session_state.setdefault("next_id", 1)


def send_cmd(method: str, params):
    cmd_id = f"ui-{st.session_state.next_id}"
    st.session_state.next_id += 1
    payload = {"id": cmd_id, "method": method, "params": params}
    client.publish("room1/cmd", json.dumps(payload), qos=1)
    return cmd_id


@st.fragment(run_every=1)
def live():
    with data["lock"]:
        telem = dict(data["telemetry"])
        node_b_status = data["status"]
        last_resp = data["last_resp"]
        broker_ok = data["connected"]
        err = data["error"]

    if err:
        st.error(err)

    # Trạng thái kết nối Broker & Node B
    c_b, c_a = st.columns(2)
    with c_b:
        if not broker_ok:
            st.warning(f"Broker MQTT ({MQTT_HOST}:{MQTT_PORT}): Đang mất kết nối")
        elif node_b_status == "online":
            fw = telem.get("fw_version", "?")
            st.success(f"Node B: **ONLINE** (fw `{fw}`)")
        elif node_b_status == "offline":
            st.error("Node B: **OFFLINE**")
        else:
            st.info("Node B: Chưa rõ trạng thái")

    with c_a:
        node_a_ok = telem.get("node_a_online")
        if node_a_ok is True:
            st.success("Node A (ESP-NOW): **ONLINE**")
        elif node_a_ok is False:
            st.warning("Node A (ESP-NOW): **OFFLINE** (chỉ có trạng thái Node B)")
        else:
            st.info("Node A (ESP-NOW): Chờ dữ liệu...")

    # Hàng 1: Cảm biến (Node A)
    m1, m2, m3, m4 = st.columns(4)
    temp = telem.get("temperature")
    humi = telem.get("humidity")
    light = telem.get("light")
    presence = telem.get("presence")

    m1.metric("Nhiệt độ", f"{temp:.1f} °C" if isinstance(temp, (int, float)) else "—")
    m2.metric("Độ ẩm", f"{humi:.1f} %" if isinstance(humi, (int, float)) else "—")
    m3.metric("Ánh sáng (ADC)", f"{light}" if light is not None else "—")
    m4.metric(
        "Hiện diện",
        "Có người" if presence is True else ("Trống" if presence is False else "—"),
    )

    # Hàng 2: Chấp hành (Node B)
    s1, s2, s3 = st.columns(3)
    fan_lvl = telem.get("fan_level")
    light_on = telem.get("light_on")
    mode = telem.get("mode", "—")

    s1.metric("Mức quạt", f"Mức {fan_lvl}" if fan_lvl is not None else "—")
    s2.metric(
        "Đèn phòng",
        "Bật" if light_on is True else ("Tắt" if light_on is False else "—"),
    )
    s3.metric("Chế độ", str(mode).upper())

    # Phản hồi lệnh gần nhất
    st.caption("Phản hồi lệnh gần nhất (`room1/cmd/resp`):")
    if last_resp is not None:
        st.json(last_resp)
    else:
        st.text("Chưa có lệnh nào.")


live()

st.divider()
st.subheader("Điều khiển tại chỗ")

col1, col2, col3, col4 = st.columns([1, 1, 1, 1.5])
with col1:
    if st.button("Quạt 0 (Tắt)", use_container_width=True):
        send_cmd("setFan", 0)
with col2:
    if st.button("Quạt 1", use_container_width=True):
        send_cmd("setFan", 1)
with col3:
    if st.button("Quạt 2", use_container_width=True):
        send_cmd("setFan", 2)
with col4:
    def on_light_toggle():
        send_cmd("setLight", bool(st.session_state.light_toggle))

    st.session_state.setdefault(
        "light_toggle", bool(data["telemetry"].get("light_on", False))
    )
    st.toggle("Công tắc đèn", key="light_toggle", on_change=on_light_toggle)