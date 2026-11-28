#!/usr/bin/env python3
"""
ota.py - Các hàm tiện ích và HTTP server phục vụ phát hành Firmware OTA (Issue #45)
Khớp 100% CONTRACT.md (mục 9) và TASKS_W2.md (mục 4).
Tác giả: Tuấn (TV2 - Gateway)
"""

import functools
import hashlib
import http.server
import logging
import os
import threading
from pathlib import Path
from typing import Any, Dict, Optional, Union

logger = logging.getLogger("gateway_ota")


def sha256_file(target: Union[str, Path, bytes, bytearray]) -> str:
    """
    Tính mã băm sha256 dạng chuỗi hex chữ thường, 64 ký tự.
    Hỗ trợ truyền vào đường dẫn file (str, Path) hoặc dữ liệu bytes trực tiếp.
    """
    hasher = hashlib.sha256()
    if isinstance(target, (bytes, bytearray)):
        hasher.update(target)
        return hasher.hexdigest().lower()

    path = Path(target)
    if not path.is_file():
        raise FileNotFoundError(f"Không tìm thấy file firmware: {path}")

    with open(path, "rb") as f:
        while True:
            chunk = f.read(8192)
            if not chunk:
                break
            hasher.update(chunk)

    return hasher.hexdigest().lower()


def make_ota_msg(path: Union[str, Path], version: str, base_url: str) -> Dict[str, Any]:
    """
    Tạo bản tin publish lên room1/ota đúng CONTRACT mục 9.
    Trả về dict gồm đúng 4 key: version, url, size, sha256.
    """
    p = Path(path)
    if not p.is_file():
        raise FileNotFoundError(f"Không tìm thấy file firmware: {p}")

    file_size = p.stat().st_size
    file_sha256 = sha256_file(p)
    file_name = p.name

    clean_base = base_url.rstrip("/")
    url = f"{clean_base}/fw/{file_name}"

    return {
        "version": str(version),
        "url": url,
        "size": file_size,
        "sha256": file_sha256,
    }


def map_ota_status(status: Dict[str, Any]) -> Dict[str, Any]:
    """
    Chuyển đổi trạng thái từ room1/ota/status sang telemetry CoreIOT cho room1-node-b.
    Đúng bảng quy đổi trong TASKS_W2.md mục 4:
      downloading -> fw_state: "DOWNLOADING"
      verifying   -> fw_state: "DOWNLOADED"
      rebooting   -> fw_state: "UPDATING"
      success     -> fw_state: "UPDATED"
      failed      -> fw_state: "FAILED" + fw_error = mã lỗi
    """
    state_str = str(status.get("state", "")).lower()

    if state_str == "downloading":
        return {"fw_state": "DOWNLOADING"}
    elif state_str == "verifying":
        return {"fw_state": "DOWNLOADED"}
    elif state_str == "rebooting":
        return {"fw_state": "UPDATING"}
    elif state_str == "success":
        result = {"fw_state": "UPDATED"}
        if "version" in status:
            result["fw_version"] = status["version"]
        return result
    elif state_str == "failed":
        err = status.get("error", "unknown_error")
        return {
            "fw_state": "FAILED",
            "fw_error": str(err),
        }
    else:
        return {}


class OTAHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    """
    Handler phục vụ file firmware trong thư mục fw_dir.
    Tự động chuẩn hóa đường dẫn để chấp nhận cả /fw/<file> lẫn /<file>,
    đồng thời chặn triệt để Path Traversal (..) để bảo vệ an toàn hệ thống.
    """

    def __init__(self, *args, directory: Optional[str] = None, **kwargs):
        self.fw_dir = os.path.abspath(directory or ".")
        super().__init__(*args, directory=self.fw_dir, **kwargs)

    def translate_path(self, path: str) -> str:
        clean = path.split("?", 1)[0].split("#", 1)[0]
        if clean.startswith("/fw/"):
            clean = clean[3:]
        return super().translate_path(clean)

    def log_message(self, format: str, *args):
        # Sử dụng logger chuẩn của Gateway thay vì in thô ra stderr
        logger.debug("OTA HTTP Server: %s - - [%s] " + format, self.address_string(), self.log_date_time_string(), *args)


def start_http_server(fw_dir: Union[str, Path], port: int = 8000) -> http.server.ThreadingHTTPServer:
    """
    Khởi động ThreadingHTTPServer phát file trong fw_dir trên thread riêng.
    Trả về server instance để có thể shutdown khi cần.
    """
    resolved_dir = Path(fw_dir).resolve()
    if not resolved_dir.is_dir():
        resolved_dir.mkdir(parents=True, exist_ok=True)

    handler = functools.partial(OTAHTTPRequestHandler, directory=str(resolved_dir))
    server = http.server.ThreadingHTTPServer(("0.0.0.0", int(port)), handler)

    t = threading.Thread(target=server.serve_forever, daemon=True)
    t.start()
    logger.info("OTA HTTP Server đã khởi động tại cổng %d (Phát file từ: %s)", port, resolved_dir)
    return server
