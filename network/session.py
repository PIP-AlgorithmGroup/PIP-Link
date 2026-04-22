"""会话管理 - 连接生命周期管理"""

import threading
import time
import logging
from enum import Enum
from typing import Optional, Callable
from config import Config
from network.service_discovery import ServiceDiscovery
from network.video_process import VideoReceiverProcess
from network.control_sender import ControlSender
from network.heartbeat import HeartbeatManager


logger = logging.getLogger(__name__)


class SessionState(Enum):
    """会话状态"""
    IDLE = "idle"
    DISCOVERING = "discovering"
    CONNECTING = "connecting"
    CONNECTED = "connected"
    DISCONNECTED = "disconnected"
    RECONNECTING = "reconnecting"


class SessionManager:
    """会话管理器 - 管理连接生命周期"""

    def __init__(self):
        self.state = SessionState.IDLE
        self._lock = threading.RLock()

        # 网络组件
        self.service_discovery: Optional[ServiceDiscovery] = None
        self.video_receiver: Optional[VideoReceiverProcess] = None
        self.control_sender: Optional[ControlSender] = None
        self.heartbeat: Optional[HeartbeatManager] = None

        # 服务器信息
        self.server_ip: str = ""
        self.control_port: int = 0
        self.video_port: int = 0
        self._handshake_event = threading.Event()

        # 回调
        self.on_state_changed: Optional[Callable] = None
        self.on_services_discovered: Optional[Callable] = None
        self.on_error: Optional[Callable] = None
        self.on_param_response: Optional[Callable] = None
        self.on_ready_changed: Optional[Callable] = None

        # 自动重连
        self._reconnect_thread: Optional[threading.Thread] = None
        self._reconnect_enabled = False
        self._reconnect_interval = 5.0
        self._max_reconnect_attempts = 3

        # 上次连接的服务信息（用于重连）
        self._last_service_name: str = ""
        self._last_service_info: dict = {}

    def start_discovery(self, service_name: str):
        """启动 mDNS 发现（后台线程）"""
        with self._lock:
            if self.state != SessionState.IDLE:
                return

            self._set_state(SessionState.DISCOVERING)

            # 在后台线程中执行服务发现
            discovery_thread = threading.Thread(
                target=self._discovery_thread,
                args=(service_name,),
                daemon=True
            )
            discovery_thread.start()

    def _discovery_thread(self, service_name: str):
        """服务发现线程 — 实时回调发现的服务"""
        try:
            self.service_discovery = ServiceDiscovery()
            self.service_discovery.start()

            logger.info(f"Scanning for services: {service_name}")

            # 扫描 5 秒，实时回调发现的服务
            import time
            start_time = time.time()
            last_reported = {}  # 已报告过的服务

            while time.time() - start_time < 5.0:
                # 如果已被连接流程接管，提前退出扫描
                if self.state != SessionState.DISCOVERING:
                    logger.info("Discovery interrupted by connection")
                    return

                time.sleep(0.2)  # 每 200ms 检查一次

                if not self.service_discovery:
                    return
                all_services = self.service_discovery.get_all_services()
                if all_services:
                    # 找出新发现的服务
                    new_services = {}
                    for svc_name, svc_info in all_services.items():
                        if svc_name not in last_reported:
                            new_services[svc_name] = svc_info
                            last_reported[svc_name] = svc_info
                            logger.info(f"Found service: {svc_name} ({svc_info.get('addresses', ['unknown'])[0]}:{svc_info.get('port', 0)})")

                    # 实时回调新发现的服务
                    if new_services and self.on_services_discovered:
                        self.on_services_discovered(new_services)

            # 扫描完成，仅在仍处于 DISCOVERING 时回到 IDLE
            with self._lock:
                if self.state == SessionState.DISCOVERING:
                    self._set_state(SessionState.IDLE)

            if last_reported:
                logger.info(f"Discovery complete: found {len(last_reported)} service(s)")
            else:
                logger.warning("No services found")

        except Exception as e:
            logger.error(f"Discovery thread error: {e}")
            with self._lock:
                if self.state == SessionState.DISCOVERING:
                    self._set_state(SessionState.IDLE)
            if self.on_error:
                self.on_error(str(e))
        finally:
            # 仅在仍处于 DISCOVERING 时清理（连接流程会自行管理）
            if self.state == SessionState.DISCOVERING and self.service_discovery:
                self.service_discovery.stop()
                self.service_discovery = None

    def connect_to_service(self, service_name: str, service_info: dict):
        """连接到指定的服务（非阻塞，后台握手）"""
        try:
            # 解析服务信息
            addresses = service_info.get('addresses', [])
            port = service_info.get('port', 0)
            properties = service_info.get('properties', {})

            if not addresses or not port:
                logger.error("Invalid service info")
                return

            # 保存服务信息用于重连
            self._last_service_name = service_name
            self._last_service_info = service_info

            with self._lock:
                # 先清理旧连接（高频连接/断开时防止资源泄漏）
                self._disconnect_internal()

            self.server_ip = addresses[0]
            self.control_port = port
            video_port_str = properties.get('video_port', '')
            if video_port_str:
                self.video_port = int(video_port_str)
            else:
                self.video_port = port - Config.VIDEO_PORT_OFFSET

            logger.info(f"Connecting to: {self.server_ip}:{self.control_port} (video: {self.video_port})")
            threading.Thread(target=self._connect_to_server, daemon=True).start()

        except Exception as e:
            logger.error(f"Connect error: {e}")
            self._set_state(SessionState.IDLE)
            if self.on_error:
                self.on_error(str(e))

    def _connect_to_server(self):
        """连接到服务器（带握手确认）"""
        with self._lock:
            try:
                self._set_state(SessionState.CONNECTING)

                # 停止 mDNS 发现
                if self.service_discovery:
                    self.service_discovery.stop()
                    self.service_discovery = None

                # 握手事件
                self._handshake_event = threading.Event()

                # 启动视频接收（独立进程）
                self.video_receiver = VideoReceiverProcess(
                    self.video_port,
                    server_addr=(self.server_ip, self.video_port)
                )
                self.video_receiver.start()

                # 启动控制发送
                self.control_sender = ControlSender()
                self.control_sender.on_param_response = self._on_param_response
                self.control_sender.on_ready_changed = self._on_ready_changed
                self.control_sender.start(self.server_ip, self.control_port)

                # 启动心跳
                self.heartbeat = HeartbeatManager()
                self.heartbeat.on_connection_lost = self._on_heartbeat_timeout
                self.heartbeat.on_connection_restored = self._on_heartbeat_restored
                self.heartbeat.on_first_ack = self._on_handshake_ok
                self.heartbeat.start(self.server_ip, self.control_port)

            except Exception as e:
                logger.error(f"Connection error: {e}")
                self._set_state(SessionState.IDLE)
                if self.on_error:
                    self.on_error(str(e))
                return

        # 在锁外等待握手（最多 10 秒）
        if not self._handshake_event.wait(timeout=10.0):
            logger.warning("Handshake timeout — no ACK from server")
            self.disconnect()
        else:
            # 检查是否是被 disconnect 唤醒的（而非真正握手成功）
            if self.state == SessionState.CONNECTED:
                logger.info(f"Connected to {self.server_ip}:{self.control_port}")

    def _on_handshake_ok(self):
        """首次心跳 ACK 收到 — 握手成功，查询机载端参数"""
        with self._lock:
            if self.state == SessionState.CONNECTING:
                self._set_state(SessionState.CONNECTED)
        self._handshake_event.set()
        if self.control_sender:
            self.control_sender.send_param_query()

    def _on_heartbeat_timeout(self):
        """心跳超时回调 — 断开组件并启动自动重连"""
        logger.warning("Heartbeat timeout, starting reconnect")
        with self._lock:
            self._disconnect_internal()
            self._set_state(SessionState.DISCONNECTED)
        self._start_reconnect()

    def _on_heartbeat_restored(self):
        """心跳恢复回调"""
        logger.info("Heartbeat restored")
        self._stop_reconnect()

    def _start_reconnect(self):
        """启动自动重连"""
        with self._lock:
            if self._reconnect_enabled:
                return

            self._reconnect_enabled = True
            self._set_state(SessionState.RECONNECTING)

            self._reconnect_thread = threading.Thread(target=self._reconnect_loop, daemon=True)
            self._reconnect_thread.start()

    def _stop_reconnect(self):
        """停止自动重连"""
        with self._lock:
            self._reconnect_enabled = False

    def _reconnect_loop(self):
        """重连循环 — 同步尝试连接，避免 event 竞态"""
        attempt = 0
        try:
            while self._reconnect_enabled and attempt < self._max_reconnect_attempts:
                attempt += 1
                logger.info(f"Reconnect attempt {attempt}/{self._max_reconnect_attempts} in {self._reconnect_interval}s...")
                time.sleep(self._reconnect_interval)

                if not self._reconnect_enabled:
                    break

                if not self._last_service_info:
                    logger.warning("No saved service info, cannot reconnect")
                    break

                logger.info(f"Reconnecting to {self._last_service_name}...")
                if self._try_connect_sync():
                    logger.info("Reconnect successful")
                    self._reconnect_enabled = False
                    return

            if self._reconnect_enabled:
                logger.warning(f"Reconnect failed after {attempt} attempts")

            with self._lock:
                self._reconnect_enabled = False
                if self.state != SessionState.CONNECTED:
                    self._set_state(SessionState.IDLE)

        except Exception as e:
            logger.error(f"Reconnect loop error: {e}")

    def _try_connect_sync(self) -> bool:
        """同步尝试连接一次，返回是否成功"""
        # 从保存的服务信息重新解析地址
        addresses = self._last_service_info.get('addresses', [])
        port = self._last_service_info.get('port', 0)
        properties = self._last_service_info.get('properties', {})
        if not addresses or not port:
            return False

        with self._lock:
            self._disconnect_internal()

            self.server_ip = addresses[0]
            self.control_port = port
            video_port_str = properties.get('video_port', '')
            self.video_port = int(video_port_str) if video_port_str else port - Config.VIDEO_PORT_OFFSET

            self._set_state(SessionState.CONNECTING)
            self._handshake_event = threading.Event()

            try:
                self.video_receiver = VideoReceiverProcess(
                    self.video_port,
                    server_addr=(self.server_ip, self.video_port)
                )
                self.video_receiver.start()

                self.control_sender = ControlSender()
                self.control_sender.on_param_response = self._on_param_response
                self.control_sender.on_ready_changed = self._on_ready_changed
                self.control_sender.start(self.server_ip, self.control_port)

                self.heartbeat = HeartbeatManager()
                self.heartbeat.on_first_ack = self._on_handshake_ok
                # 重连期间不注册 on_connection_lost，防止循环触发
                self.heartbeat.start(self.server_ip, self.control_port)

            except Exception as e:
                logger.error(f"Reconnect connect error: {e}")
                self._disconnect_internal()
                return False

        if not self._handshake_event.wait(timeout=10.0):
            logger.warning("Reconnect handshake timeout")
            with self._lock:
                self._disconnect_internal()
            return False

        # 握手成功，注册心跳回调
        with self._lock:
            if self.heartbeat:
                self.heartbeat.on_connection_lost = self._on_heartbeat_timeout
                self.heartbeat.on_connection_restored = self._on_heartbeat_restored
        return self.state == SessionState.CONNECTED

    def disconnect(self):
        """断开连接"""
        with self._lock:
            self._stop_reconnect()
            self._disconnect_internal()
            self._set_state(SessionState.IDLE)

    def send_param_update(self, params: dict):
        """发送参数修改到机载端"""
        if self.control_sender and self.state == SessionState.CONNECTED:
            self.control_sender.send_param_update(params)

    def _on_param_response(self, params: dict):
        """机载端参数响应 — 转发给上层"""
        logger.info(f"Param response from air unit: {params}")
        if self.on_param_response:
            self.on_param_response(params)

    def _on_ready_changed(self, is_ready: bool):
        """READY 状态变化 — 转发给上层"""
        logger.info(f"Ready state: {is_ready}")
        if self.on_ready_changed:
            self.on_ready_changed(is_ready)

    def _disconnect_internal(self):
        """内部断开连接"""
        if self.service_discovery:
            self.service_discovery.stop()
            self.service_discovery = None

        if self.video_receiver:
            self.video_receiver.stop()
            self.video_receiver = None

        if self.control_sender:
            self.control_sender.stop()
            self.control_sender = None

        if self.heartbeat:
            self.heartbeat.on_connection_lost = None
            self.heartbeat.on_connection_restored = None
            self.heartbeat.on_first_ack = None
            self.heartbeat.stop()
            self.heartbeat = None

        # 唤醒可能在等待握手的线程
        self._handshake_event.set()

        self.server_ip = ""
        self.control_port = 0
        self.video_port = 0

        logger.info("Disconnected")

    def _set_state(self, new_state: SessionState):
        """设置状态"""
        if self.state != new_state:
            self.state = new_state
            logger.info(f"State: {new_state.value}")
            if self.on_state_changed:
                self.on_state_changed(new_state)

    def get_statistics(self) -> dict:
        """获取统计信息"""
        stats = {
            "state": self.state.value,
            "server_ip": self.server_ip,
            "server_port": self.control_port,
            "control_port": self.control_port,
            "video_port": self.video_port,
        }

        if self.video_receiver:
            stats.update(self.video_receiver.get_statistics())

        if self.control_sender:
            ctrl_stats = self.control_sender.get_statistics()
            # rtt_avg 从秒转毫秒，None 转 0.0
            rtt_sec = ctrl_stats.pop("rtt_avg", None)
            ctrl_stats["rtt_avg"] = (rtt_sec * 1000.0) if rtt_sec else 0.0
            ctrl_stats["packet_loss_rate"] = self.control_sender.get_recent_loss(1.0)
            stats.update(ctrl_stats)

        if self.heartbeat:
            stats.update(self.heartbeat.get_statistics())

        return stats
