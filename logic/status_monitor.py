"""Status monitoring - FPS, latency, packet loss statistics"""

import time
import threading
import logging
try:
    import psutil
    _PSUTIL_AVAILABLE = True
except ImportError:
    _PSUTIL_AVAILABLE = False

logger = logging.getLogger(__name__)

# 历史缓冲大小（600 @ 1Hz ≈ 10分钟）
HISTORY_SIZE = 600


class StatusMonitor:
    """Status monitor - real-time statistics collection"""

    def __init__(self):
        self.fps = 0.0
        self.rtt_ms = 0.0
        self.packet_loss_rate = 0.0
        self.frames_received = 0
        self.packets_received = 0
        self.bytes_received = 0

        # FPS tracking
        self.frame_count = 0
        self.last_time = time.time()

        # Lock for thread safety
        self._lock = threading.Lock()

        # 历史环形缓冲（1Hz 采样）
        self._rtt_history: list = [0.0] * HISTORY_SIZE
        self._loss_history: list = [0.0] * HISTORY_SIZE
        self._bw_history: list = [0.0] * HISTORY_SIZE   # kbps
        self._fps_history: list = [0.0] * HISTORY_SIZE
        self._cpu_history: list = [0.0] * HISTORY_SIZE
        self._mem_history: list = [0.0] * HISTORY_SIZE   # percent
        self._hist_idx: int = 0
        self._hist_last_update: float = 0.0

        # 当前带宽估算（由外部每帧更新）
        self.bandwidth_kbps: float = 0.0

        # 系统资源缓存
        self._cpu_percent: float = 0.0
        self._mem_percent: float = 0.0
        self._mem_used: int = 0
        self._mem_total: int = 0
        self._proc_mem: int = 0
        self._proc = psutil.Process() if _PSUTIL_AVAILABLE else None

    def tick_frame(self) -> None:
        """Call once per rendered frame for FPS calculation"""
        with self._lock:
            self.frame_count += 1

    def update(self, session_stats: dict):
        """Update statistics from session manager"""
        with self._lock:
            # Calculate FPS
            current_time = time.time()
            elapsed = current_time - self.last_time
            if elapsed >= 0.5:
                self.fps = self.frame_count / elapsed
                self.frame_count = 0
                self.last_time = current_time

            # Update from session stats (rtt_avg 已经是毫秒)
            self.rtt_ms = session_stats.get("rtt_avg", 0.0)
            self.frames_received = session_stats.get("frames_received", 0)
            self.packets_received = session_stats.get("packets_received", 0)
            self.bytes_received = session_stats.get("bytes_received", 0)

            # 丢包率：优先使用视频帧丢包率，回退到控制指令丢包率
            self.packet_loss_rate = session_stats.get("video_loss_rate",
                                    session_stats.get("packet_loss_rate", 0.0))

            # 1Hz 采样历史
            if current_time - self._hist_last_update >= 1.0:
                self._hist_last_update = current_time
                self._sample_history()

    def _sample_history(self):
        """采样一帧历史数据（在 _lock 持有时调用）"""
        idx = self._hist_idx % HISTORY_SIZE

        self._rtt_history[idx] = self.rtt_ms
        self._loss_history[idx] = self.packet_loss_rate * 100.0  # → 百分比
        self._bw_history[idx] = self.bandwidth_kbps
        self._fps_history[idx] = self.fps

        # 系统资源（非锁内避免阻塞，但已知 psutil 很快）
        if _PSUTIL_AVAILABLE:
            try:
                self._cpu_percent = psutil.cpu_percent(interval=0)
                mem = psutil.virtual_memory()
                self._mem_percent = mem.percent
                self._mem_used = mem.used
                self._mem_total = mem.total
                if self._proc:
                    self._proc_mem = self._proc.memory_info().rss
            except Exception:
                pass

        self._cpu_history[idx] = self._cpu_percent
        self._mem_history[idx] = self._mem_percent

        self._hist_idx += 1

    def get_status(self) -> dict:
        """Get current status"""
        with self._lock:
            return {
                "fps": self.fps,
                "latency_ms": self.rtt_ms,
                "packet_loss_rate": self.packet_loss_rate,
                "frames_received": self.frames_received,
                "packets_received": self.packets_received,
                "bytes_received": self.bytes_received,
                "cpu_percent": self._cpu_percent,
                "mem_percent": self._mem_percent,
                "mem_used": self._mem_used,
                "mem_total": self._mem_total,
                "proc_mem": self._proc_mem,
            }

    def get_history(self) -> dict:
        """返回有序历史数组（oldest→newest），供图表使用"""
        with self._lock:
            n = HISTORY_SIZE
            wi = self._hist_idx % n
            # 将环形缓冲重排为时序顺序
            def ordered(buf):
                return buf[wi:] + buf[:wi]
            return {
                "rtt": ordered(self._rtt_history),
                "loss": ordered(self._loss_history),
                "bandwidth": ordered(self._bw_history),
                "fps": ordered(self._fps_history),
                "cpu": ordered(self._cpu_history),
                "mem": ordered(self._mem_history),
                "size": n,
            }
