"""会话管理 - 连接生命周期管理"""

import threading
import time
import logging
from enum import Enum
from typing import Optional, Callable
from network.service_discovery import ServiceDiscovery
from network.video_receiver import VideoReceiver
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
        self.video_receiver: Optional[VideoReceiver] = None
        self.control_sender: Optional[ControlSender] = None
        self.heartbeat: Optional[HeartbeatManager] = None

        # 服务器信息
        self.server_ip: str = ""
        self.control_port: int = 0
        self.video_port: int = 0

        # 回调
        self.on_state_changed: Optional[Callable] = None
        self.on_error: Optional[Callable] = None

        # 自动重连
        self._reconnect_thread: Optional[threading.Thread] = None
        self._reconnect_enabled = False
        self._reconnect_interval = 5.0

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
        """服务发现线程"""
        try:
            self.service_discovery = ServiceDiscovery()
            self.service_discovery.on_service_found = self._on_service_found
            self.service_discovery.on_service_lost = self._on_service_lost
            self.service_discovery.start()

            logger.info(f"Waiting for service: {service_name}")
            service_info = self.service_discovery.wait_for_service(service_name, timeout=10.0)

            if service_info:
                self._on_service_found(service_name, service_info)
            else:
                logger.error(f"Service discovery timeout: {service_name}")
                with self._lock:
                    self._set_state(SessionState.IDLE)
                if self.on_error:
                    self.on_error("Service discovery timeout")

        except Exception as e:
            logger.error(f"Discovery thread error: {e}")
            with self._lock:
                self._set_state(SessionState.IDLE)
            if self.on_error:
                self.on_error(str(e))

    def _on_service_found(self, service_name: str, service_info: dict):
        """服务发现回调"""
        try:
            # 解析服务信息
            addresses = service_info.get('addresses', [])
            port = service_info.get('port', 0)
            properties = service_info.get('properties', {})

            logger.debug(f"Service info: addresses={addresses}, port={port}, properties={properties}")

            if not addresses or not port:
                logger.error("Invalid service info")
                return

            # 处理地址（可能是字节或字符串）
            addr = addresses[0]
            if isinstance(addr, bytes):
                import socket
                self.server_ip = socket.inet_ntoa(addr)
            else:
                self.server_ip = str(addr)

            self.control_port = port
            self.video_port = port + 1000  # 视频端口偏移

            logger.info(f"Service found: {self.server_ip}:{self.control_port} (video: {self.video_port})")
            self._connect_to_server()

        except Exception as e:
            logger.error(f"Service found error: {e}")
            self._set_state(SessionState.IDLE)
            if self.on_error:
                self.on_error(str(e))

    def _on_service_lost(self, service_name: str):
        """服务丢失回调"""
        logger.warning(f"Service lost: {service_name}")
        self._start_reconnect()

    def _connect_to_server(self):
        """连接到服务器"""
        with self._lock:
            try:
                self._set_state(SessionState.CONNECTING)

                # 停止 mDNS 发现
                if self.service_discovery:
                    self.service_discovery.stop()
                    self.service_discovery = None

                # 启动视频接收
                self.video_receiver = VideoReceiver(self.video_port)
                self.video_receiver.start()

                # 启动控制发送
                self.control_sender = ControlSender()
                self.control_sender.start(self.server_ip, self.control_port)

                # 启动心跳
                self.heartbeat = HeartbeatManager()
                self.heartbeat.on_connection_lost = self._on_heartbeat_timeout
                self.heartbeat.on_connection_restored = self._on_heartbeat_restored
                self.heartbeat.start(self.server_ip, self.control_port)

                self._set_state(SessionState.CONNECTED)
                logger.info(f"Connected to {self.server_ip}:{self.control_port}")

            except Exception as e:
                logger.error(f"Connection error: {e}")
                self._set_state(SessionState.IDLE)
                if self.on_error:
                    self.on_error(str(e))

    def _on_heartbeat_timeout(self):
        """心跳超时回调"""
        logger.warning("Heartbeat timeout, starting reconnect")
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
        """重连循环"""
        try:
            while self._reconnect_enabled:
                logger.info(f"Reconnecting in {self._reconnect_interval}s...")
                time.sleep(self._reconnect_interval)

                if not self._reconnect_enabled:
                    break

                # 断开现有连接
                self._disconnect_internal()

                # 重新发现服务
                logger.info("Rediscovering service...")
                self.start_discovery("air_unit")

        except Exception as e:
            logger.error(f"Reconnect loop error: {e}")

    def disconnect(self):
        """断开连接"""
        with self._lock:
            self._stop_reconnect()
            self._disconnect_internal()
            self._set_state(SessionState.IDLE)

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
            self.heartbeat.stop()
            self.heartbeat = None

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
            "control_port": self.control_port,
            "video_port": self.video_port,
        }

        if self.video_receiver:
            stats.update(self.video_receiver.get_statistics())

        if self.control_sender:
            stats.update(self.control_sender.get_statistics())

        if self.heartbeat:
            stats.update(self.heartbeat.get_statistics())

        return stats
