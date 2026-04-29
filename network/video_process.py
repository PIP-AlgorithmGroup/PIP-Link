"""VideoReceiver 多进程封装 — 独立进程接收+解码，shared memory 传帧"""

import multiprocessing
from multiprocessing import shared_memory
import struct
import time
import logging
import numpy as np
from typing import Optional
from config import Config

logger = logging.getLogger(__name__)

_HEADER = 8  # [frame_counter:4][padding:4]
_FRAME_SIZE = Config.RENDER_WIDTH * Config.RENDER_HEIGHT * 3


def _receiver_main(port, server_addr, shm_name, stats_q, stop_evt):
    """子进程入口 — 运行 VideoReceiver 并将解码帧写入 shared memory"""
    import os, sys
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from network.video_receiver import VideoReceiver

    shm = shared_memory.SharedMemory(name=shm_name)
    w, h = Config.RENDER_WIDTH, Config.RENDER_HEIGHT

    receiver = VideoReceiver(port, server_addr=tuple(server_addr) if server_addr else None)
    receiver.start()

    frame_counter = 0
    last_stats_t = 0.0

    try:
        while not stop_evt.is_set():
            frame = receiver.get_latest_frame()
            if frame is not None and isinstance(frame, np.ndarray):
                if frame.shape == (h, w, 3):
                    buf_idx = frame_counter % 2
                    offset = _HEADER + buf_idx * _FRAME_SIZE
                    shm.buf[offset:offset + _FRAME_SIZE] = frame.tobytes()
                    frame_counter += 1
                    struct.pack_into("=I", shm.buf, 0, frame_counter)

            now = time.time()
            if now - last_stats_t > 0.2:
                try:
                    stats_q.put_nowait(receiver.get_statistics())
                except Exception:
                    pass
                last_stats_t = now

            if frame is None:
                time.sleep(0.001)
    except Exception as e:
        logger.error(f"VideoReceiverProcess worker error: {e}")
    finally:
        receiver.stop()
        shm.close()


class VideoReceiverProcess:
    """VideoReceiver drop-in replacement — 独立进程，GIL 隔离"""

    def __init__(self, port: int, server_addr: Optional[tuple] = None):
        self.port = port
        self.server_addr = server_addr
        self.is_running = False
        self._process: Optional[multiprocessing.Process] = None
        self._shm: Optional[shared_memory.SharedMemory] = None
        self._stats_q: Optional[multiprocessing.Queue] = None
        self._stop_evt: Optional[multiprocessing.Event] = None
        self._last_counter = 0
        self._last_stats: dict = {}

    def start(self):
        if self.is_running:
            return
        shm_size = _HEADER + _FRAME_SIZE * 2
        self._shm = shared_memory.SharedMemory(create=True, size=shm_size)
        struct.pack_into("=I", self._shm.buf, 0, 0)

        self._stats_q = multiprocessing.Queue(maxsize=8)
        self._stop_evt = multiprocessing.Event()

        addr = list(self.server_addr) if self.server_addr else None
        self._process = multiprocessing.Process(
            target=_receiver_main,
            args=(self.port, addr, self._shm.name,
                  self._stats_q, self._stop_evt),
            daemon=True,
        )
        self._process.start()
        self.is_running = True
        logger.info(f"VideoReceiverProcess started (pid={self._process.pid})")

    def stop(self):
        if not self.is_running:
            return
        self.is_running = False
        if self._stop_evt:
            self._stop_evt.set()
        if self._process:
            self._process.join(timeout=3.0)
            if self._process.is_alive():
                self._process.terminate()
            self._process = None
        if self._shm:
            try:
                self._shm.close()
                self._shm.unlink()
            except Exception:
                pass
            self._shm = None
        logger.info("VideoReceiverProcess stopped")

    def get_latest_frame(self) -> Optional[np.ndarray]:
        if not self._shm:
            return None
        try:
            counter = struct.unpack_from("=I", self._shm.buf, 0)[0]
            if counter == self._last_counter:
                return None
            self._last_counter = counter
            read_idx = (counter - 1) % 2
            offset = _HEADER + read_idx * _FRAME_SIZE
            frame = np.frombuffer(
                bytes(self._shm.buf[offset:offset + _FRAME_SIZE]),
                dtype=np.uint8,
            ).reshape((Config.RENDER_HEIGHT, Config.RENDER_WIDTH, 3))
            return frame
        except (ValueError, TypeError, AttributeError):
            return None

    def get_statistics(self) -> dict:
        while self._stats_q:
            try:
                self._last_stats = self._stats_q.get_nowait()
            except Exception:
                break
        return self._last_stats
