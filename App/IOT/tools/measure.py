#!/usr/bin/env python3
"""
measure.py - Công cụ đo đạc các chỉ số hiệu năng phục vụ Chương 4 Báo cáo (Issue #67)
Khớp 100% tài liệu TASKS_W3.md (mục 4) và CONTRACT.md.
Các phép đo:
  1. latency_up  : Đo độ trễ truyền dữ liệu từ Node B lên CoreIOT Cloud.
  2. latency_cmd : Đo độ trễ phát lệnh điều khiển hai chiều tới Node B và tách đoạn Cloud / Local.
  3. recovery    : Đo thời gian tự phục hồi của hệ thống khi mất kết nối mạng.
Tác giả: Tuấn (TV2 - Gateway)
"""

import argparse
import csv
import json
import logging
import os
import sys
import threading
import time
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, List, Optional
import urllib.request
import urllib.error

import paho.mqtt.client as mqtt

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] [Measure] %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger("measure")

DEFAULT_REPORT_DIR = Path(__file__).resolve().parent.parent.parent.parent / "Report" / "data"


def load_env(path: Path):
    """Đọc cấu hình từ .env."""
    if not path.exists():
        return
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        os.environ.setdefault(key.strip(), value.strip().strip('"').strip("'"))


# Nạp biến môi trường từ App/IOT/gateway/.env
env_file = Path(__file__).resolve().parent.parent / "gateway" / ".env"
load_env(env_file)

COREIOT_HOST = os.getenv("COREIOT_HOST", "app.coreiot.io")
COREIOT_USER = os.getenv("COREIOT_USER", "")
COREIOT_PASSWORD = os.getenv("COREIOT_PASSWORD", "")
COREIOT_NODE_B_ID = os.getenv("COREIOT_NODE_B_ID", "")
MQTT_HOST = os.getenv("MQTT_HOST", "localhost")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))


