"""心跳管理"""

import threading
import socket
import time
import logging
from typing import Optional, Callable, Dict
from config import Config
from network.protocol import Protocol


logger = logging.getLogger(__name__)


class HeartbeatManager:
    """心跳管理 - 定期发送心跳并检测连接状态"""

    def __init__(self):
        self.socket: Optional[socket.socket] = None
        self.remote_addr: Optional[tuple] = None
        self.is_running = False

        # 待确认的心跳 {seq: send_time}
        self._pending_heartbeats: Dict[int, float] = {}
        self._pending_lock = threading.Lock()

        # 连接状态
        self._connection_lost = False
        self._timeout_count = 0

        # 回调
        self.on_error: Optional[Callable] = None
        self.on_connection_lost: Optional[Callable] = None
        self.on_connection_restored: Optional[Callable] = None
        self.on_first_ack: Optional[Callable] = None  # 首次握手成功
        self._first_ack_fired = False

        # 统计
        self._stats_lock = threading.Lock()
        self.heartbeats_sent = 0
        self.heartbeats_acked = 0
        self.timeouts = 0

    def start(self, server_ip: str, server_port: int):
        """启动心跳"""
        if self.is_running:
            return

        self.remote_addr = (server_ip, server_port)
        self.is_running = True
        self._connection_lost = False
        self._timeout_count = 0

        # 启动发送线程
        tx_thread = threading.Thread(target=self._tx_thread, daemon=True)
        tx_thread.start()

        # 启动接收线程（接收ACK）
        rx_thread = threading.Thread(target=self._rx_thread, daemon=True)
        rx_thread.start()

        logger.info(f"HeartbeatManager started ({server_ip}:{server_port})")

    def stop(self):
        """停止心跳"""
        self.is_running = False
        sock = self.socket
        self.socket = None
        if sock:
            try:
                sock.close()
            except Exception:
                pass
        logger.info("HeartbeatManager stopped")

    def _tx_thread(self):
        """发送线程"""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.socket.bind(('0.0.0.0', 0))
            self.socket.settimeout(0.1)

            seq = 0
            interval = Config.HEARTBEAT_INTERVAL

            while self.is_running:
                seq += 1
                self._send_heartbeat(seq)

                # 检查超时
                self._check_timeout()

                time.sleep(interval)

        except Exception as e:
            logger.error(f"TX thread error: {e}")
            if self.on_error:
                self.on_error(str(e))

    def _rx_thread(self):
        """接收线程 - 接收心跳ACK"""
        try:
            while self.is_running:
                if not self.socket:
                    time.sleep(0.01)
                    continue

                try:
                    data, addr = self.socket.recvfrom(4096)
                    if len(data) >= 13:
                        self._process_heartbeat_ack(data)
                except socket.timeout:
                    pass
                except ConnectionResetError:
                    pass  # Windows ICMP port unreachable — 忽略
                except Exception as e:
                    if self.is_running:
                        logger.error(f"RX error: {e}")

        except Exception as e:
            logger.error(f"RX thread error: {e}")

    def _send_heartbeat(self, seq: int):
        """发送心跳"""
        try:
            if not self.socket or not self.remote_addr:
                return

            t1 = time.perf_counter()
            message = Protocol.build_heartbeat(seq=seq, t1=t1)

            self.socket.sendto(message, self.remote_addr)

            # 记录待确认
            with self._pending_lock:
                self._pending_heartbeats[seq] = time.time()

            with self._stats_lock:
                self.heartbeats_sent += 1

        except Exception as e:
            logger.error(f"Send heartbeat error: {e}")
            if self.on_error:
                self.on_error(str(e))

    def _process_heartbeat_ack(self, data: bytes):
        """处理心跳ACK"""
        try:
            seq, t2, t3 = Protocol.parse_ack(data)

            with self._pending_lock:
                if seq in self._pending_heartbeats:
                    del self._pending_heartbeats[seq]

            with self._stats_lock:
                self.heartbeats_acked += 1

            # 收到 ACK 就重置超时计数
            self._timeout_count = 0

            if not self._first_ack_fired:
                self._first_ack_fired = True
                logger.info("First heartbeat ACK received — handshake OK")
                if self.on_first_ack:
                    self.on_first_ack()

            if self._connection_lost:
                self._connection_lost = False
                logger.info("Connection restored")
                if self.on_connection_restored:
                    self.on_connection_restored()

        except Exception as e:
            logger.debug(f"Heartbeat ACK parse error: {e}")

    def _check_timeout(self):
        """检查超时 — 清理过期心跳，连续超时触发断连"""
        current_time = time.time()
        timeout_seqs = []

        with self._pending_lock:
            for seq, send_time in list(self._pending_heartbeats.items()):
                if current_time - send_time > 5.0:
                    timeout_seqs.append(seq)
            for seq in timeout_seqs:
                del self._pending_heartbeats[seq]

        if timeout_seqs:
            with self._stats_lock:
                self.timeouts += len(timeout_seqs)
            self._timeout_count += len(timeout_seqs)

            if self._timeout_count >= 3 and not self._connection_lost:
                self._connection_lost = True
                logger.warning("Connection lost (3 timeouts)")
                if self.on_connection_lost:
                    self.on_connection_lost()

    def is_connected(self) -> bool:
        """检查连接状态"""
        return not self._connection_lost

    def get_statistics(self) -> dict:
        """获取统计"""
        with self._stats_lock:
            return {
                "heartbeats_sent": self.heartbeats_sent,
                "heartbeats_acked": self.heartbeats_acked,
                "timeouts": self.timeouts,
                "connected": self.is_connected(),
            }
