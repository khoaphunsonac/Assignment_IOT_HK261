from datetime import datetime
import json
import os
from pathlib import Path
import threading
import time

import paho.mqtt.client as mqtt
import streamlit as st

MQTT_HOST = os.getenv("MQTT_HOST", "localhost")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
CLIENT_ID = "room1-ui"

# App/IOT/gateway/ui/app.py -> parents[2] = App/IOT
RAW_DATA_DIR = Path(__file__).resolve().parents[2] / "ai" / "data" / "raw"

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
        # Trạng thái thu dữ liệu TinyML (#48) - nằm ngoài st.session_state để thread MQTT đọc được
        "rec_active": False,
        "rec_label": 0,
        "rec_file": None,
        "rec_last_seq": None,
        "rec_count_0": 0,
        "rec_count_1": 0,
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

                # Ghi mẫu TinyML nếu đang bật ghi
                if data["rec_active"] and data["rec_file"] is not None:
                    temp = payload.get("temperature")
                    humi = payload.get("humidity")
                    seq = payload.get("seq")

                    has_sensor = (
                        isinstance(temp, (int, float))
                        and not isinstance(temp, bool)
                        and isinstance(humi, (int, float))
                        and not isinstance(humi, bool)
                    )
                    not_dup_seq = seq is None or seq != data["rec_last_seq"]

                    if has_sensor and not_dup_seq:
                        label = 1 if data["rec_label"] == 1 else 0
                        with open(data["rec_file"], "a", encoding="utf-8", newline="") as f:
                            f.write(f"{temp},{humi},{label}\n")
                        data["rec_last_seq"] = seq
                        if label == 1:
                            data["rec_count_1"] += 1
                        else:
                            data["rec_count_0"] += 1

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
        client.connect_async(MQTT_HOST, MQTT_PORT, keepalive=30)
        client.loop_start()
    except Exception as e:
        with lock:
            data["error"] = f"Lỗi cấu hình MQTT {MQTT_HOST}:{MQTT_PORT} ({e})"

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
        rec_active = data["rec_active"]
        rec_label = data["rec_label"]
        rec_file = data["rec_file"]
        rec_count_0 = data["rec_count_0"]
        rec_count_1 = data["rec_count_1"]

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

    # Trạng thái thu dữ liệu TinyML (#48)
    r1, r2, r3 = st.columns([2, 1, 1])
    with r1:
        if rec_active and rec_file is not None:
            st.info(
                f"Đang ghi vào `{rec_file.name}` — Nhãn hiện tại: **{rec_label}**"
            )
        elif rec_file is not None:
            st.caption(f"Đã dừng ghi. File gần nhất: `{rec_file.name}`")
        else:
            st.caption("Thu dữ liệu TinyML: Chưa bắt đầu ghi.")
    r2.metric("Mẫu nhãn 0 (Bình thường)", rec_count_0)
    r3.metric("Mẫu nhãn 1 (Bất thường)", rec_count_1)

    # Phản hồi lệnh gần nhất
    st.caption("Phản hồi lệnh gần nhất (`room1/cmd/resp`):")
    if last_resp is not None:
        st.json(last_resp)
    else:
        st.text("Chưa có lệnh nào.")


live()

st.divider()
st.subheader("Điều khiển tại chỗ")

col1, col2, col3, col4, col5, col6 = st.columns(6)
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
    if st.button("Bật đèn", use_container_width=True):
        send_cmd("setLight", True)
with col5:
    if st.button("Tắt đèn", use_container_width=True):
        send_cmd("setLight", False)
with col6:
    if st.button("Về auto", use_container_width=True):
        send_cmd("setMode", "auto")

st.divider()
st.subheader("Thu dữ liệu TinyML")

with data["lock"]:
    is_recording = data["rec_active"]
    current_label = data["rec_label"]

rc1, rc2 = st.columns([1, 2])
with rc1:
    if not is_recording:
        if st.button("Bắt đầu ghi", type="primary", use_container_width=True):
            RAW_DATA_DIR.mkdir(parents=True, exist_ok=True)
            csv_path = RAW_DATA_DIR / f"{datetime.now().strftime('%Y%m%d_%H%M')}.csv"
            with data["lock"]:
                data["rec_file"] = csv_path
                data["rec_last_seq"] = None
                data["rec_count_0"] = 0
                data["rec_count_1"] = 0
                data["rec_active"] = True
            st.rerun()
    else:
        if st.button("Dừng ghi", use_container_width=True):
            with data["lock"]:
                data["rec_active"] = False
            st.rerun()

with rc2:
    def on_anomaly_toggle():
        with data["lock"]:
            data["rec_label"] = 1 if st.session_state.anomaly_switch else 0

    st.session_state.setdefault("anomaly_switch", current_label == 1)
    st.toggle("Tạo bất thường", key="anomaly_switch", on_change=on_anomaly_toggle)