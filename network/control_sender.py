"""控制发送线程"""

import threading
import socket
import time
import json
import logging
from collections import deque
from typing import Optional, Callable, Dict
from config import Config
from network.protocol import Protocol, MSG_TYPE_ACK, MSG_TYPE_PARAM_UPDATE, KEYBOARD_STATE_SIZE
from network.keyboard_encoder import KeyboardEncoder
from logic.latency_calculator import LatencyCalculator


logger = logging.getLogger(__name__)


class ControlSender:
    """控制发送线程 - 发送控制指令并等待ACK"""

    def __init__(self):
        self.socket: Optional[socket.socket] = None
        self.remote_addr: Optional[tuple] = None
        self.is_running = False

        # 键盘编码器
        self.keyboard = KeyboardEncoder()

        # 延迟计算
        self.latency_calc = LatencyCalculator()

        # 待确认的消息 {seq: (send_time, retry_count)}
        self._pending_acks: Dict[int, tuple] = {}
        self._pending_lock = threading.Lock()

        # READY 状态（F5 切换，NOT READY 时发全零）
        self.is_ready = False
        self._zero_state = b'\x00' * KEYBOARD_STATE_SIZE

        # 鼠标状态（由 app.py 每帧更新）
        self._mouse_dx = 0
        self._mouse_dy = 0
        self._mouse_buttons = 0
        self._scroll_delta = 0
        self._mouse_lock = threading.Lock()

        # 回调
        self.on_error: Optional[Callable] = None
        self.on_param_response: Optional[Callable[[dict], None]] = None
        self.on_ready_changed: Optional[Callable[[bool], None]] = None

        # 统计
        self._stats_lock = threading.Lock()
        self.commands_sent = 0
        self.acks_received = 0
        self.retransmits = 0
        self.timeout_errors = 0
        self._param_seq = 0

        # 滑动窗口丢包率
        self._send_times: deque = deque(maxlen=200)
        self._ack_times: deque = deque(maxlen=200)

    def start(self, server_ip: str, server_port: int):
        """启动发送"""
        if self.is_running:
            return

        self.remote_addr = (server_ip, server_port)
        self.is_running = True

        # 启动键盘监听
        self.keyboard.on_f5_pressed = self._toggle_ready
        self.keyboard.start()

        # 启动发送线程
        tx_thread = threading.Thread(target=self._tx_thread, daemon=True)
        tx_thread.start()

        # 启动接收线程（接收ACK）
        rx_thread = threading.Thread(target=self._rx_thread, daemon=True)
        rx_thread.start()

        logger.info(f"ControlSender started ({server_ip}:{server_port})")

    def _toggle_ready(self):
        """F5 切换 READY 状态"""
        self.set_ready(not self.is_ready)

    def set_ready(self, ready: bool):
        """设置 READY 状态（供外部调用：断连、失焦等）"""
        if self.is_ready == ready:
            return
        self.is_ready = ready
        logger.info(f"READY: {self.is_ready}")
        if self.on_ready_changed:
            self.on_ready_changed(self.is_ready)

    def stop(self):
        """停止发送"""
        self.set_ready(False)
        self.is_running = False
        self.keyboard.stop()
        sock = self.socket
        self.socket = None
        if sock:
            try:
                sock.close()
            except Exception:
                pass
        logger.info("ControlSender stopped")

    def _tx_thread(self):
        """发送线程"""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.socket.bind(('0.0.0.0', 0))
            self.socket.settimeout(0.1)

            seq = 0
            interval = 1.0 / Config.TX_SEND_RATE
            last_send_time = time.time()

            while self.is_running:
                current_time = time.time()
                elapsed = current_time - last_send_time

                # 定期发送控制指令
                if elapsed >= interval:
                    seq += 1
                    self._send_control_command(seq)
                    last_send_time = current_time

                # 检查超时重传
                self._check_retransmit()

                time.sleep(0.001)

        except Exception as e:
            logger.error(f"TX thread error: {e}")
            if self.on_error:
                self.on_error(str(e))

    def _rx_thread(self):
        """接收线程 - 接收 ACK 和参数响应"""
        try:
            while self.is_running:
                if not self.socket:
                    time.sleep(0.01)
                    continue

                try:
                    data, addr = self.socket.recvfrom(4096)
                    if len(data) < 13:
                        continue
                    import struct
                    msg_type = struct.unpack('B', data[3:4])[0]
                    if msg_type == MSG_TYPE_ACK:
                        self._process_ack(data)
                    elif msg_type == MSG_TYPE_PARAM_UPDATE:
                        self._process_param_response(data)
                except socket.timeout:
                    pass
                except (ConnectionResetError, OSError):
                    pass

        except Exception as e:
            logger.error(f"RX thread error: {e}")

    def update_mouse(self, dx: int, dy: int, buttons: int, scroll: int):
        """由 app.py 每帧调用，更新鼠标状态供下一次控制指令使用"""
        with self._mouse_lock:
            self._mouse_dx += dx   # 累加，避免帧间丢失
            self._mouse_dy += dy
            self._mouse_buttons = buttons
            self._scroll_delta = scroll

    def _send_control_command(self, seq: int):
        """发送控制指令 — READY 时发真实位图，否则发全零"""
        try:
            sock = self.socket
            if not sock or not self.remote_addr:
                return

            polled = self.keyboard.get_state()
            keyboard_state = polled if self.is_ready else self._zero_state

            with self._mouse_lock:
                mouse_dx = self._mouse_dx
                mouse_dy = self._mouse_dy
                mouse_buttons = self._mouse_buttons if self.is_ready else 0
                scroll_delta = self._scroll_delta
                self._mouse_dx = 0  # 消费累积量
                self._mouse_dy = 0
                self._scroll_delta = 0

            if not self.is_ready:
                mouse_dx = mouse_dy = scroll_delta = 0

            t1 = time.perf_counter()
            message = Protocol.build_control_command(
                seq=seq,
                t1=t1,
                keyboard_state=keyboard_state,
                mouse_dx=mouse_dx,
                mouse_dy=mouse_dy,
                mouse_buttons=mouse_buttons,
                scroll_delta=scroll_delta,
            )

            sock.sendto(message, self.remote_addr)

            # 记录待确认
            with self._pending_lock:
                self._pending_acks[seq] = (time.time(), 0)
                self.latency_calc.record_send(seq, t1)

            with self._stats_lock:
                self.commands_sent += 1
                self._send_times.append(time.time())

        except Exception as e:
            if self.is_running:
                logger.error(f"Send error: {e}")
                if self.on_error:
                    self.on_error(str(e))

    def _process_ack(self, data: bytes):
        """处理ACK"""
        try:
            seq, t2, t3 = Protocol.parse_ack(data)

            # 记录ACK时间
            t4 = time.perf_counter()
            self.latency_calc.record_ack(seq, t2, t3, t4)

            # 移除待确认
            with self._pending_lock:
                if seq in self._pending_acks:
                    del self._pending_acks[seq]

            with self._stats_lock:
                self.acks_received += 1
                self._ack_times.append(time.time())

        except Exception as e:
            logger.debug(f"ACK parse error: {e}")

    def _check_retransmit(self):
        """检查超时重传"""
        current_time = time.time()
        to_retransmit = []

        with self._pending_lock:
            for seq, (send_time, retry_count) in list(self._pending_acks.items()):
                elapsed = current_time - send_time

                if elapsed > 0.1 and retry_count < 3:
                    to_retransmit.append((seq, retry_count))
                elif elapsed > 0.1 and retry_count >= 3:
                    del self._pending_acks[seq]
                    with self._stats_lock:
                        self.timeout_errors += 1

        for seq, retry_count in to_retransmit:
            self._retransmit_command(seq, retry_count)

    def _retransmit_command(self, seq: int, retry_count: int):
        """重传控制指令"""
        try:
            sock = self.socket
            if not sock or not self.remote_addr:
                return

            polled = self.keyboard.get_state()
            keyboard_state = polled if self.is_ready else self._zero_state

            t1 = time.perf_counter()
            message = Protocol.build_control_command(
                seq=seq,
                t1=t1,
                keyboard_state=keyboard_state,
            )

            sock.sendto(message, self.remote_addr)

            # 更新待确认
            with self._pending_lock:
                if seq in self._pending_acks:
                    self._pending_acks[seq] = (time.time(), retry_count + 1)

            with self._stats_lock:
                self.retransmits += 1

            logger.debug(f"Retransmit seq={seq}, retry={retry_count + 1}")

        except Exception as e:
            logger.error(f"Retransmit error: {e}")

    def get_recent_loss(self, window: float = 1.0) -> float:
        """计算最近 window 秒内的丢包率"""
        now = time.time()
        cutoff = now - window
        with self._stats_lock:
            sent = sum(1 for t in self._send_times if t >= cutoff)
            acked = sum(1 for t in self._ack_times if t >= cutoff)
        if sent == 0:
            return 0.0
        return max(0.0, (sent - acked) / sent)

    def send_param_update(self, params: dict):
        """发送参数修改到机载端（fire-and-forget）"""
        try:
            if not self.socket or not self.remote_addr:
                return
            self._param_seq += 1
            t1 = time.perf_counter()
            message = Protocol.build_param_update(self._param_seq, t1, params)
            self.socket.sendto(message, self.remote_addr)
            logger.info(f"Sent param update: {params}")
        except Exception as e:
            logger.error(f"Param update send error: {e}")

    def send_param_query(self):
        """发送参数查询到机载端"""
        try:
            if not self.socket or not self.remote_addr:
                return
            self._param_seq += 1
            t1 = time.perf_counter()
            message = Protocol.build_param_query(self._param_seq, t1)
            self.socket.sendto(message, self.remote_addr)
            logger.info("Sent param query")
        except Exception as e:
            logger.error(f"Param query send error: {e}")

    def _process_param_response(self, data: bytes):
        """处理机载端返回的参数数据"""
        try:
            msg_type, seq, t1, payload = Protocol.parse_message(data)
            if payload:
                params = json.loads(payload.decode('utf-8'))
                logger.info(f"Received params from air unit: {params}")
                if self.on_param_response:
                    self.on_param_response(params)
        except Exception as e:
            logger.debug(f"Param response parse error: {e}")

    def get_statistics(self) -> dict:
        """获取统计"""
        with self._stats_lock:
            rtt_min = self.latency_calc.get_min_rtt()
            rtt_max = self.latency_calc.get_max_rtt()
            return {
                "commands_sent": self.commands_sent,
                "acks_received": self.acks_received,
                "retransmits": self.retransmits,
                "rtt_avg": self.latency_calc.get_average_rtt(),
                "packets_sent": self.commands_sent,
                "packets_lost": max(0, self.commands_sent - self.acks_received),
                "packets_retransmitted": self.retransmits,
                "timeout_errors": self.timeout_errors,
                "latency_min_ms": (rtt_min * 1000.0) if rtt_min else 0.0,
                "latency_max_ms": (rtt_max * 1000.0) if rtt_max else 0.0,
            }
