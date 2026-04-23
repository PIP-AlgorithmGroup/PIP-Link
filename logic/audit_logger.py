"""操作日志审计 - 线程安全，异步写文件"""

import time
import json
import csv
import os
import threading
import logging
from collections import deque
from typing import Optional, List, Dict, Any


logger = logging.getLogger(__name__)


class AuditLogger:
    """操作日志审计 - 记录连接/控制/参数/异常事件"""

    EVENT_TYPES = ["connect", "disconnect", "param_change",
                   "recording", "screenshot", "error", "warning", "info"]

    def __init__(self, log_dir: str = "logs", max_memory: int = 10000):
        self._log_dir = log_dir
        self._records: deque = deque(maxlen=max_memory)
        self._lock = threading.Lock()
        self._write_queue: deque = deque()
        self._write_lock = threading.Lock()
        self._writer_thread: Optional[threading.Thread] = None
        self._stop_event = threading.Event()
        self._current_log_file: Optional[str] = None
        self._start_writer()

    def _start_writer(self):
        self._writer_thread = threading.Thread(
            target=self._writer_loop, daemon=True, name="AuditWriter"
        )
        self._writer_thread.start()

    def _writer_loop(self):
        while not self._stop_event.is_set():
            self._stop_event.wait(timeout=1.0)
            self._flush_queue()

    def _flush_queue(self):
        with self._write_lock:
            if not self._write_queue:
                return
            items = list(self._write_queue)
            self._write_queue.clear()

        try:
            os.makedirs(self._log_dir, exist_ok=True)
            date_str = time.strftime("%Y%m%d")
            log_path = os.path.join(self._log_dir, f"audit_{date_str}.log")
            self._current_log_file = log_path
            with open(log_path, "a", encoding="utf-8") as f:
                for item in items:
                    f.write(json.dumps(item, ensure_ascii=False) + "\n")
        except Exception as e:
            logger.error(f"AuditLogger write error: {e}")

    def log(self, event_type: str, detail: str, extra: Optional[Dict[str, Any]] = None):
        """记录一条审计事件"""
        record = {
            "ts": time.time(),
            "time": time.strftime("%Y-%m-%d %H:%M:%S"),
            "type": event_type,
            "detail": detail,
        }
        if extra:
            record["extra"] = extra

        with self._lock:
            self._records.append(record)

        with self._write_lock:
            self._write_queue.append(record)

    def get_recent(self, n: int = 100) -> List[dict]:
        """获取最近 n 条记录（最新在前）"""
        with self._lock:
            records = list(self._records)
        return list(reversed(records[-n:]))

    def get_all(self) -> List[dict]:
        with self._lock:
            return list(self._records)

    def clear(self):
        with self._lock:
            self._records.clear()

    def export_csv(self, path: str) -> str:
        """导出 CSV，返回实际写入路径"""
        if not path.endswith(".csv"):
            path += ".csv"
        os.makedirs(os.path.dirname(path) if os.path.dirname(path) else ".", exist_ok=True)
        records = self.get_all()
        try:
            with open(path, "w", newline="", encoding="utf-8-sig") as f:
                writer = csv.DictWriter(f, fieldnames=["time", "type", "detail", "ts"])
                writer.writeheader()
                for r in records:
                    writer.writerow({
                        "time": r.get("time", ""),
                        "type": r.get("type", ""),
                        "detail": r.get("detail", ""),
                        "ts": r.get("ts", 0),
                    })
            logger.info(f"Audit log exported to {path}")
            return path
        except Exception as e:
            logger.error(f"Export CSV error: {e}")
            return ""

    def export_json(self, path: str) -> str:
        """导出 JSON，返回实际写入路径"""
        if not path.endswith(".json"):
            path += ".json"
        os.makedirs(os.path.dirname(path) if os.path.dirname(path) else ".", exist_ok=True)
        records = self.get_all()
        try:
            with open(path, "w", encoding="utf-8") as f:
                json.dump(records, f, ensure_ascii=False, indent=2)
            logger.info(f"Audit log exported to {path}")
            return path
        except Exception as e:
            logger.error(f"Export JSON error: {e}")
            return ""

    def stop(self):
        self._stop_event.set()
        self._flush_queue()
