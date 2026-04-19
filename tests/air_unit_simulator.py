"""机载端 mDNS 测试脚本 - 模拟机载端服务"""

import socket
import struct
import time
import threading
import logging
from zeroconf import ServiceInfo, Zeroconf
import argparse


logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


class AirUnitSimulator:
    """机载端模拟器 - 提供 mDNS 服务和基础通信"""

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

        # 统计
        self.control_commands_received = 0
        self.acks_sent = 0
        self.video_frames_sent = 0

        # 线程控制
        self.is_running = False

    def start(self):
        """启动机载端模拟器"""
        logger.info(f"Starting Air Unit Simulator: {self.air_unit_name}")

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

        logger.info("Air Unit Simulator started successfully")

    def stop(self):
        """停止机载端模拟器"""
        logger.info("Stopping Air Unit Simulator")
        self.is_running = False

        if self.zeroconf:
            self.zeroconf.close()

        if self.control_socket:
            self.control_socket.close()

        if self.video_socket:
            self.video_socket.close()

        logger.info("Air Unit Simulator stopped")

    def _start_mdns(self):
        """启动 mDNS 服务"""
        try:
            # 获取本机 IP
            hostname = socket.gethostname()
            local_ip = socket.gethostbyname(hostname)

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
                },
                server=f"{self.air_unit_name}.local.",
            )

            # 注册服务
            self.zeroconf = Zeroconf()
            self.zeroconf.register_service(self.service_info)

            logger.info(f"mDNS service registered: {self.air_unit_name}._pip_link._udp.local.")
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
                    logger.warning(f"Invalid magic: {hex(magic)}")
                    continue

                # 解析时间戳
                t1 = struct.unpack("=d", data[9:17])[0]

                self.control_commands_received += 1

                logger.debug(f"Received control command from {addr[0]}:{addr[1]}")
                logger.debug(f"  Seq: {seq}, t1: {t1:.6f}")

                # 发送 ACK
                self._send_ack(addr, seq)

            except socket.timeout:
                continue
            except Exception as e:
                if self.is_running:
                    logger.error(f"Control receiver error: {e}")

        logger.info("Control receiver thread stopped")

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
            import zlib
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
        client_addr = None

        while self.is_running:
            try:
                # 尝试接收来自客户端的任何数据（用于获取客户端地址）
                try:
                    data, addr = self.video_socket.recvfrom(1)
                    client_addr = addr
                    logger.info(f"Video client connected: {addr[0]}:{addr[1]}")
                except socket.timeout:
                    pass

                # 如果有客户端，发送模拟视频帧
                if client_addr:
                    frame_id += 1

                    # 构建模拟视频帧（简单的测试数据）
                    frame_data = struct.pack(
                        "=I",
                        frame_id
                    ) + b"VIDEO_FRAME_DATA" * 100  # 1600 字节

                    try:
                        self.video_socket.sendto(frame_data, client_addr)
                        self.video_frames_sent += 1

                        if frame_id % 30 == 0:
                            logger.debug(f"Sent {frame_id} video frames")

                    except Exception as e:
                        logger.warning(f"Failed to send video frame: {e}")
                        client_addr = None

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
        logger.info(f"ACKs sent: {self.acks_sent}")
        logger.info(f"Video frames sent: {self.video_frames_sent}")
        logger.info("=" * 60)


def main():
    parser = argparse.ArgumentParser(description="Air Unit mDNS Test Simulator")
    parser.add_argument("--name", default="air_unit_01", help="Air unit name (default: air_unit_01)")
    parser.add_argument("--control-port", type=int, default=6000, help="Control port (default: 6000)")
    parser.add_argument("--video-port", type=int, default=5000, help="Video port (default: 5000)")
    parser.add_argument("--duration", type=int, default=0, help="Run duration in seconds (0 = infinite)")

    args = parser.parse_args()

    # 创建模拟器
    simulator = AirUnitSimulator(
        air_unit_name=args.name,
        control_port=args.control_port,
        video_port=args.video_port
    )

    # 启动
    simulator.start()

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
                    simulator.print_statistics()

    except KeyboardInterrupt:
        logger.info("Interrupted by user")
    finally:
        simulator.print_statistics()
        simulator.stop()


if __name__ == "__main__":
    main()
