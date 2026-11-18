#!/usr/bin/env python3
"""
bridge.py - Cầu nối Gateway giữa Local MQTT (Mosquitto) và CoreIOT Cloud
Khớp 100% tài liệu CONTRACT.md (mục 8, 11) và TASKS_W1.md (Issue #28 - Tiêu chí T15).
Tác giả: Tuấn (TV2 - Gateway)
"""

import json
import logging
import os
import threading
import time
from pathlib import Path
from typing import Any, Dict, Optional

import paho.mqtt.client as mqtt

from ota import make_ota_msg, map_ota_status, start_http_server

# Đảm bảo kết nối nội bộ localhost không bị proxy can thiệp
no_proxy_curr = os.environ.get("no_proxy", "")
if "localhost" not in no_proxy_curr:
    os.environ["no_proxy"] = (no_proxy_curr + ",localhost,127.0.0.1").strip(",")

# Cấu hình logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] [Gateway] %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger("gateway_bridge")


def load_env(path: Path):
    """Đọc file .env không cần cài thêm thư viện phụ thuộc."""
    if not path.exists():
        return
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        os.environ.setdefault(key.strip(), value.strip().strip('"').strip("'"))


# Nạp cấu hình từ .env
env_path = Path(__file__).with_name(".env")
load_env(env_path)

# Cấu hình Local MQTT Broker
LOCAL_HOST = os.getenv("MQTT_HOST", "localhost")
LOCAL_PORT = int(os.getenv("MQTT_PORT", "1883"))
LOCAL_CLIENT_ID = "room1-gateway"

# Cấu hình CoreIOT Cloud
COREIOT_HOST = os.getenv("COREIOT_HOST", "app.coreiot.io")
COREIOT_PORT = int(os.getenv("COREIOT_PORT", "1883"))
COREIOT_TOKEN = os.getenv("COREIOT_TOKEN", "")

# Cấu hình OTA (CONTRACT mục 9 & TASKS_W2 mục 4)
GATEWAY_IP = os.getenv("GATEWAY_IP", "127.0.0.1")
OTA_PORT = int(os.getenv("OTA_PORT", "8000"))

# Tên thiết bị con trên CoreIOT (CONTRACT mục 11.1)
DEVICE_NODE_A = "room1-node-a"
DEVICE_NODE_B = "room1-node-b"

# Danh sách key thuộc từng thiết bị (CONTRACT mục 11.2)
NODE_A_KEYS = {
    "temperature", "humidity", "light", "presence",
    "anomaly", "anomaly_score", "seq", "dht_fail", "espnow_loss"
}
NODE_B_KEYS = {
    "fan_level", "light_on", "mode", "rssi", "fw_version", "uptime_s"
}

# Cấu hình ngưỡng mặc định (CONTRACT mục 7)
config_cache = {
    "fan_on_1": 31.0,
    "fan_off_1": 29.0,
    "fan_on_2": 33.0,
    "fan_off_2": 31.0,
    "light_dark": 1500,
    "light_bright": 2500,
    "empty_timeout_s": 600,
    "manual_timeout_s": 1800,
}
config_lock = threading.Lock()

# Quản lý trạng thái online/offline của thiết bị con (CONTRACT mục 11.3)
node_a_online: Optional[bool] = None
node_b_online: Optional[bool] = None

# Theo dõi pending RPC từ CoreIOT để ghép cặp phản hồi (CONTRACT mục 11.4)
# key: local_cmd_id ("tb-<coreiot_id>"), value: {"coreiot_id": int/str, "device": str, "time": float}
pending_rpc: Dict[str, Dict[str, Any]] = {}
rpc_lock = threading.Lock()
RPC_TIMEOUT_S = 5.0


def split_telemetry(payload: Dict[str, Any]) -> Dict[str, Any]:
    """
    Tách telemetry local (room1/telemetry) thành bản tin của 2 thiết bị con
    đúng chuẩn v1/gateway/telemetry của CoreIOT (CONTRACT mục 11.2).
    """
    ts = payload.get("ts", int(time.time() * 1000))
    result: Dict[str, Any] = {}

    # Lọc các key của Node A
    values_a = {k: payload[k] for k in NODE_A_KEYS if k in payload}
    if values_a:
        result[DEVICE_NODE_A] = [{"ts": ts, "values": values_a}]

    # Lọc các key của Node B
    values_b = {k: payload[k] for k in NODE_B_KEYS if k in payload}
    if values_b:
        result[DEVICE_NODE_B] = [{"ts": ts, "values": values_b}]

    return result


