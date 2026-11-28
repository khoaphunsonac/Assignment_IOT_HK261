"""
test_ota.py - Kiểm thử tự động cho module OTA Gateway (Issue #45)
Khớp 100% CONTRACT mục 9 và TASKS_W2 mục 4.
"""

import tempfile
import unittest
import urllib.error
import urllib.request
from pathlib import Path

from ota import make_ota_msg, map_ota_status, sha256_file, start_http_server


class TestGatewayOTA(unittest.TestCase):
    def test_sha256_file_and_bytes(self):
        """Kiểm tra tính mã băm sha256: b'abc' ra đúng giá trị chuẩn."""
        expected = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        # 1. Test với bytes trực tiếp
        self.assertEqual(sha256_file(b"abc"), expected)

        # 2. Test với file thực tế
        with tempfile.NamedTemporaryFile(delete=False) as tf:
            tf.write(b"abc")
            tf_path = Path(tf.name)

        try:
            self.assertEqual(sha256_file(tf_path), expected)
        finally:
            if tf_path.exists():
                tf_path.unlink()

    def test_map_ota_status_all_states(self):
        """Kiểm tra map_ota_status đủ 5 trạng thái theo CONTRACT mục 9 và TASKS_W2."""
        # 1. downloading -> DOWNLOADING
        res_down = map_ota_status({"version": "1.0.1", "state": "downloading", "progress": 40})
        self.assertEqual(res_down, {"fw_state": "DOWNLOADING"})

        # 2. verifying -> DOWNLOADED
        res_ver = map_ota_status({"version": "1.0.1", "state": "verifying"})
        self.assertEqual(res_ver, {"fw_state": "DOWNLOADED"})

        # 3. rebooting -> UPDATING
        res_reb = map_ota_status({"version": "1.0.1", "state": "rebooting"})
        self.assertEqual(res_reb, {"fw_state": "UPDATING"})

        # 4. success -> UPDATED (+ fw_version)
        res_succ = map_ota_status({"version": "1.0.1", "state": "success"})
        self.assertEqual(res_succ.get("fw_state"), "UPDATED")
        self.assertEqual(res_succ.get("fw_version"), "1.0.1")

        # 5. failed -> FAILED (+ fw_error)
        res_fail = map_ota_status({"version": "1.0.1", "state": "failed", "error": "checksum_mismatch"})
        self.assertEqual(res_fail, {"fw_state": "FAILED", "fw_error": "checksum_mismatch"})

        # 6. unknown state -> trả dict rỗng để bridge bỏ qua
        res_un = map_ota_status({"state": "other"})
        self.assertEqual(res_un, {})

    def test_make_ota_msg(self):
        """Kiểm tra make_ota_msg có đủ 4 key, size đúng và url chuẩn."""
        dummy_content = b"Firmware Binary Content Sample 12345"
        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tf:
            tf.write(dummy_content)
            tf_path = Path(tf.name)

        try:
            base_url = "http://192.168.1.100:8000"
            msg = make_ota_msg(tf_path, "1.0.1", base_url)

            # Đủ 4 key
            expected_keys = {"version", "url", "size", "sha256"}
            self.assertEqual(set(msg.keys()), expected_keys)

            # Giá trị đúng
            self.assertEqual(msg["version"], "1.0.1")
            self.assertEqual(msg["size"], len(dummy_content))
            self.assertEqual(msg["sha256"], sha256_file(dummy_content))
            self.assertEqual(msg["url"], f"http://192.168.1.100:8000/fw/{tf_path.name}")
        finally:
            if tf_path.exists():
                tf_path.unlink()

    def test_http_server_serving_firmware(self):
        """Kiểm tra HTTP server phát file firmware đúng chuẩn."""
        with tempfile.TemporaryDirectory() as temp_dir:
            fw_file = Path(temp_dir) / "test_fw_1.0.1.bin"
            content = b"TEST FIRMWARE DOWNLOAD DATA"
            fw_file.write_bytes(content)

            server = start_http_server(temp_dir, port=18888)
            opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
            try:
                # 1. Thử tải qua URL /fw/test_fw_1.0.1.bin
                url = f"http://127.0.0.1:18888/fw/{fw_file.name}"
                with opener.open(url, timeout=3) as resp:
                    downloaded = resp.read()
                    self.assertEqual(downloaded, content)

                # 2. Thử tải qua URL /test_fw_1.0.1.bin
                url_direct = f"http://127.0.0.1:18888/{fw_file.name}"
                with opener.open(url_direct, timeout=3) as resp:
                    downloaded_direct = resp.read()
                    self.assertEqual(downloaded_direct, content)
            finally:
                server.shutdown()
                server.server_close()

    def test_http_server_directory_traversal_prevention(self):
        """Kiểm tra HTTP server chặn triệt để path traversal (..) để tránh lộ file ngoài fw_dir."""
        with tempfile.TemporaryDirectory() as temp_dir:
            fw_dir = Path(temp_dir) / "fw"
            fw_dir.mkdir()
            fw_file = fw_dir / "node_b_1.0.1.bin"
            fw_file.write_bytes(b"FIRMWARE")

            secret_file = Path(temp_dir) / "secret.env"
            secret_file.write_bytes(b"SECRET_TOKEN=12345")

            server = start_http_server(fw_dir, port=18889)
            opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
            try:
                # 1. Truy cập file hợp lệ trong fw/ -> 200 OK
                valid_url = f"http://127.0.0.1:18889/fw/{fw_file.name}"
                with opener.open(valid_url, timeout=3) as resp:
                    self.assertEqual(resp.read(), b"FIRMWARE")

                # 2. Truy cập lùi thư mục /fw/../secret.env -> Phải ra 404
                traversal_url1 = "http://127.0.0.1:18889/fw/../secret.env"
                with self.assertRaises(urllib.error.HTTPError) as ctx1:
                    opener.open(traversal_url1, timeout=3)
                self.assertEqual(ctx1.exception.code, 404)

                # 3. Truy cập lùi thư mục /../../secret.env -> Phải ra 404
                traversal_url2 = "http://127.0.0.1:18889/../../secret.env"
                with self.assertRaises(urllib.error.HTTPError) as ctx2:
                    opener.open(traversal_url2, timeout=3)
                self.assertEqual(ctx2.exception.code, 404)
            finally:
                server.shutdown()
                server.server_close()


if __name__ == "__main__":
    unittest.main()
