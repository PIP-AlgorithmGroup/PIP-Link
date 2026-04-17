"""mDNS 服务发现"""

import threading
from typing import Optional, Callable
from zeroconf import ServiceBrowser, Zeroconf, ServiceStateChange
import socket


class ServiceDiscovery:
    """mDNS 服务发现"""

    def __init__(self):
        self.zeroconf: Optional[Zeroconf] = None
        self.browser: Optional[ServiceBrowser] = None
        self.is_running = False

        # 回调
        self.on_service_found: Optional[Callable] = None
        self.on_error: Optional[Callable] = None

    def start(self, service_name: str):
        """启动发现"""
        if self.is_running:
            return

        self.is_running = True
        thread = threading.Thread(target=self._discovery_thread, args=(service_name,), daemon=True)
        thread.start()
        print(f"[ServiceDiscovery] 启动 mDNS 发现: {service_name}")

    def stop(self):
        """停止发现"""
        self.is_running = False
        if self.browser:
            self.browser.cancel()
        if self.zeroconf:
            self.zeroconf.close()
        print("[ServiceDiscovery] 已停止")

    def _discovery_thread(self, service_name: str):
        """发现线程"""
        try:
            self.zeroconf = Zeroconf()
            self.browser = ServiceBrowser(
                self.zeroconf,
                service_name,
                handlers=[self._on_service_state_change]
            )

            while self.is_running:
                threading.Event().wait(0.1)

        except Exception as e:
            print(f"[ServiceDiscovery] 错误: {e}")
            if self.on_error:
                self.on_error(str(e))

    def _on_service_state_change(self, zeroconf, service_type, name, state_change):
        """服务状态变化"""
        if state_change == ServiceStateChange.Added:
            self._on_service_added(zeroconf, service_type, name)

    def _on_service_added(self, zeroconf, service_type, name):
        """服务发现"""
        try:
            info = zeroconf.get_service_info(service_type, name)
            if info and info.addresses:
                ip = socket.inet_ntoa(info.addresses[0])
                port = info.port
                if self.on_service_found:
                    self.on_service_found(ip, port)
        except Exception as e:
            print(f"[ServiceDiscovery] 解析错误: {e}")
