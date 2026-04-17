"""心跳管理"""

import threading
import socket
import time
from typing import Optional, Callable
from config import Config


class HeartbeatManager:
    """心跳管理"""

    def __init__(self):
        self.socket: Optional[socket.socket] = None
        self.remote_addr: Optional[tuple] = None
        self.is_running = False

        # 回调
        self.on_error: Optional[Callable] = None

        # 统计
        self.heartbeats_sent = 0

    def start(self, server_ip: str, server_port: int):
        """启动心跳"""
        if self.is_running:
            return

        self.remote_addr = (server_ip, server_port)
        self.is_running = True

        thread = threading.Thread(target=self._heartbeat_thread, daemon=True)
        thread.start()
        print(f"[HeartbeatManager] 启动 ({server_ip}:{server_port})")

    def stop(self):
        """停止心跳"""
        self.is_running = False
        if self.socket:
            try:
                self.socket.close()
            except:
                pass
        print("[HeartbeatManager] 已停止")

    def _heartbeat_thread(self):
        """心跳线程"""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

            while self.is_running:
                try:
                    self._send_heartbeat()
                    time.sleep(Config.HEARTBEAT_INTERVAL)
                except Exception as e:
                    if self.is_running:
                        print(f"[HeartbeatManager] 发送错误: {e}")
                        if self.on_error:
                            self.on_error(str(e))

        except Exception as e:
            print(f"[HeartbeatManager] 线程错误: {e}")
            if self.on_error:
                self.on_error(str(e))

    def _send_heartbeat(self):
        """发送心跳"""
        if not self.socket or not self.remote_addr:
            return

        packet = b"HEARTBEAT"
        self.socket.sendto(packet, self.remote_addr)
        self.heartbeats_sent += 1

    def get_statistics(self) -> dict:
        """获取统计"""
        return {
            "heartbeats_sent": self.heartbeats_sent,
        }