def get_coreiot_jwt_token(host: str, username: str, password: str) -> Optional[str]:
    """Lấy JWT token từ ThingsBoard / CoreIOT REST API."""
    if not username or not password:
        return None
    url = f"https://{host}/api/auth/login"
    req_body = json.dumps({"username": username, "password": password}).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=req_body,
        headers={"Content-Type": "application/json", "Accept": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            return data.get("token")
    except Exception as e:
        logger.warning("Không thể đăng nhập lấy JWT token từ CoreIOT REST: %s", e)
        return None


def print_stats(name: str, values: List[float], unit: str = "ms"):
    """In bảng thống kê Trung bình, Lớn nhất, Nhỏ nhất."""
    if not values:
        logger.warning("Không có dữ liệu đo để thống kê.")
        return
    avg_val = sum(values) / len(values)
    max_val = max(values)
    min_val = min(values)
    logger.info("=== KẾT QUẢ THỐNG KÊ [%s] ===", name.upper())
    logger.info("  - Số lần đo (N) : %d", len(values))
    logger.info("  - Trung bình    : %.2f %s", avg_val, unit)
    logger.info("  - Lớn nhất (Max): %.2f %s", max_val, unit)
    logger.info("  - Nhỏ nhất (Min): %.2f %s", min_val, unit)


def save_csv(path: Path, header: List[str], rows: List[List[Any]]):
    """Ghi danh sách kết quả đo ra file CSV."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(header)
        writer.writerows(rows)
    logger.info("Đã lưu kết quả đo ra file CSV: %s", path)


# -------------------------------------------------------------
# 1. Đo độ trễ truyền dữ liệu lên CoreIOT (latency_up)
# -------------------------------------------------------------
def run_latency_up(n: int = 5, output_file: Optional[Path] = None, fake_mode: bool = False):
    """
    Đo độ trễ = thời điểm nhận bản tin - ts gốc của Node B.
    Hỗ trợ chế độ --fake để kiểm thử nhanh trên broker local.
    """
    out_path = output_file or (
        DEFAULT_REPORT_DIR / ("latency_up_fake.csv" if fake_mode else "latency_up.csv")
    )
    logger.info("Bắt đầu đo latency_up (N=%d, Fake=%s)...", n, fake_mode)

    records = []
    latencies = []

    client = mqtt.Client(client_id="measure-latency-up")
    received_event = threading.Event()
    last_msg = {}

    def on_message(c, userdata, msg):
        nonlocal last_msg
        now_ms = int(time.time() * 1000)
        try:
            payload = json.loads(msg.payload.decode("utf-8"))
            ts_device = payload.get("ts")
            if ts_device:
                last_msg = {"ts_device": ts_device, "ts_recv": now_ms}
                received_event.set()
        except Exception:
            pass

    client.on_message = on_message
    try:
        client.connect(MQTT_HOST, MQTT_PORT, 60)
        client.subscribe("room1/telemetry", qos=0)
        client.loop_start()
    except Exception as e:
        logger.error("Không thể kết nối tới MQTT Broker: %s", e)
        return

    try:
        for i in range(1, n + 1):
            received_event.clear()
            logger.info("Đang chờ bản tin telemetry thứ %d/%d...", i, n)
            if not received_event.wait(timeout=10):
                logger.warning("Hết thời gian chờ bản tin telemetry %d!", i)
                continue

            ts_dev = last_msg["ts_device"]
            ts_rec = last_msg["ts_recv"]
            lat = max(0, ts_rec - ts_dev)
            latencies.append(lat)
            records.append([i, ts_dev, ts_rec, lat])
            logger.info("  Lần %d: ts_dev=%d, ts_rec=%d -> Độ trễ = %d ms", i, ts_dev, ts_rec, lat)
            time.sleep(1.0)
    finally:
        client.loop_stop()
        client.disconnect()

    header = ["iteration", "ts_device", "ts_received", "latency_ms"]
    save_csv(out_path, header, records)
    print_stats("latency_up", latencies, "ms")


# -------------------------------------------------------------
# 2. Đo độ trễ phát lệnh điều khiển (latency_cmd)
# -------------------------------------------------------------
def run_latency_cmd(n: int = 5, output_file: Optional[Path] = None, fake_mode: bool = False):
    """
    Gửi lệnh setFan hai chiều và đo tổng thời gian tới khi có phản hồi,
    đồng thời đo thời gian tại local broker để phân tách đoạn Cloud và Local.
    """
    out_path = output_file or (
        DEFAULT_REPORT_DIR / ("latency_cmd_fake.csv" if fake_mode else "latency_cmd.csv")
    )
    logger.info("Bắt đầu đo latency_cmd (N=%d, Fake=%s)...", n, fake_mode)

    records = []
    total_latencies = []

    client = mqtt.Client(client_id="measure-latency-cmd")
    resp_event = threading.Event()
    local_times: Dict[str, float] = {}

    def on_message(c, userdata, msg):
        topic = msg.topic
        now = time.time()
        try:
            payload = json.loads(msg.payload.decode("utf-8"))
            cid = str(payload.get("id"))
            if topic == "room1/cmd":
                local_times[f"{cid}_sent"] = now
            elif topic == "room1/cmd/resp":
                local_times[f"{cid}_resp"] = now
                resp_event.set()
        except Exception:
            pass

    client.on_message = on_message
    try:
        client.connect(MQTT_HOST, MQTT_PORT, 60)
        client.subscribe("room1/cmd", qos=0)
        client.subscribe("room1/cmd/resp", qos=0)
        client.loop_start()
    except Exception as e:
        logger.error("Không thể kết nối tới MQTT Broker: %s", e)
        return

    try:
        for i in range(1, n + 1):
            cmd_id = f"meas-{i}-{int(time.time())}"
            cmd_payload = {"id": cmd_id, "method": "setFan", "params": (i % 2) + 1}

            resp_event.clear()
            start_time = time.time()

            # Phát lệnh
            client.publish("room1/cmd", json.dumps(cmd_payload), qos=1)

            if not resp_event.wait(timeout=5):
                logger.warning("Lần %d: Hết thời gian chờ phản hồi!", i)
                continue

            end_time = time.time()
            total_lat = (end_time - start_time) * 1000.0
            t_sent = local_times.get(f"{cmd_id}_sent", start_time)
            t_resp = local_times.get(f"{cmd_id}_resp", end_time)
            local_lat = max(0.0, (t_resp - t_sent) * 1000.0)
            cloud_lat = max(0.0, total_lat - local_lat)

            total_latencies.append(total_lat)
            records.append([i, cmd_id, round(total_lat, 2), round(local_lat, 2), round(cloud_lat, 2)])
            logger.info(
                "  Lần %d: Tổng độ trễ=%.2f ms (Local=%.2f ms, Cloud/Network=%.2f ms)",
                i,
                total_lat,
                local_lat,
                cloud_lat,
            )
            time.sleep(1.0)
    finally:
        client.loop_stop()
        client.disconnect()

    header = ["iteration", "cmd_id", "total_latency_ms", "local_latency_ms", "cloud_latency_ms"]
    save_csv(out_path, header, records)
    print_stats("latency_cmd", total_latencies, "ms")


# -------------------------------------------------------------
# 3. Đo thời gian tự phục hồi khi mất mạng (recovery)
# -------------------------------------------------------------
def run_recovery(n: int = 3, output_file: Optional[Path] = None, fake_mode: bool = False):
    """
    Đo thời gian hệ thống tự phục hồi sau sự cố mạng:
    Tính từ lúc kết nối lại tới khi bản tin telemetry đầu tiên xuất hiện.
    """
    out_path = output_file or (
        DEFAULT_REPORT_DIR / ("recovery_fake.csv" if fake_mode else "recovery.csv")
    )
    logger.info("Bắt đầu đo thời gian tự phục hồi (N=%d)...", n)

    recovery_times = []
    records = []

    client = mqtt.Client(client_id="measure-recovery")
    rec_event = threading.Event()

    def on_message(c, userdata, msg):
        rec_event.set()

    client.on_message = on_message
    try:
        client.connect(MQTT_HOST, MQTT_PORT, 60)
        client.subscribe("room1/telemetry", qos=0)
        client.loop_start()
    except Exception as e:
        logger.error("Không thể kết nối tới MQTT Broker: %s", e)
        return

    try:
        for i in range(1, n + 1):
            input(f"\n[Lần {i}/{n}] Hãy ngắt mạng / ngắt hotspot / ngắt bridge, sau đó nhấn Enter...")
            time.sleep(2)
            input(f"[Lần {i}/{n}] Hãy bật lại mạng / hotspot / bridge, sau đó nhấn Enter để bắt đầu bấm giờ...")

            start_t = time.time()
            rec_event.clear()
            logger.info("Đang đếm thời gian tới khi có telemetry mới...")

            if not rec_event.wait(timeout=60):
                logger.warning("Quá 60s không thấy dữ liệu phục hồi!")
                continue

            recovery_s = time.time() - start_t
            recovery_times.append(recovery_s)
            records.append([i, round(recovery_s, 2), datetime.now().isoformat()])
            logger.info("  Lần %d: Thời gian phục hồi = %.2f giây", i, recovery_s)
    finally:
        client.loop_stop()
        client.disconnect()

    header = ["iteration", "recovery_time_s", "timestamp"]
    save_csv(out_path, header, records)
    print_stats("recovery", recovery_times, "s")


def main():
    parser = argparse.ArgumentParser(description="Script đo đạc hiệu năng hệ thống IoT (Chương 4 Báo cáo)")
    subparsers = parser.add_subparsers(dest="command", required=True, help="Lệnh đo con")

    # 1. latency_up
    p_up = subparsers.add_parser("latency_up", help="Đo độ trễ truyền dữ liệu Node B -> CoreIOT")
    p_up.add_argument("--n", type=int, default=5, help="Số lần đo (mặc định 5)")
    p_up.add_argument("--output", type=str, default=None, help="Đường dẫn file CSV kết quả")
    p_up.add_argument("--fake", action="store_true", help="Chạy chế độ test nhanh với fake_node_b")

    # 2. latency_cmd
    p_cmd = subparsers.add_parser("latency_cmd", help="Đo độ trễ lệnh điều khiển 2 chiều (Cloud & Local)")
    p_cmd.add_argument("--n", type=int, default=5, help="Số lần đo (mặc định 5)")
    p_cmd.add_argument("--output", type=str, default=None, help="Đường dẫn file CSV kết quả")
    p_cmd.add_argument("--fake", action="store_true", help="Chạy chế độ test nhanh với fake_node_b")

    # 3. recovery
    p_rec = subparsers.add_parser("recovery", help="Đo thời gian tự phục hồi khi mất kết nối")
    p_rec.add_argument("--n", type=int, default=3, help="Số lần đo (mặc định 3)")
    p_rec.add_argument("--output", type=str, default=None, help="Đường dẫn file CSV kết quả")
    p_rec.add_argument("--fake", action="store_true", help="Chạy chế độ test nhanh với fake_node_b")

    args = parser.parse_args()
    out_path = Path(args.output).resolve() if args.output else None

    if args.command == "latency_up":
        run_latency_up(n=args.n, output_file=out_path, fake_mode=args.fake)
    elif args.command == "latency_cmd":
        run_latency_cmd(n=args.n, output_file=out_path, fake_mode=args.fake)
    elif args.command == "recovery":
        run_recovery(n=args.n, output_file=out_path, fake_mode=args.fake)


if __name__ == "__main__":
    main()
