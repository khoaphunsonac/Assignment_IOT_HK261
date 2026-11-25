#!/usr/bin/env python3
"""
store_forward.py - Module lưu tạm dữ liệu khi mất internet và gửi bù (Issue #64 - Tiêu chí T15)
Khớp 100% tài liệu CONTRACT.md (mục 11.2) và TASKS_W3.md (mục 4).
Tác giả: Tuấn (TV2 - Gateway)
"""

import json
import logging
import os
import threading
from collections import deque
from pathlib import Path
from typing import Any, Dict, List, Optional, Union

logger = logging.getLogger("gateway_store_forward")

DEFAULT_BUFFER_FILE = Path(__file__).parent / "buffer.jsonl"
DEFAULT_MAX_RECORDS = 1800  # 1 giờ dữ liệu với tần suất 2 giây/lần
DEFAULT_BATCH_SIZE = 50     # Gửi mỗi lô 50 bản ghi


class StoreForwardBuffer:
    """
    Hàng đợi lưu tạm telemetry dạng FIFO có giới hạn dung lượng,
    đồng bộ liên tục với file jsonl trên đĩa để tránh mất mát khi tắt bridge.
    """

    def __init__(
        self,
        file_path: Union[str, Path] = DEFAULT_BUFFER_FILE,
        max_size: int = DEFAULT_MAX_RECORDS,
        batch_size: int = DEFAULT_BATCH_SIZE,
    ):
        self.file_path = Path(file_path).resolve()
        self.max_size = int(max_size)
        self.batch_size = int(batch_size)
        self.lock = threading.Lock()
        self.queue: deque = deque(maxlen=self.max_size)
        self.load()

    def load(self) -> int:
        """Đọc toàn bộ bản ghi còn lưu trong file jsonl vào hàng đợi."""
        with self.lock:
            self.queue.clear()
            if not self.file_path.exists():
                return 0

            loaded_count = 0
            try:
                with open(self.file_path, "r", encoding="utf-8") as f:
                    for line in f:
                        line = line.strip()
                        if not line:
                            continue
                        try:
                            record = json.loads(line)
                            self.queue.append(record)
                            loaded_count += 1
                        except json.JSONDecodeError:
                            continue
                logger.info(
                    "Đã nạp %d bản ghi telemetry chưa gửi từ đĩa: %s",
                    len(self.queue),
                    self.file_path,
                )
            except Exception as e:
                logger.error("Lỗi khi đọc file buffer %s: %s", self.file_path, e)

            return len(self.queue)

    def _sync_to_file(self):
        """Ghi toàn bộ hàng đợi hiện tại ra file đĩa (an toàn nguyên tử)."""
        temp_file = self.file_path.with_suffix(".tmp")
        try:
            self.file_path.parent.mkdir(parents=True, exist_ok=True)
            with open(temp_file, "w", encoding="utf-8") as f:
                for record in self.queue:
                    f.write(json.dumps(record, ensure_ascii=False) + "\n")
            temp_file.replace(self.file_path)
        except Exception as e:
            logger.error("Lỗi khi lưu buffer ra đĩa %s: %s", self.file_path, e)
            if temp_file.exists():
                try:
                    temp_file.unlink()
                except Exception:
                    pass

    def append(self, item: Dict[str, Any]):
        """
        Thêm 1 bản ghi telemetry vào buffer.
        Nếu đầy (vượt max_size), bản ghi cũ nhất sẽ tự động bị loại bỏ (FIFO).
        """
        with self.lock:
            self.queue.append(item)
            self._sync_to_file()

    def pop_batch(self, batch_size: Optional[int] = None) -> List[Dict[str, Any]]:
        """
        Lấy ra một lô tối đa batch_size bản ghi từ đầu hàng đợi (từ cũ nhất đến mới nhất)
        và cập nhật lại file đĩa.
        """
        size = batch_size or self.batch_size
        with self.lock:
            batch = []
            for _ in range(min(size, len(self.queue))):
                batch.append(self.queue.popleft())

            if batch:
                self._sync_to_file()
            return batch

    def size(self) -> int:
        """Số lượng bản ghi hiện đang nằm trong hàng đợi."""
        with self.lock:
            return len(self.queue)

    def is_empty(self) -> bool:
        """Kiểm tra hàng đợi có rỗng hay không."""
        with self.lock:
            return len(self.queue) == 0

    def clear(self):
        """Xóa sạch hàng đợi và file lưu trữ."""
        with self.lock:
            self.queue.clear()
            if self.file_path.exists():
                try:
                    self.file_path.unlink()
                except Exception as e:
                    logger.warning("Không thể xóa file buffer %s: %s", self.file_path, e)

    @staticmethod
    def format_batch_payload(batch: List[Dict[str, Any]]) -> Dict[str, Any]:
        """
        Gộp một mảng các bản ghi telemetry đã tách thành một bản tin v1/gateway/telemetry duy nhất.
        Đúng chuẩn ThingsBoard Gateway:
        {
          "room1-node-a": [{"ts": 1700000000000, "values": {...}}, ...],
          "room1-node-b": [{"ts": 1700000000000, "values": {...}}, ...]
        }
        """
        result: Dict[str, List[Dict[str, Any]]] = {}
        for record in batch:
            for device, entries in record.items():
                if device not in result:
                    result[device] = []
                if isinstance(entries, list):
                    result[device].extend(entries)
                elif isinstance(entries, dict):
                    result[device].append(entries)

        # Loại bỏ các thiết bị không có dữ liệu
        return {k: v for k, v in result.items() if v}