def validate_config(cfg: Dict[str, Any]) -> bool:
    """Kiểm tra ràng buộc cấu hình ngưỡng theo CONTRACT mục 7."""
    try:
        f_off_1 = float(cfg["fan_off_1"])
        f_on_1 = float(cfg["fan_on_1"])
        f_off_2 = float(cfg["fan_off_2"])
        f_on_2 = float(cfg["fan_on_2"])
        l_dark = int(cfg["light_dark"])
        l_bright = int(cfg["light_bright"])
        return (f_off_1 < f_on_1 <= f_off_2 < f_on_2) and (l_dark < l_bright)
    except (KeyError, ValueError, TypeError):
        return False


class GatewayBridge:
    def __init__(self):
        self.local_client = self._create_local_client()
        self.cloud_client = self._create_cloud_client()
        self.running = False
        self.timeout_thread: Optional[threading.Thread] = None
        self.http_server: Optional[Any] = None

    def _create_mqtt_instance(self, client_id: str = "") -> mqtt.Client:
        """Hỗ trợ cả paho-mqtt 1.6.1 lẫn 2.x."""
        if hasattr(mqtt, "CallbackAPIVersion"):
            return mqtt.Client(mqtt.CallbackAPIVersion.VERSION1, client_id=client_id)
        return mqtt.Client(client_id=client_id)

    def _create_local_client(self) -> mqtt.Client:
        client = self._create_mqtt_instance(LOCAL_CLIENT_ID)
        # Last Will của Gateway trên Local broker (CONTRACT mục 8)
        client.will_set("room1/gateway/status", json.dumps({"state": "offline"}), qos=1, retain=True)
        client.on_connect = self._on_local_connect
        client.on_message = self._on_local_message
        return client

    def _create_cloud_client(self) -> mqtt.Client:
        # ThingsBoard Gateway không bắt buộc client_id cố định nhưng cần username là Token
        client = self._create_mqtt_instance("room1-gateway-cloud")
        if COREIOT_TOKEN:
            client.username_pw_set(COREIOT_TOKEN, password=None)
        client.on_connect = self._on_cloud_connect
        client.on_disconnect = self._on_cloud_disconnect
        client.on_message = self._on_cloud_message
        return client

    # -------------------------------------------------------------
    # Callbacks cho Local MQTT Client
    # -------------------------------------------------------------
    def _on_local_connect(self, client: mqtt.Client, userdata, flags, rc):
        if rc == 0:
            logger.info("Đã kết nối tới Local Broker (%s:%d)", LOCAL_HOST, LOCAL_PORT)
            # Thông báo trạng thái online của Gateway (retained)
            client.publish("room1/gateway/status", json.dumps({"state": "online"}), qos=1, retain=True)

            # Subscribe các topic local cần trung chuyển
            client.subscribe("room1/telemetry", qos=0)
            client.subscribe("room1/cmd/resp", qos=0)
            client.subscribe("room1/status", qos=1)
            client.subscribe("room1/event", qos=0)
            client.subscribe("room1/ota/status", qos=0)
            logger.info("Local: Đã subscribe room1/telemetry, room1/cmd/resp, room1/status, room1/event, room1/ota/status")
        else:
            logger.error("Local: Kết nối thất bại với mã rc=%s", rc)

    def _on_local_message(self, client: mqtt.Client, userdata, msg):
        topic = msg.topic
        payload_str = msg.payload.decode("utf-8", errors="replace")

        try:
            data = json.loads(payload_str)
        except Exception:
            logger.warning("Local: Không parse được JSON từ topic %s: %s", topic, payload_str)
            return

        # 1. Xử lý Telemetry từ Node B
        if topic == "room1/telemetry":
            self._handle_local_telemetry(data)

        # 2. Xử lý phản hồi lệnh từ Node B (trả lời RPC cho CoreIOT)
        elif topic == "room1/cmd/resp":
            self._handle_local_cmd_resp(data)

        # 3. Theo dõi trạng thái online/offline của Node B (CONTRACT mục 11.3)
        elif topic == "room1/status":
            self._handle_local_node_b_status(data)

        # 4. Trung chuyển sự kiện AI lên CoreIOT (CONTRACT mục 11.6)
        elif topic == "room1/event":
            self._handle_local_event(data)

        # 5. Xử lý trạng thái OTA từ Node B (CONTRACT mục 9 & TASKS_W2 mục 4)
        elif topic == "room1/ota/status":
            self._handle_local_ota_status(data)

    def _handle_local_ota_status(self, data: Dict[str, Any]):
        """Chuyển tiếp trạng thái OTA từ Node B lên CoreIOT telemetry (CONTRACT mục 9 & TASKS_W2 mục 4)."""
        mapped = map_ota_status(data)
        if not mapped:
            return
        ts = data.get("ts", int(time.time() * 1000))
        telemetry_payload = {
            DEVICE_NODE_B: [
                {
                    "ts": ts,
                    "values": mapped,
                }
            ]
        }
        self.cloud_client.publish("v1/gateway/telemetry", json.dumps(telemetry_payload), qos=0)
        logger.info("CoreIOT OTA Telemetry -> %s: %s", DEVICE_NODE_B, mapped)

    def _handle_local_telemetry(self, data: Dict[str, Any]):
        global node_a_online
        # Cập nhật trạng thái online của Node A nếu có trường node_a_online
        if "node_a_online" in data:
            current_a = bool(data["node_a_online"])
            if node_a_online != current_a:
                node_a_online = current_a
                event_topic = "v1/gateway/connect" if current_a else "v1/gateway/disconnect"
                self.cloud_client.publish(event_topic, json.dumps({"device": DEVICE_NODE_A}), qos=1)
                logger.info("CoreIOT: %s -> %s", DEVICE_NODE_A, "connect" if current_a else "disconnect")

        # Tách telemetry cho 2 thiết bị và đẩy lên CoreIOT
        gateway_telemetry = split_telemetry(data)
        if gateway_telemetry:
            payload_to_cloud = json.dumps(gateway_telemetry)
            self.cloud_client.publish("v1/gateway/telemetry", payload_to_cloud, qos=0)
            logger.debug("CoreIOT Telemetry: %s", payload_to_cloud)

    def _handle_local_cmd_resp(self, data: Dict[str, Any]):
        """Node B trả lời room1/cmd/resp -> Chuyển tiếp kết quả lên CoreIOT v1/gateway/rpc."""
        cmd_id = data.get("id")
        if not cmd_id:
            return

        with rpc_lock:
            info = pending_rpc.pop(str(cmd_id), None)

        if not info:
            # Không phải RPC từ CoreIOT (có thể từ UI Streamlit hoặc test bằng tay)
            return

        coreiot_id = info["coreiot_id"]
        device = info.get("device", DEVICE_NODE_B)

        # Dữ liệu trả lời cho CoreIOT (CONTRACT mục 11.4):
        # {"device": "room1-node-b", "id": 17, "data": {"ok": true, "fan_level": 2, ...}}
        rpc_resp_data = {"ok": data.get("ok", True)}
        if "error" in data:
            rpc_resp_data["error"] = data["error"]

        # Trích xuất state nếu có
        state = data.get("state", {})
        if isinstance(state, dict):
            for k in ("fan_level", "light_on", "mode"):
                if k in state:
                    rpc_resp_data[k] = state[k]

        coreiot_rpc_response = {
            "device": device,
            "id": coreiot_id,
            "data": rpc_resp_data,
        }
        self.cloud_client.publish("v1/gateway/rpc", json.dumps(coreiot_rpc_response), qos=1)
        logger.info("CoreIOT RPC Response -> Device %s, ID %s: %s", device, coreiot_id, rpc_resp_data)

    def _handle_local_node_b_status(self, data: Dict[str, Any]):
        global node_b_online
        state = data.get("state")
        is_online = (state == "online")

        if node_b_online != is_online:
            node_b_online = is_online
            event_topic = "v1/gateway/connect" if is_online else "v1/gateway/disconnect"
            self.cloud_client.publish(event_topic, json.dumps({"device": DEVICE_NODE_B}), qos=1)
            logger.info("CoreIOT: %s -> %s", DEVICE_NODE_B, "connect" if is_online else "disconnect")

    def _handle_local_event(self, data: Dict[str, Any]):
        """Chuyển tiếp sự kiện AI lên CoreIOT (CONTRACT mục 11.6)."""
        ts = data.get("ts", int(time.time() * 1000))
        event_payload = {
            "ts": ts,
            "values": {
                "event_type": data.get("type", "unknown"),
                "event_level": data.get("level", "info"),
                "event_msg": data.get("msg", ""),
            }
        }
        self.cloud_client.publish("v1/devices/me/telemetry", json.dumps(event_payload), qos=1)
        logger.info("CoreIOT AI Event: %s", event_payload)

    # -------------------------------------------------------------
    # Callbacks cho CoreIOT Cloud Client
    # -------------------------------------------------------------
    def _on_cloud_connect(self, client: mqtt.Client, userdata, flags, rc):
        if rc == 0:
            logger.info("Đã kết nối tới CoreIOT Cloud (%s:%d)", COREIOT_HOST, COREIOT_PORT)

            # 1. Đăng ký kết nối 2 thiết bị con (CONTRACT mục 11.3)
            client.publish("v1/gateway/connect", json.dumps({"device": DEVICE_NODE_A}), qos=1)
            client.publish("v1/gateway/connect", json.dumps({"device": DEVICE_NODE_B}), qos=1)
            logger.info("CoreIOT: Đã gửi v1/gateway/connect cho %s và %s", DEVICE_NODE_A, DEVICE_NODE_B)

            # 2. Subscribe nhận RPC điều khiển từ CoreIOT
            client.subscribe("v1/gateway/rpc", qos=1)

            # 3. Subscribe nhận cập nhật cấu hình ngưỡng từ CoreIOT (CONTRACT mục 11.5)
            client.subscribe("v1/gateway/attributes", qos=1)
            client.subscribe("v1/gateway/attributes/response", qos=1)

            # 4. Yêu cầu lấy toàn bộ shared attributes hiện tại của room1-node-b (CONTRACT mục 11.5)
            attr_req = {
                "device": DEVICE_NODE_B,
                "id": 1,
                "client": False,
                "keys": list(config_cache.keys()),
            }
            client.publish("v1/gateway/attributes/request", json.dumps(attr_req), qos=1)
            logger.info("CoreIOT: Đã gửi request lấy Shared Attributes cho %s (keys: %s)", DEVICE_NODE_B, list(config_cache.keys()))
            logger.info("CoreIOT: Đã subscribe v1/gateway/rpc và v1/gateway/attributes")
        else:
            logger.error("CoreIOT: Kết nối thất bại với mã rc=%s (Kiểm tra lại COREIOT_TOKEN trong .env)", rc)

    def _on_cloud_disconnect(self, client: mqtt.Client, userdata, rc):
        if rc == 0:
            logger.info("CoreIOT: Đã ngắt kết nối chủ động (rc=0)")
        else:
            logger.warning("CoreIOT: Bị ngắt kết nối bất thường (rc=%s). Paho sẽ tự động kết nối lại...", rc)

    def _on_cloud_message(self, client: mqtt.Client, userdata, msg):
        topic = msg.topic
        payload_str = msg.payload.decode("utf-8", errors="replace")

        try:
            data = json.loads(payload_str)
        except Exception:
            logger.warning("CoreIOT: Không parse được JSON từ topic %s: %s", topic, payload_str)
            return

        # 1. Xử lý RPC từ CoreIOT gửi xuống thiết bị con
        if topic == "v1/gateway/rpc":
            logger.info("Nhận tin từ CoreIOT [v1/gateway/rpc]: %s", payload_str)
            self._handle_cloud_rpc(data)

        # 2. Xử lý cập nhật Shared Attributes (ngưỡng)
        elif topic in ("v1/gateway/attributes", "v1/gateway/attributes/response"):
            logger.info("Nhận tin từ CoreIOT [%s]: %s", topic, payload_str)
            self._handle_cloud_attributes(data)

    def _handle_cloud_rpc(self, data: Dict[str, Any]):
        """
        Nhận RPC từ CoreIOT:
        {"device": "room1-node-b", "data": {"id": 17, "method": "setFan", "params": 2}}
        -> Chuyển thành room1/cmd:
        {"id": "tb-17", "method": "setFan", "params": 2}
        """
        device = data.get("device", DEVICE_NODE_B)
        rpc_data = data.get("data", {})
        coreiot_id = rpc_data.get("id")
        method = rpc_data.get("method")
        params = rpc_data.get("params")

        if coreiot_id is None or method is None:
            # Có thể là echo response từ chính gateway
            return

        # Chuẩn hóa params nếu nhận dạng chuỗi từ widget trên dashboard
        if method in ("setFan", "autoFan") and isinstance(params, str):
            try:
                params = int(params)
            except (ValueError, TypeError):
                pass
        elif method in ("setLight", "autoLight") and isinstance(params, str):
            if params.lower() in ("true", "1"):
                params = True
            elif params.lower() in ("false", "0"):
                params = False
        elif method == "setMode" and isinstance(params, str):
            params = params.strip('"\'')

        local_cmd_id = f"tb-{coreiot_id}"

        # Ghi nhận pending RPC để chờ Node B trả lời trong vòng 5 giây
        with rpc_lock:
            pending_rpc[local_cmd_id] = {
                "coreiot_id": coreiot_id,
                "device": device,
                "timestamp": time.time(),
            }

        local_cmd = {
            "id": local_cmd_id,
            "method": method,
        }
        if params is not None:
            local_cmd["params"] = params

        # Gửi xuống Node B qua Local MQTT
        self.local_client.publish("room1/cmd", json.dumps(local_cmd), qos=1)
        logger.info("Chuyển tiếp RPC sang Local: id=%s, method=%s, params=%s", local_cmd_id, method, params)

    def _handle_cloud_attributes(self, data: Dict[str, Any]):
        """Đồng bộ ngưỡng cấu hình (Shared Attributes) từ CoreIOT xuống room1/config."""
        device = data.get("device")
        # Chỉ xử lý cấu hình cho room1-node-b
        if device and device != DEVICE_NODE_B:
            return

        attr_data = data.get("data", {})
        if not attr_data and "values" in data:
            attr_data = data["values"]

        if not attr_data or not isinstance(attr_data, dict):
            return

        with config_lock:
            updated = False
            for k in config_cache:
                if k in attr_data:
                    config_cache[k] = attr_data[k]
                    updated = True

            if updated:
                if validate_config(config_cache):
                    # Publish retained room1/config cho Node B
                    self.local_client.publish("room1/config", json.dumps(config_cache), qos=1, retain=True)
                    logger.info("Đã đồng bộ Shared Attributes từ CoreIOT xuống room1/config: %s", config_cache)
                else:
                    logger.warning("Shared Attributes không thỏa mãn ràng buộc CONTRACT mục 7: %s", config_cache)

        # Kiểm tra nếu CoreIOT kích hoạt cập nhật OTA (kết quả #29 & #45)
        fw_version = attr_data.get("fw_version")
        if fw_version:
            self._trigger_ota_from_cloud(str(fw_version), attr_data)

    def _trigger_ota_from_cloud(self, fw_version: str, attr_data: Dict[str, Any]):
        """Tự động phát hành OTA tới room1/ota khi nhận thuộc tính fw_version từ CoreIOT (#29 & #45)."""
        logger.info("CoreIOT OTA Trigger nhận được: version=%s, title=%s", fw_version, attr_data.get("fw_title"))
        fw_dir = Path(__file__).parent / "fw"
        matching_file = None
        if fw_dir.is_dir():
            for f in fw_dir.glob("*.bin"):
                if fw_version in f.name:
                    matching_file = f
                    break
            if not matching_file:
                bin_files = list(fw_dir.glob("*.bin"))
                if bin_files:
                    matching_file = bin_files[0]

        if not matching_file:
            logger.warning("CoreIOT yêu cầu OTA version %s nhưng không tìm thấy file .bin nào trong %s", fw_version, fw_dir)
            return

        base_url = f"http://{GATEWAY_IP}:{OTA_PORT}"
        try:
            ota_msg = make_ota_msg(matching_file, fw_version, base_url)
            self.local_client.publish("room1/ota", json.dumps(ota_msg), qos=1)
            logger.info("Tự động phát hành OTA tới room1/ota thành công: %s", ota_msg)
        except Exception as e:
            logger.error("Lỗi khi tự động phát hành OTA: %s", e)

    # -------------------------------------------------------------
    # Quản lý vòng đời và kiểm tra Timeout RPC
    # -------------------------------------------------------------
    def _timeout_worker(self):
        while self.running:
            time.sleep(0.5)
            now = time.time()
            timed_out = []

            with rpc_lock:
                for local_id, info in list(pending_rpc.items()):
                    if now - info["timestamp"] >= RPC_TIMEOUT_S:
                        timed_out.append(info)
                        del pending_rpc[local_id]

            for info in timed_out:
                coreiot_id = info["coreiot_id"]
                device = info.get("device", DEVICE_NODE_B)
                timeout_resp = {
                    "device": device,
                    "id": coreiot_id,
                    "data": {"ok": False, "error": "timeout"},
                }
                self.cloud_client.publish("v1/gateway/rpc", json.dumps(timeout_resp), qos=1)
                logger.warning("RPC Timeout (> 5s) -> Trả về lỗi timeout cho CoreIOT ID %s", coreiot_id)

    def start(self):
        logger.info("=== Khởi động Gateway Bridge (Tuấn - TV2) ===")
        logger.info("Local MQTT : %s:%d (Client ID: %s)", LOCAL_HOST, LOCAL_PORT, LOCAL_CLIENT_ID)
        logger.info("CoreIOT    : %s:%d (Token: %s)", COREIOT_HOST, COREIOT_PORT, "Đã có" if COREIOT_TOKEN else "CHƯA CÓ")
        logger.info("OTA Server : http://%s:%d/fw/ (Phát file firmware)", GATEWAY_IP, OTA_PORT)

        if not COREIOT_TOKEN:
            logger.warning("CẢNH BÁO: Chưa cấu hình COREIOT_TOKEN trong .env!")
            logger.warning("Vui lòng tạo Gateway trên app.coreiot.io và dán Access Token vào .env")

        self.running = True

        # Khởi động HTTP Server phục vụ file OTA trên thread riêng
        try:
            fw_dir = Path(__file__).parent / "fw"
            self.http_server = start_http_server(fw_dir, OTA_PORT)
        except Exception as e:
            logger.warning("Không thể khởi động HTTP Server OTA tại cổng %d: %s", OTA_PORT, e)

        self.timeout_thread = threading.Thread(target=self._timeout_worker, daemon=True)
        self.timeout_thread.start()

        # Kết nối Local broker
        try:
            self.local_client.connect(LOCAL_HOST, LOCAL_PORT, 60)
            self.local_client.loop_start()
        except Exception as e:
            logger.error("Không thể kết nối tới Local MQTT Broker: %s", e)

        # Kết nối CoreIOT Cloud
        if COREIOT_TOKEN:
            try:
                self.cloud_client.connect(COREIOT_HOST, COREIOT_PORT, 60)
                self.cloud_client.loop_start()
            except Exception as e:
                logger.error("Không thể kết nối tới CoreIOT Cloud: %s", e)

        try:
            while self.running:
                time.sleep(1)
        except KeyboardInterrupt:
            logger.info("Nhận tín hiệu dừng (Ctrl+C)...")
        finally:
            self.stop()

    def stop(self):
        self.running = False
        logger.info("Đang ngắt kết nối các client...")
        try:
            self.local_client.publish("room1/gateway/status", json.dumps({"state": "offline"}), qos=1, retain=True)
            time.sleep(0.3)
            self.local_client.loop_stop()
            self.local_client.disconnect()
        except Exception:
            pass

        try:
            self.cloud_client.loop_stop()
            self.cloud_client.disconnect()
        except Exception:
            pass

        if self.http_server:
            try:
                self.http_server.shutdown()
                self.http_server.server_close()
                logger.info("OTA HTTP Server đã dừng.")
            except Exception:
                pass

        logger.info("Gateway Bridge đã dừng.")


if __name__ == "__main__":
    bridge = GatewayBridge()
    bridge.start()
