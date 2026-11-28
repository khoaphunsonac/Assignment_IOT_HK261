"""
test_measure.py - Kiểm thử tự động cho công cụ đo đạc hiệu năng measure.py (Issue #67)
Khớp 100% tài liệu TASKS_W3.md mục 4.
Tác giả: Tuấn (TV2 - Gateway)
"""

import csv
import tempfile
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

from measure import print_stats, save_csv, load_env


class TestMeasureTool(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_save_csv(self):
        """Kiểm tra lưu dữ liệu đo ra file CSV đúng chuẩn."""
        csv_file = Path(self.temp_dir.name) / "test_out.csv"
        header = ["col1", "col2", "col3"]
        rows = [
            [1, 100, 20.5],
            [2, 200, 22.1],
        ]

        save_csv(csv_file, header, rows)
        self.assertTrue(csv_file.exists())

        with open(csv_file, "r", encoding="utf-8") as f:
            reader = list(csv.reader(f))
            self.assertEqual(reader[0], header)
            self.assertEqual(reader[1], ["1", "100", "20.5"])
            self.assertEqual(reader[2], ["2", "200", "22.1"])

    def test_print_stats(self):
        """Kiểm tra tính toán thống kê (không văng lỗi khi danh sách rỗng hoặc có số liệu)."""
        # Không có phần tử
        print_stats("test_empty", [])
        # Có phần tử
        print_stats("test_valid", [10.0, 20.0, 30.0], "ms")

    def test_load_env(self):
        """Kiểm tra đọc file .env."""
        env_file = Path(self.temp_dir.name) / ".env"
        env_file.write_text("TEST_KEY=12345\n# Comment\nANOTHER=hello\n", encoding="utf-8")

        import os
        load_env(env_file)
        self.assertEqual(os.environ.get("TEST_KEY"), "12345")
        self.assertEqual(os.environ.get("ANOTHER"), "hello")


if __name__ == "__main__":
    unittest.main()
