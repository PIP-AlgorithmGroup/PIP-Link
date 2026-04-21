"""视频接收线程 - 接收分片并重组"""

import struct
import threading
import socket
import queue
import logging
import time
import numpy as np
import cv2
from collections import deque
from typing import Optional, Callable, Dict
from config import Config
from network.protocol import Protocol
from network.fec import FECDecoder, FEC_AVAILABLE
from network.h264_decoder import H264Decoder, H264_AVAILABLE


logger = logging.getLogger(__name__)


class VideoReceiver:
    """视频接收 - 分片重组 + 渲染队列"""

    def __init__(self, port: int, server_addr: tuple = None):
        self.port = port
        self.server_addr = server_addr
        self.socket: Optional[socket.socket] = None
        self.is_running = False
        self.render_queue = queue.Queue(maxsize=Config.RENDER_QUEUE_MAX_SIZE)

        # 分片重组缓冲 {frame_id: {chunk_idx: chunk_data}}
        self._frame_buffer: Dict[int, Dict[int, bytes]] = {}
        self._frame_info: Dict[int, int] = {}  # {frame_id: total_chunks}
        self._buffer_lock = threading.Lock()
        self._last_completed_frame_id = 0

        # NACK 追踪
        self._frame_first_seen: Dict[int, float] = {}
        self._nack_count: Dict[int, int] = {}  # {frame_id: nack_sent_count}
        self._nack_timeout = 0.05  # 50ms 后检测不完整帧
        self._nack_max_retries = 2

        # FEC 解码器
        self._fec_decoder = FECDecoder(Config.FEC_REDUNDANCY) if FEC_AVAILABLE else None
        self._frame_fec_info: Dict[int, tuple] = {}  # {frame_id: (orig_chunks, total_with_fec)}
        self._chunk_sizes: Dict[int, Dict[int, int]] = {}  # {frame_id: {chunk_idx: size}}

        # H.264 解码器
        self._h264_decoder = H264Decoder() if H264_AVAILABLE else None
        self._frame_codec: Dict[int, int] = {}  # {frame_id: codec_flag}

        # 回调
        self.on_frame_received: Optional[Callable] = None
        self.on_error: Optional[Callable] = None

        # 统计
        self._stats_lock = threading.Lock()
        self.frames_received = 0
        self.packets_received = 0
        self.bytes_received = 0
        self.frames_dropped = 0
        self._last_frame_time = 0.0
        self._last_decode_time_ms = 0.0
        self.decode_errors = 0
        self.crc_errors = 0

        # 滑动窗口丢包率（基于 frame_id 连续性）
        self._frame_events: deque = deque(maxlen=500)  # (time, expected, received)

    def start(self):
        if self.is_running:
            return
        self.is_running = True
        # 重置统计（重连时需要）
        with self._stats_lock:
            self.frames_received = 0
            self.packets_received = 0
            self.bytes_received = 0
            self.frames_dropped = 0
        # 清空缓冲
        with self._buffer_lock:
            self._frame_buffer.clear()
            self._frame_info.clear()
            self._last_completed_frame_id = 0
        threading.Thread(target=self._rx_thread, daemon=True).start()
        logger.info(f"VideoReceiver started (port: {self.port})")

    def stop(self):
        self.is_running = False
        if self.socket:
            try:
                self.socket.close()
            except Exception:
                pass
            self.socket = None
        # 清空渲染队列，防止断开后残留旧帧
        while not self.render_queue.empty():
            try:
                self.render_queue.get_nowait()
            except queue.Empty:
                break
        logger.info("VideoReceiver stopped")

    def _rx_thread(self):
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
            self.socket.bind(("0.0.0.0", self.port))
            self.socket.settimeout(1.0)
            logger.info(f"VideoReceiver bind: 0.0.0.0:{self.port}")

            # 注册包（首次）
            last_register_time = 0.0
            if self.server_addr:
                try:
                    self.socket.sendto(b"REGISTER", self.server_addr)
                    last_register_time = time.time()
                    logger.info(f"Sent video register to {self.server_addr}")
                except Exception as e:
                    logger.warning(f"Register failed: {e}")

            while self.is_running:
                try:
                    data, addr = self.socket.recvfrom(Config.UDP_BUFFER_SIZE)
                    self._process_packet(data)
                except socket.timeout:
                    # 没收到帧或长时间无帧，定期重发 REGISTER
                    if self.server_addr:
                        now = time.time()
                        with self._stats_lock:
                            no_frames = self.frames_received == 0
                            stale = (self._last_frame_time > 0 and
                                     now - self._last_frame_time > 3.0)
                        if (no_frames or stale) and now - last_register_time >= 2.0:
                            try:
                                self.socket.sendto(b"REGISTER", self.server_addr)
                                last_register_time = now
                                logger.debug(f"Re-sent video register to {self.server_addr}")
                            except Exception:
                                pass
                except (ConnectionResetError, OSError):
                    pass
                except Exception as e:
                    if self.is_running:
                        logger.error(f"Receive error: {e}")
                # 每次循环检查不完整帧
                self._check_incomplete_frames()
        except Exception as e:
            logger.error(f"RX thread error: {e}")

    def _process_packet(self, data: bytes):
        """处理分片包 - 支持旧格式(12B头)和新格式(15B头含FEC)"""
        with self._stats_lock:
            self.packets_received += 1
            self.bytes_received += len(data)

        if len(data) < 12:
            with self._stats_lock:
                self.crc_errors += 1
            return

        # 尝试新格式: [frame_id:4][total:2][idx:2][size:4][fec_flag:1][orig_chunks:2][codec:1] = 16 bytes
        if len(data) >= 16:
            frame_id, total_chunks, chunk_idx, chunk_size, fec_flag, orig_chunks, codec_flag = \
                struct.unpack("=IHHIBHB", data[:16])
            if fec_flag <= 1 and orig_chunks <= total_chunks and chunk_size <= len(data) - 16:
                payload = data[16:16 + chunk_size]
                has_fec = True
            else:
                # 回退旧格式
                frame_id, total_chunks, chunk_idx, chunk_size = struct.unpack("=IHHI", data[:12])
                payload = data[12:12 + chunk_size]
                has_fec = False
                orig_chunks = total_chunks
                fec_flag = 0
                codec_flag = 0
        else:
            frame_id, total_chunks, chunk_idx, chunk_size = struct.unpack("=IHHI", data[:12])
            payload = data[12:12 + chunk_size]
            has_fec = False
            orig_chunks = total_chunks
            fec_flag = 0
            codec_flag = 0

        with self._buffer_lock:
            if frame_id <= self._last_completed_frame_id:
                return

            if frame_id not in self._frame_buffer:
                self._frame_buffer[frame_id] = {}
                self._frame_info[frame_id] = total_chunks
                self._frame_first_seen[frame_id] = time.time()
                self._frame_codec[frame_id] = codec_flag
                if has_fec:
                    self._frame_fec_info[frame_id] = (orig_chunks, total_chunks)
                    self._chunk_sizes[frame_id] = {}

            self._frame_buffer[frame_id][chunk_idx] = payload
            if has_fec and frame_id in self._chunk_sizes and fec_flag == 0:
                self._chunk_sizes[frame_id][chunk_idx] = len(payload)

            # 检查是否可以重组（收到 >= orig_chunks 个 chunks）
            n_received = len(self._frame_buffer[frame_id])
            can_complete = n_received >= orig_chunks

            if can_complete:
                frame_data = self._try_reassemble(frame_id, orig_chunks, total_chunks, has_fec)
                if frame_data is not None:
                    frame_codec = self._frame_codec.get(frame_id, 0)
                    prev_id = self._last_completed_frame_id
                    self._last_completed_frame_id = frame_id

                    # 记录丢包事件：prev_id+1 到 frame_id 之间跳过的帧算丢失
                    now = time.time()
                    skipped = max(0, frame_id - prev_id - 1) if prev_id > 0 else 0
                    with self._stats_lock:
                        self._frame_events.append((now, 1 + skipped, 1))

                    stale = [fid for fid in self._frame_buffer if fid <= frame_id]
                    for fid in stale:
                        del self._frame_buffer[fid]
                        self._frame_info.pop(fid, None)
                        self._frame_first_seen.pop(fid, None)
                        self._nack_count.pop(fid, None)
                        self._frame_fec_info.pop(fid, None)
                        self._chunk_sizes.pop(fid, None)
                        self._frame_codec.pop(fid, None)

                    self._send_video_ack(frame_id)
                    self._decode_and_enqueue(frame_data, frame_codec)

    def _enqueue_frame(self, frame):
        """放入渲染队列"""
        try:
            self.render_queue.put_nowait(frame)
        except queue.Full:
            try:
                self.render_queue.get_nowait()
            except queue.Empty:
                pass
            self.render_queue.put_nowait(frame)
            with self._stats_lock:
                self.frames_dropped += 1

        with self._stats_lock:
            self.frames_received += 1
            self._last_frame_time = time.time()

    def _try_reassemble(self, frame_id: int, orig_chunks: int, total_chunks: int, has_fec: bool) -> Optional[bytes]:
        """尝试重组帧数据，必要时使用 FEC 恢复"""
        received = self._frame_buffer[frame_id]

        # 检查是否有所有原始 data chunks
        all_data_present = all(i in received for i in range(orig_chunks))
        if all_data_present:
            return b"".join(received[i] for i in range(orig_chunks))

        # 需要 FEC 恢复
        if has_fec and self._fec_decoder and len(received) >= orig_chunks:
            chunk_sizes = self._chunk_sizes.get(frame_id, {})
            result = self._fec_decoder.decode(received, orig_chunks, total_chunks, chunk_sizes)
            if result is not None:
                return b"".join(result)

        return None

    def _decode_and_enqueue(self, frame_data: bytes, codec: int = 0):
        """解码帧数据并放入渲染队列"""
        decode_start = time.perf_counter()

        if codec == 1 and self._h264_decoder:
            frames = self._h264_decoder.decode(frame_data)
            if not frames:
                with self._stats_lock:
                    self.decode_errors += 1
            for frame in frames:
                if frame.shape[1] != Config.RENDER_WIDTH or frame.shape[0] != Config.RENDER_HEIGHT:
                    frame = cv2.resize(frame, (Config.RENDER_WIDTH, Config.RENDER_HEIGHT))
                self._enqueue_frame(frame)
            self._last_decode_time_ms = (time.perf_counter() - decode_start) * 1000
            return

        # JPEG 或 raw BGR
        expected_raw = Config.RENDER_WIDTH * Config.RENDER_HEIGHT * 3
        if len(frame_data) == expected_raw:
            frame = np.frombuffer(frame_data, dtype=np.uint8).reshape(
                (Config.RENDER_HEIGHT, Config.RENDER_WIDTH, 3))
            self._enqueue_frame(frame)
        else:
            jpg_arr = np.frombuffer(frame_data, dtype=np.uint8)
            frame = cv2.imdecode(jpg_arr, cv2.IMREAD_COLOR)
            if frame is not None:
                if frame.shape[1] != Config.RENDER_WIDTH or frame.shape[0] != Config.RENDER_HEIGHT:
                    frame = cv2.resize(frame, (Config.RENDER_WIDTH, Config.RENDER_HEIGHT))
                self._enqueue_frame(frame)
            else:
                with self._stats_lock:
                    self.decode_errors += 1

        self._last_decode_time_ms = (time.perf_counter() - decode_start) * 1000

    def get_latest_frame(self):
        """获取最新帧（numpy array 或 None）"""
        try:
            return self.render_queue.get_nowait()
        except queue.Empty:
            return None

    def get_statistics(self) -> dict:
        with self._stats_lock:
            return {
                "frames_received": self.frames_received,
                "packets_received": self.packets_received,
                "bytes_received": self.bytes_received,
                "frames_dropped": self.frames_dropped,
                "video_loss_rate": self._calc_recent_loss(1.0),
                "decode_time_ms": self._last_decode_time_ms,
                "buffer_frames": self.render_queue.qsize(),
                "decode_errors": self.decode_errors,
                "crc_errors": self.crc_errors,
                "keyframe_interval": 30,
            }

    def _calc_recent_loss(self, window: float = 1.0) -> float:
        """计算最近 window 秒的视频帧丢包率"""
        now = time.time()
        cutoff = now - window
        total_expected = 0
        total_received = 0
        for t, expected, received in self._frame_events:
            if t >= cutoff:
                total_expected += expected
                total_received += received
        if total_expected == 0:
            return 0.0
        return max(0.0, (total_expected - total_received) / total_expected)

    def _send_video_ack(self, frame_id: int):
        """发送视频帧 ACK 到服务端"""
        if not self.server_addr or not self.socket:
            return
        try:
            ack_data = Protocol.build_video_ack(frame_id)
            self.socket.sendto(ack_data, self.server_addr)
        except Exception as e:
            logger.debug(f"Video ACK send failed: {e}")

    def _check_incomplete_frames(self):
        """检查超时的不完整帧，发送 NACK"""
        if not self.server_addr or not self.socket:
            return
        now = time.time()
        nacks_to_send = []
        with self._buffer_lock:
            for frame_id in list(self._frame_buffer.keys()):
                if frame_id <= self._last_completed_frame_id:
                    continue
                first_seen = self._frame_first_seen.get(frame_id, now)
                elapsed = now - first_seen
                if elapsed < self._nack_timeout:
                    continue
                nack_count = self._nack_count.get(frame_id, 0)
                if nack_count >= self._nack_max_retries:
                    continue
                total = self._frame_info.get(frame_id, 0)
                if total == 0:
                    continue
                received = set(self._frame_buffer[frame_id].keys())
                missing = [i for i in range(total) if i not in received]
                if not missing:
                    continue
                self._nack_count[frame_id] = nack_count + 1
                nacks_to_send.append((frame_id, missing))
        for frame_id, missing in nacks_to_send:
            try:
                nack_data = Protocol.build_video_nack(frame_id, missing)
                self.socket.sendto(nack_data, self.server_addr)
            except Exception:
                pass
