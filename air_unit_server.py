#!/usr/bin/env python3
"""机载端测试脚本 - 在 Ubuntu 上运行，测试与客户端的远程连接"""

import socket
import struct
import time
import threading
import logging
import argparse
import sys
from zeroconf import ServiceInfo, Zeroconf
import zlib


logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


class AirUnitServer:
    """机载端服务器 - 接收客户端连接并响应"""

    def __init__(self, air_unit_name: str = "air_unit_01", control_port: int = 6000, video_port: int = 5000):
        self.air_unit_name = air_unit_name
        self.control_port = control_port
        self.video_port = video_port

        # mDNS 服务
        self.zeroconf = None
        self.service_info = None

        # UDP 套接字
        self.control_socket = None
        self.video_socket = None

        # 客户端信息
        self.client_addr = None
        self.last_heartbeat_time = 0

        # 统计
        self.control_commands_received = 0
        self.acks_sent = 0
        self.video_frames_sent = 0
        self.heartbeats_received = 0

        # 线程控制
        self.is_running = False

    def start(self):
        """启动机载端服务器"""
        logger.info(f"Starting Air Unit Server: {self.air_unit_name}")

        try:
            # 启动 mDNS 服务
            self._start_mdns()

            # 启动 UDP 服务器
            self._start_udp_servers()

            # 启动接收线程
            self.is_running = True
            control_thread = threading.Thread(target=self._control_receiver_thread, daemon=True)
            control_thread.start()

            video_thread = threading.Thread(target=self._video_sender_thread, daemon=True)
            video_thread.start()

            logger.info("Air Unit Server started successfully")
            logger.info(f"Waiting for client connection on {self.control_port}...")

        except Exception as e:
            logger.error(f"Failed to start server: {e}")
            raise

    def stop(self):
        """停止机载端服务器"""
        logger.info("Stopping Air Unit Server")
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
        try:
            # 获取本机 IP
            hostname = socket.gethostname()
            local_ip = socket.gethostbyname(hostname)

            logger.info(f"Local hostname: {hostname}")
            logger.info(f"Local IP: {local_ip}")

            # 创建服务信息
            self.service_info = ServiceInfo(
                "_pip_link._udp.local.",
                f"{self.air_unit_name}._pip_link._udp.local.",
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

            # 注册服务
            self.zeroconf = Zeroconf()
            self.zeroconf.register_service(self.service_info)

            logger.info(f"mDNS service registered: {self.air_unit_name}._pip_link._udp.local.")
            logger.info(f"  IP: {local_ip}")
            logger.info(f"  Control Port: {self.control_port}")
            logger.info(f"  Video Port: {self.video_port}")

        except Exception as e:
            logger.error(f"Failed to start mDNS: {e}")
            raise

    def _start_udp_servers(self):
        """启动 UDP 服务器"""
        try:
            # 控制指令接收
            self.control_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.control_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.control_socket.bind(("0.0.0.0", self.control_port))
            self.control_socket.settimeout(1.0)

            logger.info(f"Control socket listening on port {self.control_port}")

            # 视频发送
            self.video_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.video_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.video_socket.bind(("0.0.0.0", self.video_port))
            self.video_socket.settimeout(1.0)

            logger.info(f"Video socket listening on port {self.video_port}")

        except Exception as e:
            logger.error(f"Failed to start UDP servers: {e}")
            raise

    def _control_receiver_thread(self):
        """控制指令接收线程"""
        logger.info("Control receiver thread started")

        while self.is_running:
            try:
                data, addr = self.control_socket.recvfrom(4096)

                if len(data) < 13:
                    continue

                # 解析消息头
                magic, version, msg_type, reserved, seq = struct.unpack(
                    "=HBBBI",
                    data[:9]
                )

                # 验证 Magic
                if magic != 0xABCD:
                    logger.warning(f"Invalid magic from {addr}: {hex(magic)}")
                    continue

                # 验证 CRC
                crc_received = struct.unpack("=I", data[-4:])[0]
                crc_calculated = zlib.crc32(data[:-4]) & 0xffffffff
                if crc_received != crc_calculated:
                    logger.warning(f"CRC check failed from {addr}")
                    continue

                # 记录客户端地址
                if self.client_addr != addr:
                    logger.info(f"Client connected: {addr[0]}:{addr[1]}")
                    self.client_addr = addr

                # 处理不同的消息类型
                if msg_type == 0x01:  # 控制指令
                    self._handle_control_command(addr, seq, data)
                elif msg_type == 0x04:  # 心跳
                    self._handle_heartbeat(addr, seq)

            except socket.timeout:
                continue
            except Exception as e:
                if self.is_running:
                    logger.error(f"Control receiver error: {e}")

        logger.info("Control receiver thread stopped")

    def _handle_control_command(self, addr: tuple, seq: int, data: bytes):
        """处理控制指令"""
        try:
            # 解析时间戳
            t1 = struct.unpack("=d", data[9:17])[0]

            self.control_commands_received += 1

            logger.debug(f"Received control command: seq={seq}, t1={t1:.6f}")

            # 发送 ACK
            self._send_ack(addr, seq)

        except Exception as e:
            logger.error(f"Failed to handle control command: {e}")

    def _handle_heartbeat(self, addr: tuple, seq: int):
        """处理心跳"""
        try:
            self.heartbeats_received += 1
            self.last_heartbeat_time = time.time()

            logger.debug(f"Received heartbeat: seq={seq}")

            # 发送 ACK
            self._send_ack(addr, seq)

        except Exception as e:
            logger.error(f"Failed to handle heartbeat: {e}")

    def _send_ack(self, addr: tuple, seq: int):
        """发送 ACK"""
        try:
            t2 = time.perf_counter()
            t3 = time.perf_counter()

            # 构建 ACK 消息
            header = struct.pack(
                "=HBBBI",
                0xABCD,  # Magic
                0x01,    # Version
                0x05,    # MSG_TYPE_ACK
                0,       # Reserved
                seq
            )

            timestamps = struct.pack("=dd", t2, t3)
            message_without_crc = header + timestamps

            # 计算 CRC32
            crc = zlib.crc32(message_without_crc) & 0xffffffff

            # 添加 CRC
            message = message_without_crc + struct.pack("=I", crc)

            # 发送
            self.control_socket.sendto(message, addr)
            self.acks_sent += 1

            logger.debug(f"Sent ACK to {addr[0]}:{addr[1]}, seq={seq}")

        except Exception as e:
            logger.error(f"Failed to send ACK: {e}")

    def _video_sender_thread(self):
        """视频发送线程 - 模拟发送视频帧"""
        logger.info("Video sender thread started")

        frame_id = 0

        while self.is_running:
            try:
                # 如果有客户端，发送模拟视频帧
                if self.client_addr:
                    frame_id += 1

                    # 构建模拟视频帧（简单的测试数据）
                    frame_data = struct.pack(
                        "=I",
                        frame_id
                    ) + b"VIDEO_FRAME_DATA" * 100  # 1600 字节

                    try:
                        self.video_socket.sendto(frame_data, self.client_addr)
                        self.video_frames_sent += 1

                        if frame_id % 30 == 0:
                            logger.debug(f"Sent {frame_id} video frames")

                    except Exception as e:
                        logger.warning(f"Failed to send video frame: {e}")
                        self.client_addr = None

                time.sleep(0.033)  # ~30fps

            except Exception as e:
                if self.is_running:
                    logger.error(f"Video sender error: {e}")

        logger.info("Video sender thread stopped")

    def print_statistics(self):
        """打印统计信息"""
        logger.info("=" * 60)
        logger.info("Air Unit Statistics")
        logger.info("=" * 60)
        logger.info(f"Control commands received: {self.control_commands_received}")
        logger.info(f"Heartbeats received: {self.heartbeats_received}")
        logger.info(f"ACKs sent: {self.acks_sent}")
        logger.info(f"Video frames sent: {self.video_frames_sent}")
        if self.client_addr:
            logger.info(f"Connected client: {self.client_addr[0]}:{self.client_addr[1]}")
        logger.info("=" * 60)


def main():
    parser = argparse.ArgumentParser(description="Air Unit Server - Test remote connection")
    parser.add_argument("--name", default="air_unit_01", help="Air unit name (default: air_unit_01)")
    parser.add_argument("--control-port", type=int, default=6000, help="Control port (default: 6000)")
    parser.add_argument("--video-port", type=int, default=5000, help="Video port (default: 5000)")
    parser.add_argument("--duration", type=int, default=0, help="Run duration in seconds (0 = infinite)")
    parser.add_argument("--verbose", action="store_true", help="Enable verbose logging")

    args = parser.parse_args()

    # 设置日志级别
    if args.verbose:
        logging.getLogger().setLevel(logging.DEBUG)

    # 创建服务器
    server = AirUnitServer(
        air_unit_name=args.name,
        control_port=args.control_port,
        video_port=args.video_port
    )

    # 启动
    server.start()

    try:
        if args.duration > 0:
            logger.info(f"Running for {args.duration} seconds...")
            time.sleep(args.duration)
        else:
            logger.info("Running indefinitely. Press Ctrl+C to stop.")
            while True:
                time.sleep(1)
                # 每 10 秒打印一次统计
                if int(time.time()) % 10 == 0:
                    server.print_statistics()

    except KeyboardInterrupt:
        logger.info("\nInterrupted by user")
    finally:
        server.print_statistics()
        server.stop()


if __name__ == "__main__":
    main()
