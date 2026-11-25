#!/usr/bin/env python3
"""
ota_publish.py - Công cụ CLI phát hành Firmware OTA thủ công tới Node B (Issue #45)
Cú pháp: python App/IOT/gateway/ota_publish.py <firmware_path> <version>
Ví dụ: python App/IOT/gateway/ota_publish.py fw/node_b_1.0.1.bin 1.0.1
Tác giả: Tuấn (TV2 - Gateway)
"""

import argparse
import json
import logging
import os
import sys
import time
from pathlib import Path

import paho.mqtt.client as mqtt

from ota import make_ota_msg

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] [OTA-Publish] %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger("ota_publish")


def load_env(path: Path):
    """Đọc file .env nếu có."""
    if not path.exists():
        return
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        os.environ.setdefault(key.strip(), value.strip().strip('"').strip("'"))


def create_mqtt_client() -> mqtt.Client:
    """Tạo MQTT client tương thích cả paho 1.x lẫn 2.x."""
    if hasattr(mqtt, "CallbackAPIVersion"):
        return mqtt.Client(mqtt.CallbackAPIVersion.VERSION1, client_id="room1-ota-publisher")
    return mqtt.Client(client_id="room1-ota-publisher")


def main():
    env_file = Path(__file__).with_name(".env")
    load_env(env_file)

    default_mqtt_host = os.getenv("MQTT_HOST", "localhost")
    default_mqtt_port = int(os.getenv("MQTT_PORT", "1883"))
    default_gw_ip = os.getenv("GATEWAY_IP", "127.0.0.1")
    default_ota_port = int(os.getenv("OTA_PORT", "8000"))

    parser = argparse.ArgumentParser(description="Phát hành Firmware OTA tới Node B qua topic room1/ota")
    parser.add_argument("path", type=str, help="Đường dẫn tới file firmware .bin (vd: fw/node_b_1.0.1.bin)")
    parser.add_argument("version", type=str, help="Phiên bản firmware (vd: 1.0.1)")
    parser.add_argument("--host", type=str, default=default_mqtt_host, help="MQTT Broker host (mặc định: localhost)")
    parser.add_argument("--port", type=int, default=default_mqtt_port, help="MQTT Broker port (mặc định: 1883)")
    parser.add_argument("--gateway-ip", type=str, default=default_gw_ip, help="IP của Gateway trong mạng LAN/Hotspot")
    parser.add_argument("--ota-port", type=int, default=default_ota_port, help="Cổng HTTP Server phục vụ file OTA (mặc định: 8000)")

    args = parser.parse_args()

    fw_path = Path(args.path)
    if not fw_path.is_file():
        # Thử tìm tương đối so với thư mục gateway
        alt_path = Path(__file__).parent / args.path
        if alt_path.is_file():
            fw_path = alt_path
        else:
            logger.error("Không tìm thấy file firmware tại: %s (hoặc %s)", args.path, alt_path)
            sys.exit(1)

    base_url = f"http://{args.gateway_ip}:{args.ota_port}"
    try:
        ota_msg = make_ota_msg(fw_path, args.version, base_url)
    except Exception as e:
        logger.error("Lỗi chuẩn bị bản tin OTA: %s", e)
        sys.exit(1)

    logger.info("Chuẩn bị phát hành Firmware OTA:")
    logger.info("  - File    : %s", fw_path.resolve())
    logger.info("  - Version : %s", ota_msg["version"])
    logger.info("  - Size    : %d bytes", ota_msg["size"])
    logger.info("  - SHA256  : %s", ota_msg["sha256"])
    logger.info("  - URL     : %s", ota_msg["url"])

    if args.gateway_ip in ("127.0.0.1", "localhost"):
        logger.warning("CẢNH BÁO: --gateway-ip đang là %s. Node B thật sẽ không thể tải file qua IP loopback này (cần chỉ định IP LAN/Hotspot)!", args.gateway_ip)

    client = create_mqtt_client()
    try:
        client.connect(args.host, args.port, keepalive=60)
        client.loop_start()
        time.sleep(0.2)

        payload_str = json.dumps(ota_msg)
        pub_info = client.publish("room1/ota", payload_str, qos=1)
        pub_info.wait_for_publish(timeout=5)
        logger.info("Đã publish thành công bản tin lên topic [room1/ota]: %s", payload_str)
    except Exception as e:
        logger.error("Không thể kết nối hoặc publish tới Broker %s:%d: %s", args.host, args.port, e)
        sys.exit(1)
    finally:
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()
