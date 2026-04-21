"""Status monitoring - FPS, latency, packet loss statistics"""

import time
import threading
import logging


logger = logging.getLogger(__name__)


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
            }
