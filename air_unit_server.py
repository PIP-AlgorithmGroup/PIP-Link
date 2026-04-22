#!/usr/bin/env python3
"""机载端测试脚本 - 在 Ubuntu 上运行，测试与客户端的远程连接"""

import socket
import struct
import time
import threading
import logging
import argparse
import numpy as np
from typing import Dict
from zeroconf import ServiceInfo, Zeroconf
import zlib
import cv2
from network.fec import FECEncoder, FEC_AVAILABLE
from network.h264_encoder import H264Encoder, H264_AVAILABLE


logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

# 视频分辨率（与客户端 Config 一致）
VIDEO_WIDTH = 1280
VIDEO_HEIGHT = 720

# 默认串流参数
DEFAULT_TARGET_BITRATE_KBPS = 2000
DEFAULT_FPS = 30
DEFAULT_JPEG_QUALITY = 80


class AdaptiveEncoder:
    """自适应 JPEG 编码器 — 根据目标码率动态调整质量"""

    def __init__(self, target_bitrate_kbps: int = DEFAULT_TARGET_BITRATE_KBPS,
                 fps: int = DEFAULT_FPS, initial_quality: int = DEFAULT_JPEG_QUALITY):
        self.target_bitrate_kbps = target_bitrate_kbps
        self.fps = fps
        self.quality = initial_quality
        self.quality_min = 15
        self.quality_max = 85

        # 目标每帧字节数
        self.target_frame_bytes = (target_bitrate_kbps * 1000 // 8) // fps

        # 指数移动平均跟踪实际帧大小
        self._ema_size = float(self.target_frame_bytes)
        self._ema_alpha = 0.3  # 响应速度

        # 缓存基础彩条帧（静态部分不重复生成）
        self._base_frame = self._generate_base_frame()

    @staticmethod
    def _generate_base_frame() -> np.ndarray:
        """生成测试卡基础帧 — 彩条 + 分辨率网格 + 中心圆 + 灰阶"""
        frame = np.zeros((VIDEO_HEIGHT, VIDEO_WIDTH, 3), dtype=np.uint8)

        # 上部 75%: 彩条
        bar_h = int(VIDEO_HEIGHT * 0.70)
        colors_bgr = [
            (255, 255, 255), (0, 255, 255), (255, 255, 0), (0, 255, 0),
            (255, 0, 255), (0, 0, 255), (255, 0, 0),
        ]
        bar_w = VIDEO_WIDTH // len(colors_bgr)
        for i, color in enumerate(colors_bgr):
            x0 = i * bar_w
            x1 = (i + 1) * bar_w if i < len(colors_bgr) - 1 else VIDEO_WIDTH
            frame[:bar_h, x0:x1] = color

        # 下部: 灰阶渐变条
        gray_y0 = bar_h
        gray_h = int(VIDEO_HEIGHT * 0.08)
        for x in range(VIDEO_WIDTH):
            gray_val = int(255 * x / VIDEO_WIDTH)
            frame[gray_y0:gray_y0 + gray_h, x] = (gray_val, gray_val, gray_val)

        # 底部: 黑色区域（留给动态内容）
        # frame[gray_y0 + gray_h:, :] 已经是黑色

        # 分辨率网格（细白线）
        grid_spacing = 64
        for x in range(0, VIDEO_WIDTH, grid_spacing):
            frame[:bar_h, x:x+1] = (80, 80, 80)
        for y in range(0, bar_h, grid_spacing):
            frame[y:y+1, :] = (80, 80, 80)

        # 中心十字 + 圆
        cx, cy = VIDEO_WIDTH // 2, bar_h // 2
        cv2.circle(frame, (cx, cy), 80, (200, 200, 200), 2)
        cv2.circle(frame, (cx, cy), 40, (200, 200, 200), 1)
        cv2.line(frame, (cx - 100, cy), (cx + 100, cy), (200, 200, 200), 1)
        cv2.line(frame, (cx, cy - 100), (cx, cy + 100), (200, 200, 200), 1)

        # 四角标记（用于检测画面裁切）
        marker_size = 30
        for (mx, my) in [(0, 0), (VIDEO_WIDTH - marker_size, 0),
                         (0, bar_h - marker_size), (VIDEO_WIDTH - marker_size, bar_h - marker_size)]:
            frame[my:my + marker_size, mx:mx + marker_size] = (255, 255, 255)

        # 分辨率文字
        text = f"{VIDEO_WIDTH}x{VIDEO_HEIGHT}"
        cv2.putText(frame, text, (cx - 80, cy + 130),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, (200, 200, 200), 1, cv2.LINE_AA)

        return frame

    def generate_dynamic_frame(self, frame_id: int) -> np.ndarray:
        """生成带动态元素的测试帧（raw BGR）"""
        frame = self._base_frame.copy()
        dyn_y = int(VIDEO_HEIGHT * 0.78)

        timestamp = time.strftime("%H:%M:%S")
        cv2.putText(frame, f"#{frame_id:06d}  {timestamp}", (20, dyn_y + 30),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 200, 255), 1, cv2.LINE_AA)

        pointer_cx = VIDEO_WIDTH - 80
        pointer_cy = dyn_y + 50
        angle = (frame_id * 12) % 360
        rad = np.radians(angle)
        px = int(pointer_cx + 35 * np.cos(rad))
        py = int(pointer_cy + 35 * np.sin(rad))
        cv2.circle(frame, (pointer_cx, pointer_cy), 38, (100, 100, 100), 1)
        cv2.line(frame, (pointer_cx, pointer_cy), (px, py), (0, 255, 0), 2)

        checker_size = 8
        block_w, block_h = 120, 80
        block_x = int((VIDEO_WIDTH / 2 - block_w / 2) + 60 * np.sin(frame_id * 0.05))
        block_y = dyn_y + 10
        # 生成棋盘格 pattern（numpy 向量化）
        bx_arr = np.arange(block_w)
        by_arr = np.arange(block_h)
        checker = ((bx_arr[None, :] // checker_size) + (by_arr[:, None] // checker_size)) % 2 == 0
        # 裁剪到画面范围
        x0 = max(0, block_x)
        y0 = max(0, block_y)
        x1 = min(VIDEO_WIDTH, block_x + block_w)
        y1 = min(VIDEO_HEIGHT, block_y + block_h)
        if x1 > x0 and y1 > y0:
            roi = checker[y0 - block_y:y1 - block_y, x0 - block_x:x1 - block_x]
            frame[y0:y1, x0:x1][roi] = (255, 255, 255)

        scroll_y = VIDEO_HEIGHT - 6
        bar_x = (frame_id * 6) % VIDEO_WIDTH
        bar_end = min(bar_x + 160, VIDEO_WIDTH)
        frame[scroll_y:VIDEO_HEIGHT, bar_x:bar_end] = (0, 200, 255)

        return frame

    def encode(self, frame_id: int) -> bytes:
        """编码一帧 — 生成动态帧 + JPEG 自适应编码"""
        frame = self.generate_dynamic_frame(frame_id)

        _, jpeg_data = cv2.imencode('.jpg', frame,
                                     [cv2.IMWRITE_JPEG_QUALITY, self.quality])
        encoded = jpeg_data.tobytes()

        self._ema_size = self._ema_alpha * len(encoded) + (1 - self._ema_alpha) * self._ema_size
        self._adjust_quality()

        return encoded

    def _adjust_quality(self):
        """根据实际帧大小 vs 目标帧大小调整 JPEG 质量"""
        ratio = self._ema_size / self.target_frame_bytes
        if ratio > 1.1:
            # 超出目标 10%，降质量
            self.quality = max(self.quality_min, self.quality - 2)
        elif ratio < 0.8:
            # 低于目标 20%，提质量
            self.quality = min(self.quality_max, self.quality + 1)


BIT_TO_KEY = {
    0: "ESC", 1: "F1", 2: "F2", 3: "F3", 4: "F4", 5: "F5", 6: "F6", 7: "F7",
    8: "F8", 9: "F9", 10: "F10", 11: "F11", 12: "F12", 13: "`", 14: "1", 15: "2",
    16: "3", 17: "4", 18: "5", 19: "6", 20: "7", 21: "8", 22: "9", 23: "0",
    24: "-", 25: "=", 26: "BS", 27: "TAB", 28: "Q", 29: "W", 30: "E", 31: "R",
    32: "T", 33: "Y", 34: "U", 35: "I", 36: "O", 37: "P", 38: "[", 39: "]",
    40: "\\", 41: "CAPS", 42: "A", 43: "S", 44: "D", 45: "F", 46: "G", 47: "H",
    48: "J", 49: "K", 50: "L", 51: ";", 52: "'", 53: "ENTER", 54: "LSHIFT", 55: "Z",
    56: "X", 57: "C", 58: "V", 59: "B", 60: "N", 61: "M", 62: ",", 63: ".",
    64: "/", 65: "RSHIFT", 66: "LCTRL", 67: "LALT", 68: "SPACE", 69: "RALT", 70: "RCTRL",
}


def decode_keyboard_bitmap(kb_state: bytes) -> list:
    """将 10 字节键盘位图解码为按下的键名列表"""
    keys = []
    for byte_idx, byte_val in enumerate(kb_state):
        if byte_val == 0:
            continue
        for bit in range(8):
            if byte_val & (1 << bit):
                bit_index = byte_idx * 8 + bit
                keys.append(BIT_TO_KEY.get(bit_index, f"?{bit_index}"))
    return keys


class AirUnitServer:
    """机载端服务器 - 接收客户端连接并响应"""

    def __init__(self, air_unit_name="air_unit_01", control_port=6000, video_port=5000,
                 target_bitrate_kbps=DEFAULT_TARGET_BITRATE_KBPS, fps=DEFAULT_FPS,
                 jpeg_quality=DEFAULT_JPEG_QUALITY):
        self.air_unit_name = air_unit_name
        self.control_port = control_port
        self.video_port = video_port
        self.fps = fps

        # 自适应编码器
        self.encoder = AdaptiveEncoder(target_bitrate_kbps, fps, jpeg_quality)

        self.zeroconf = None
        self.service_info = None
        self.control_socket = None
        self.video_socket = None

        # 客户端信息
        self.client_ip = None
        self.client_video_addr = None
        self.last_client_time = 0  # 最后收到客户端消息的时间

        # 参数存储
        self._params = {
            'resolution': '1920x1080',
            'bitrate': target_bitrate_kbps,
            'target_fps': fps,
            'encoder': 'h264',
            'fec_enabled': False,
            'fec_redundancy': 0.2,
        }

        # 帧缓存（用于 NACK 重传）
        self._frame_cache: Dict[int, Dict[int, bytes]] = {}  # {frame_id: {chunk_idx: packet}}
        self._frame_cache_max = 10

        # FEC 编码器
        self._fec_encoder = FECEncoder(self._params['fec_redundancy']) if FEC_AVAILABLE else None

        # H.264 编码器（按需初始化）
        self._h264_encoder = None
        if self._params.get('encoder') == 'h264' and H264_AVAILABLE:
            self._h264_encoder = H264Encoder(
                VIDEO_WIDTH, VIDEO_HEIGHT, fps,
                bitrate=target_bitrate_kbps * 1000
            )

        # 实时输入显示
        self.show_input = False

        # 统计
        self.control_commands_received = 0
        self.acks_sent = 0
        self.video_frames_sent = 0
        self.video_frames_acked = 0
        self.heartbeats_received = 0
        self.param_updates_received = 0

        self.is_running = False

    def start(self):
        """启动机载端服务器"""
        logger.info(f"Starting Air Unit Server: {self.air_unit_name}")
        self._start_mdns()
        self._start_udp_servers()

        self.is_running = True
        threading.Thread(target=self._control_receiver_thread, daemon=True).start()
        threading.Thread(target=self._video_sender_thread, daemon=True).start()
        threading.Thread(target=self._watchdog_thread, daemon=True).start()

        logger.info("Air Unit Server started successfully")
        logger.info(f"Waiting for client on port {self.control_port}...")

    def stop(self):
        """停止"""
        self.is_running = False
        if self.zeroconf:
            self.zeroconf.close()
        if self.control_socket:
            self.control_socket.close()
        if self.video_socket:
            self.video_socket.close()
        logger.info("Air Unit Server stopped")

    def _start_mdns(self):
        """启动 mDNS 服务"""
        hostname = socket.gethostname()
        local_ip = socket.gethostbyname(hostname)
        logger.info(f"Local IP: {local_ip}")

        self.service_info = ServiceInfo(
            "_pip-link._udp.local.",
            f"{self.air_unit_name}._pip-link._udp.local.",
            addresses=[socket.inet_aton(local_ip)],
            port=self.control_port,
            properties={
                "video_port": str(self.video_port),
                "control_port": str(self.control_port),
                "version": "1.0",
                "device_type": "air_unit",
            },
            server=f"{self.air_unit_name}.local.",
        )
        self.zeroconf = Zeroconf()
        self.zeroconf.register_service(self.service_info)
        logger.info(f"mDNS registered: {self.air_unit_name}._pip-link._udp.local.")
        logger.info(f"  Control: {self.control_port}, Video: {self.video_port}")

    def _start_udp_servers(self):
        """启动 UDP 服务器"""
        self.control_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.control_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.control_socket.bind(("0.0.0.0", self.control_port))
        self.control_socket.settimeout(1.0)

        self.video_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.video_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.video_socket.bind(("0.0.0.0", self.video_port))
        self.video_socket.settimeout(1.0)

    def _control_receiver_thread(self):
        """控制指令接收线程"""
        logger.info("Control receiver thread started")
        while self.is_running:
            try:
                data, addr = self.control_socket.recvfrom(4096)
                if len(data) < 13:
                    continue

                magic, version, msg_type, reserved, seq = struct.unpack("=HBBBI", data[:9])
                if magic != 0xABCD:
                    continue

                # CRC 校验
                crc_received = struct.unpack("=I", data[-4:])[0]
                crc_calculated = zlib.crc32(data[:-4]) & 0xffffffff
                if crc_received != crc_calculated:
                    continue

                # 记录客户端 IP（只在 IP 变化时打印）
                new_ip = addr[0]
                if self.client_ip != new_ip:
                    logger.info(f"Client connected: {new_ip}")
                    self.client_ip = new_ip
                self.last_client_time = time.time()

                if msg_type == 0x01:  # 控制指令（10 字节键盘位图）
                    self.control_commands_received += 1
                    kb_state = data[17:-4] if len(data) >= 31 else b''
                    if self.show_input and kb_state:
                        keys = decode_keyboard_bitmap(kb_state)
                        if keys:
                            print(f"\r  Keys: {' + '.join(keys):<60}", end="", flush=True)
                        else:
                            print(f"\r  Keys: {'(none)':<60}", end="", flush=True)
                    elif self.control_commands_received % 500 == 1:
                        pressed = sum(bin(b).count('1') for b in kb_state) if kb_state else 0
                        logger.info(f"Control #{self.control_commands_received}: "
                                    f"kb={kb_state.hex() if kb_state else 'empty'} "
                                    f"({pressed} keys)")
                    self._send_ack(addr, seq)
                elif msg_type == 0x04:  # 心跳
                    self.heartbeats_received += 1
                    self._send_ack(addr, seq)
                elif msg_type == 0x02:  # 参数修改
                    self._handle_param_update(data, addr, seq)
                elif msg_type == 0x03:  # 参数查询
                    self._handle_param_query(addr, seq)

            except socket.timeout:
                continue
            except Exception as e:
                if self.is_running:
                    logger.error(f"Control receiver error: {e}")

    def _send_ack(self, addr, seq):
        """发送 ACK"""
        t2 = time.perf_counter()
        t3 = time.perf_counter()
        header = struct.pack("=HBBBI", 0xABCD, 0x01, 0x05, 0, seq)
        timestamps = struct.pack("=dd", t2, t3)
        msg = header + timestamps
        crc = zlib.crc32(msg) & 0xffffffff
        self.control_socket.sendto(msg + struct.pack("=I", crc), addr)
        self.acks_sent += 1

    def _handle_param_update(self, data: bytes, addr: tuple, seq: int):
        """处理参数修改请求 — 存储并应用到编码器"""
        import json
        try:
            payload_bytes = data[17:-4]
            params = json.loads(payload_bytes.decode('utf-8'))
            for key, value in params.items():
                if key in self._params:
                    old = self._params[key]
                    self._params[key] = value
                    if old != value:
                        logger.info(f"Param updated: {key} = {value}")
                        self._apply_param(key, value)
            self.param_updates_received += 1
            self._send_ack(addr, seq)
        except Exception as e:
            logger.error(f"Param update error: {e}")

    def _apply_param(self, key: str, value):
        """Apply a single parameter change to the running encoder/pipeline."""
        if key == 'bitrate':
            bitrate = int(value)
            self.encoder = AdaptiveEncoder(bitrate, self.fps, self.encoder.quality)
            if self._h264_encoder:
                self._h264_encoder = H264Encoder(
                    VIDEO_WIDTH, VIDEO_HEIGHT, self.fps, bitrate=bitrate * 1000)
            logger.info(f"Encoder rebuilt: bitrate={bitrate} kbps")

        elif key == 'target_fps':
            self.fps = int(value)
            self.encoder = AdaptiveEncoder(
                self._params['bitrate'], self.fps, self.encoder.quality)
            if self._h264_encoder:
                self._h264_encoder = H264Encoder(
                    VIDEO_WIDTH, VIDEO_HEIGHT, self.fps,
                    bitrate=self._params['bitrate'] * 1000)
            logger.info(f"Encoder rebuilt: fps={self.fps}")

        elif key == 'encoder':
            codec = str(value)
            if codec == 'h264' and H264_AVAILABLE:
                self._h264_encoder = H264Encoder(
                    VIDEO_WIDTH, VIDEO_HEIGHT, self.fps,
                    bitrate=self._params['bitrate'] * 1000)
                logger.info("Switched to H.264 encoder")
            else:
                self._h264_encoder = None
                logger.info("Switched to JPEG encoder")

        elif key == 'fec_enabled':
            enabled = bool(value)
            if enabled and FEC_AVAILABLE and not self._fec_encoder:
                self._fec_encoder = FECEncoder(self._params['fec_redundancy'])
                logger.info("FEC enabled")
            elif not enabled:
                self._fec_encoder = None
                logger.info("FEC disabled")

        elif key == 'fec_redundancy':
            redundancy = float(value)
            if self._fec_encoder:
                self._fec_encoder = FECEncoder(redundancy)
                logger.info(f"FEC redundancy updated: {redundancy}")

    def _handle_param_query(self, addr: tuple, seq: int):
        """处理参数查询请求 - 回复当前参数"""
        import json
        try:
            payload = json.dumps(self._params).encode('utf-8')
            header = struct.pack("=HBBBI", 0xABCD, 0x01, 0x02, 0, seq)
            t1 = struct.pack("=d", time.perf_counter())
            msg = header + t1 + payload
            crc = zlib.crc32(msg) & 0xffffffff
            self.control_socket.sendto(msg + struct.pack("=I", crc), addr)
            self._send_ack(addr, seq)
        except Exception as e:
            logger.error(f"Param query error: {e}")

    def _handle_video_nack(self, data: bytes):
        """处理视频 NACK - 重传请求的分片"""
        if not self.client_video_addr:
            return
        try:
            # 解析 NACK: header(9) + num_chunks(2) + chunk_indices(2*N) + CRC(4)
            crc_received = struct.unpack("=I", data[-4:])[0]
            crc_calculated = zlib.crc32(data[:-4]) & 0xffffffff
            if crc_received != crc_calculated:
                return
            frame_id = struct.unpack("=I", data[5:9])[0]
            num_chunks = struct.unpack("=H", data[9:11])[0]
            missing = []
            for i in range(num_chunks):
                idx = struct.unpack("=H", data[11 + i * 2:13 + i * 2])[0]
                missing.append(idx)
            # 从缓存重传
            if frame_id in self._frame_cache:
                for chunk_idx in missing:
                    if chunk_idx in self._frame_cache[frame_id]:
                        pkt = self._frame_cache[frame_id][chunk_idx]
                        self.video_socket.sendto(pkt, self.client_video_addr)
                logger.debug(f"NACK retransmit: frame {frame_id}, {len(missing)} chunks")
        except Exception as e:
            logger.error(f"NACK handle error: {e}")

    def _watchdog_thread(self):
        """客户端断连检测"""
        while self.is_running:
            time.sleep(2.0)
            if self.client_ip and self.last_client_time > 0:
                elapsed = time.time() - self.last_client_time
                if elapsed > 5.0:
                    logger.warning(f"Client {self.client_ip} disconnected (no data for {elapsed:.0f}s)")
                    self.client_ip = None
                    self.client_video_addr = None

    def _video_sender_thread(self):
        """视频发送线程 - 自适应码率彩条测试画面"""
        logger.info(f"Video sender started (target: {self.encoder.target_bitrate_kbps} kbps, "
                     f"{self.fps} fps, Q{self.encoder.quality})")
        frame_id = 0
        bytes_sent_window = 0
        window_start = time.time()

        while self.is_running:
            try:
                # drain 所有待处理包（REGISTER/ACK/NACK），避免积压
                if self.client_video_addr:
                    self.video_socket.settimeout(0.001)
                else:
                    self.video_socket.settimeout(1.0)
                while True:
                    try:
                        data, addr = self.video_socket.recvfrom(1024)
                        if data == b"REGISTER":
                            if self.client_video_addr != addr:
                                logger.info(f"Video client registered: {addr[0]}:{addr[1]}")
                            self.client_video_addr = addr
                        elif len(data) >= 9 and struct.unpack("=H", data[:2])[0] == 0xABCD:
                            msg_type = data[3]
                            if msg_type == 0x06:  # VIDEO_ACK
                                self.video_frames_acked += 1
                            elif msg_type == 0x07:  # VIDEO_NACK
                                self._handle_video_nack(data)
                    except socket.timeout:
                        break

                # 等待客户端注册
                if not self.client_video_addr:
                    continue

                # 如果客户端已断连（watchdog 确认超时），停止发送
                if self.last_client_time > 0:
                    elapsed = time.time() - self.last_client_time
                    if elapsed > 5.0:
                        logger.warning("Video: client timeout, clearing video addr")
                        self.client_video_addr = None
                        continue

                frame_start = time.perf_counter()
                frame_id += 1

                # 编码：H.264 或 JPEG
                if self._h264_encoder:
                    raw_frame = self.encoder.generate_dynamic_frame(frame_id)
                    force_key = (frame_id % 30 == 1)
                    h264_packets = self._h264_encoder.encode(raw_frame, force_keyframe=force_key)
                    if not h264_packets:
                        continue
                    frame_data = h264_packets[0]
                    codec_flag = 1  # H.264
                else:
                    frame_data = self.encoder.encode(frame_id)
                    codec_flag = 0  # JPEG

                encode_time_ms = (time.perf_counter() - frame_start) * 1000.0

                # 分片发送
                _t_fec_start = time.perf_counter()
                CHUNK_SIZE = 60000
                total_data_chunks = (len(frame_data) + CHUNK_SIZE - 1) // CHUNK_SIZE
                data_chunks = []
                for chunk_idx in range(total_data_chunks):
                    offset = chunk_idx * CHUNK_SIZE
                    data_chunks.append(frame_data[offset:offset + CHUNK_SIZE])

                # FEC 编码
                fec_enabled = self._params.get('fec_enabled', False)
                if fec_enabled and self._fec_encoder:
                    all_chunks = self._fec_encoder.encode(data_chunks)
                else:
                    all_chunks = data_chunks
                total_chunks_with_fec = len(all_chunks)
                fec_time_ms = (time.perf_counter() - _t_fec_start) * 1000.0

                _t_send_start = time.perf_counter()
                frame_packets = {}
                try:
                    for chunk_idx, chunk in enumerate(all_chunks):
                        is_parity = 1 if chunk_idx >= total_data_chunks else 0
                        # 头: [frame_id:4][total:2][idx:2][size:4][fec_flag:1][orig_chunks:2][codec:1][encode_ms:4]
                        header = struct.pack("=IHHIBHBf",
                                             frame_id, total_chunks_with_fec, chunk_idx,
                                             len(chunk), is_parity, total_data_chunks, codec_flag,
                                             encode_time_ms)
                        pkt = header + chunk
                        frame_packets[chunk_idx] = pkt
                        self.video_socket.sendto(pkt, self.client_video_addr)
                        bytes_sent_window += len(pkt)
                    self.video_frames_sent += 1
                    # 缓存帧用于 NACK 重传
                    self._frame_cache[frame_id] = frame_packets
                    if len(self._frame_cache) > self._frame_cache_max:
                        oldest = min(self._frame_cache.keys())
                        del self._frame_cache[oldest]
                except Exception as e:
                    logger.warning(f"Video send failed: {e}")
                    self.client_video_addr = None
                send_time_ms = (time.perf_counter() - _t_send_start) * 1000.0

                total_frame_ms = (time.perf_counter() - frame_start) * 1000.0
                if total_frame_ms > (1000.0 / self.fps) * 1.5:
                    logger.warning(f"[SLOW SEND] {total_frame_ms:.1f}ms | "
                                   f"encode={encode_time_ms:.1f} fec={fec_time_ms:.1f} "
                                   f"send={send_time_ms:.1f} "
                                   f"chunks={total_data_chunks}+{total_chunks_with_fec - total_data_chunks} "
                                   f"size={len(frame_data)}")

                # 码率统计（每 5 秒打印）
                now = time.time()
                win_elapsed = now - window_start
                if win_elapsed >= 5.0:
                    bitrate_kbps = (bytes_sent_window * 8) / (win_elapsed * 1000)
                    logger.info(f"Video: {bitrate_kbps:.0f} kbps, Q{self.encoder.quality}, "
                                f"{len(frame_data)} bytes/frame")
                    bytes_sent_window = 0
                    window_start = now

                # 精确帧间隔控制
                elapsed = time.perf_counter() - frame_start
                sleep_time = max(0.001, (1.0 / self.fps) - elapsed)
                time.sleep(sleep_time)

            except Exception as e:
                if self.is_running:
                    logger.error(f"Video sender error: {e}")

    def print_statistics(self):
        logger.info("=" * 50)
        logger.info(f"Control: {self.control_commands_received} cmds, "
                     f"HB: {self.heartbeats_received}, ACK: {self.acks_sent}, "
                     f"Params: {self.param_updates_received}, "
                     f"Video: {self.video_frames_sent} sent / {self.video_frames_acked} acked")
        if self.client_ip:
            logger.info(f"Client: {self.client_ip}")
        logger.info("=" * 50)


def main():
    parser = argparse.ArgumentParser(description="Air Unit Server")
    parser.add_argument("--name", default="air_unit_01")
    parser.add_argument("--control-port", type=int, default=6000)
    parser.add_argument("--video-port", type=int, default=5000)
    parser.add_argument("--bitrate", type=int, default=DEFAULT_TARGET_BITRATE_KBPS,
                        help="Target bitrate in kbps (default: 2000)")
    parser.add_argument("--fps", type=int, default=DEFAULT_FPS,
                        help="Target FPS (default: 30)")
    parser.add_argument("--quality", type=int, default=DEFAULT_JPEG_QUALITY,
                        help="Initial JPEG quality (default: 50)")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--codec", choices=["jpeg", "h264"], default="h264",
                        help="Video codec (default: h264)")
    parser.add_argument("--fec", action="store_true", help="Enable FEC")
    parser.add_argument("--show-input", action="store_true",
                        help="Real-time display of keyboard input data")
    args = parser.parse_args()

    if args.verbose:
        logging.getLogger().setLevel(logging.DEBUG)

    server = AirUnitServer(args.name, args.control_port, args.video_port,
                           args.bitrate, args.fps, args.quality)
    if args.codec == 'jpeg':
        server._params['encoder'] = 'jpeg'
        server._h264_encoder = None
    if args.fec:
        server._params['fec_enabled'] = True
    if args.show_input:
        server.show_input = True
    server.start()

    try:
        while True:
            time.sleep(10)
            server.print_statistics()
    except KeyboardInterrupt:
        logger.info("\nStopping...")
    finally:
        server.print_statistics()
        server.stop()


if __name__ == "__main__":
    main()
