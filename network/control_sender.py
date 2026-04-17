"""控制发送线程"""

import threading
import socket
import time
from typing import Optional, Callable
from config import Config
from network.keyboard_encoder import KeyboardEncoder


class ControlSender:
    """控制发送线程"""

    def __init__(self):
        self.socket: Optional[socket.socket] = None
        self.remote_addr: Optional[tuple] = None
        self.is_running = False

        # 键盘编码器
        self.keyboard = KeyboardEncoder()

        # 回调
        self.on_error: Optional[Callable] = None

        # 统计
        self.commands_sent = 0

    def start(self, server_ip: str, server_port: int):
        """启动发送"""
        if self.is_running:
            return

        self.remote_addr = (server_ip, server_port)
        self.is_running = True

        # 启动键盘监听
        self.keyboard.start()

        thread = threading.Thread(target=self._tx_thread, daemon=True)
        thread.start()
        print(f"[ControlSender] 启动 ({server_ip}:{server_port})")

    def stop(self):
        """停止发送"""
        self.is_running = False
        self.keyboard.stop()
        if self.socket:
            try:
                self.socket.close()
            except:
                pass
        print("[ControlSender] 已停止")

    def _tx_thread(self):
        """发送线程"""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

            interval = 1.0 / Config.TX_SEND_RATE
            last_send_time = time.time()

            while self.is_running:
                current_time = time.time()
                elapsed = current_time - last_send_time

                if elapsed >= interval:
                    self._send_control_command()
                    last_send_time = current_time

                time.sleep(0.001)

        except Exception as e:
            print(f"[ControlSender] 线程错误: {e}")
            if self.on_error:
                self.on_error(str(e))

    def _send_control_command(self):
        """发送控制指令"""
        try:
            if not self.socket or not self.remote_addr:
                return

            # 获取键盘状态
            keyboard_state = self.keyboard.get_state()

            # 简单格式: 10字节键盘状态
            self.socket.sendto(keyboard_state, self.remote_addr)
            self.commands_sent += 1

        except Exception as e:
            if self.is_running:
                print(f"[ControlSender] 发送错误: {e}")
                if self.on_error:
                    self.on_error(str(e))

    def get_statistics(self) -> dict:
        """获取统计"""
        return {
            "commands_sent": self.commands_sent,
        }
