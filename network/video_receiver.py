"""视频接收线程"""

import threading
import socket
import queue
from typing import Optional, Callable
from config import Config


class VideoReceiver:
    """视频接收线程"""

    def __init__(self, port: int):
        self.port = port
        self.socket: Optional[socket.socket] = None
        self.is_running = False
        self.render_queue = queue.Queue(maxsize=Config.RENDER_QUEUE_MAX_SIZE)

        # 回调
        self.on_frame_received: Optional[Callable] = None
        self.on_error: Optional[Callable] = None

        # 统计
        self.frames_received = 0
        self.bytes_received = 0

    def start(self):
        """启动接收"""
        if self.is_running:
            return

        self.is_running = True
        thread = threading.Thread(target=self._rx_thread, daemon=True)
        thread.start()
        print(f"[VideoReceiver] 启动 (端口: {self.port})")

    def stop(self):
        """停止接收"""
        self.is_running = False
        if self.socket:
            try:
                self.socket.close()
            except:
                pass
        print("[VideoReceiver] 已停止")

    def _rx_thread(self):
        """接收线程"""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 2 * 1024 * 1024)
            self.socket.bind(("0.0.0.0", self.port))
            self.socket.settimeout(1.0)

            print(f"[VideoReceiver] UDP 绑定: 0.0.0.0:{self.port}")

            while self.is_running:
                try:
                    data, addr = self.socket.recvfrom(Config.UDP_BUFFER_SIZE)
                    self.bytes_received += len(data)
                    self.frames_received += 1

                    # 放入渲染队列
                    try:
                        self.render_queue.put_nowait(data)
                    except queue.Full:
                        pass  # 丢弃最旧的帧

                    if self.on_frame_received:
                        self.on_frame_received(data, addr)

                except socket.timeout:
                    continue
                except Exception as e:
                    if self.is_running:
                        print(f"[VideoReceiver] 接收错误: {e}")
                        if self.on_error:
                            self.on_error(str(e))

        except Exception as e:
            print(f"[VideoReceiver] 线程错误: {e}")
            if self.on_error:
                self.on_error(str(e))

    def get_latest_frame(self):
        """获取最新帧"""
        try:
            return self.render_queue.get_nowait()
        except queue.Empty:
            return None

    def get_statistics(self) -> dict:
        """获取统计"""
        return {
            "frames_received": self.frames_received,
            "bytes_received": self.bytes_received,
        }
