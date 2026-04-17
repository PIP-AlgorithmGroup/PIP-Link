"""会话管理 - 连接生命周期管理"""

import threading
import time
from enum import Enum
from typing import Optional, Callable
from network.service_discovery import ServiceDiscovery
from network.video_receiver import VideoReceiver
from network.control_sender import ControlSender
from network.heartbeat import HeartbeatManager


class SessionState(Enum):
    """会话状态"""
    IDLE = "idle"
    DISCOVERING = "discovering"
    CONNECTING = "connecting"
    CONNECTED = "connected"
    DISCONNECTED = "disconnected"
    RECONNECTING = "reconnecting"


class SessionManager:
    """会话管理器"""

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
        self.server_port: int = 0

        # 回调
        self.on_state_changed: Optional[Callable] = None
        self.on_error: Optional[Callable] = None

    def start_discovery(self, service_name: str):
        """启动 mDNS 发现"""
        with self._lock:
            if self.state != SessionState.IDLE:
                return

            self._set_state(SessionState.DISCOVERING)

            self.service_discovery = ServiceDiscovery()
            self.service_discovery.on_service_found = self._on_service_found
            self.service_discovery.on_error = self._on_discovery_error
            self.service_discovery.start(service_name)

    def _on_service_found(self, ip: str, port: int):
        """服务发现回调"""
        print(f"[SessionManager] 发现服务: {ip}:{port}")
        self.server_ip = ip
        self.server_port = port
        self._connect_to_server()

    def _on_discovery_error(self, error: str):
        """发现错误回调"""
        print(f"[SessionManager] 发现错误: {error}")
        self._set_state(SessionState.IDLE)
        if self.on_error:
            self.on_error(error)

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
                self.video_receiver = VideoReceiver(self.server_port)
                self.video_receiver.start()

                # 启动控制发送
                self.control_sender = ControlSender()
                self.control_sender.start(self.server_ip, self.server_port)

                # 启动心跳
                self.heartbeat = HeartbeatManager()
                self.heartbeat.start(self.server_ip, self.server_port)

                self._set_state(SessionState.CONNECTED)
                print(f"[SessionManager] 已连接到 {self.server_ip}:{self.server_port}")

            except Exception as e:
                print(f"[SessionManager] 连接错误: {e}")
                self._set_state(SessionState.IDLE)
                if self.on_error:
                    self.on_error(str(e))

    def disconnect(self):
        """断开连接"""
        with self._lock:
            self._set_state(SessionState.DISCONNECTED)

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

            self._set_state(SessionState.IDLE)
            print("[SessionManager] 已断开连接")

    def _set_state(self, new_state: SessionState):
        """设置状态"""
        if self.state != new_state:
            self.state = new_state
            print(f"[SessionManager] 状态: {new_state.value}")
            if self.on_state_changed:
                self.on_state_changed(new_state)

    def get_statistics(self) -> dict:
        """获取统计信息"""
        stats = {
            "state": self.state.value,
            "server_ip": self.server_ip,
            "server_port": self.server_port,
        }

        if self.video_receiver:
            stats.update(self.video_receiver.get_statistics())

        if self.control_sender:
            stats.update(self.control_sender.get_statistics())

        if self.heartbeat:
            stats.update(self.heartbeat.get_statistics())

        return stats
