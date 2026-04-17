"""Status monitoring"""

import time


class StatusMonitor:
    """Status monitor"""

    def __init__(self):
        self.fps = 0.0
        self.latency_ms = 0.0
        self.packet_loss_rate = 0.0
        self.frames_received = 0

        self.frame_count = 0
        self.last_time = time.time()

    def tick_frame(self) -> None:
        """Call once per rendered frame for FPS calculation"""
        self.frame_count += 1

    def update(self, stats: dict):
        """Update statistics"""
        # Calculate FPS
        current_time = time.time()
        elapsed = current_time - self.last_time
        if elapsed >= 1.0:
            self.fps = self.frame_count / elapsed
            self.frame_count = 0
            self.last_time = current_time

        # Update latency and packet loss
        self.latency_ms = stats.get("latency_ms", 0.0)
        self.packet_loss_rate = stats.get("packet_loss_rate", 0.0)
        self.frames_received = stats.get("frames_received", 0)

    def get_status(self) -> dict:
        """Get status"""
        return {
            "fps": self.fps,
            "latency_ms": self.latency_ms,
            "packet_loss_rate": self.packet_loss_rate,
            "frames_received": self.frames_received,
        }
