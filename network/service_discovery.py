"""
mDNS 服务发现
"""

import socket
import threading
import json
import logging
from typing import Optional, Callable, Dict, Any
from zeroconf import ServiceBrowser, Zeroconf, ServiceStateChange


logger = logging.getLogger(__name__)


class ServiceDiscovery:
    """
    mDNS 服务发现

    用于发现局域网内的机载端服务
    """

    def __init__(self, service_type: str = "_pip-link._udp.local."):
        """
        初始化服务发现

        Args:
            service_type: mDNS 服务类型
        """
        self.service_type = service_type
        self.zeroconf: Optional[Zeroconf] = None
        self.browser: Optional[ServiceBrowser] = None
        self.discovered_services: Dict[str, Dict[str, Any]] = {}
        self.on_service_found: Optional[Callable[[str, Dict[str, Any]], None]] = None
        self.on_service_lost: Optional[Callable[[str], None]] = None

    def start(self):
        """启动服务发现"""
        try:
            self.zeroconf = Zeroconf()
            self.browser = ServiceBrowser(
                self.zeroconf,
                self.service_type,
                handlers=[self._on_service_state_change]
            )
            logger.info(f"mDNS 服务发现已启动，监听 {self.service_type}")
        except Exception as e:
            logger.error(f"启动 mDNS 服务发现失败: {e}")
            raise

    def stop(self):
        """停止服务发现"""
        try:
            if self.browser:
                self.browser.cancel()
            if self.zeroconf:
                self.zeroconf.close()
            logger.info("mDNS 服务发现已停止")
        except Exception as e:
            logger.error(f"停止 mDNS 服务发现失败: {e}")

    def _on_service_state_change(
        self,
        zeroconf: Zeroconf,
        service_type: str,
        name: str,
        state_change: ServiceStateChange
    ):
        """处理服务状态变化"""
        if state_change == ServiceStateChange.Added:
            self._on_service_added(zeroconf, service_type, name)
        elif state_change == ServiceStateChange.Removed:
            self._on_service_removed(name)

    def _on_service_added(self, zeroconf: Zeroconf, service_type: str, name: str):
        """处理服务添加"""
        try:
            info = zeroconf.get_service_info(service_type, name)
            if info:
                service_data = self._parse_service_info(info)
                self.discovered_services[name] = service_data

                logger.info(f"发现服务: {name}")
                logger.debug(f"服务信息: {service_data}")

                if self.on_service_found:
                    self.on_service_found(name, service_data)
        except Exception as e:
            logger.error(f"处理服务添加失败: {e}")

    def _on_service_removed(self, name: str):
        """处理服务移除"""
        if name in self.discovered_services:
            del self.discovered_services[name]
            logger.info(f"服务已移除: {name}")

            if self.on_service_lost:
                self.on_service_lost(name)

    def _parse_service_info(self, info) -> Dict[str, Any]:
        """解析服务信息"""
        service_data = {
            'name': info.name,
            'type': info.type,
            'addresses': [socket.inet_ntoa(addr) for addr in info.addresses],
            'port': info.port,
            'properties': {}
        }

        # 解析 TXT 记录
        if info.properties:
            for key, value in info.properties.items():
                if isinstance(value, bytes):
                    try:
                        service_data['properties'][key.decode()] = value.decode()
                    except:
                        service_data['properties'][key.decode()] = str(value)
                else:
                    service_data['properties'][key] = value

        return service_data

    def get_service(self, service_name: str) -> Optional[Dict[str, Any]]:
        """获取指定服务信息"""
        return self.discovered_services.get(service_name)

    def get_all_services(self) -> Dict[str, Dict[str, Any]]:
        """获取所有发现的服务"""
        return self.discovered_services.copy()

    def wait_for_service(
        self,
        service_name: str,
        timeout: float = 10.0
    ) -> Optional[Dict[str, Any]]:
        """
        等待服务被发现

        Args:
            service_name: 服务名称
            timeout: 超时时间（秒）

        Returns:
            服务信息或 None（超时）
        """
        import time

        start_time = time.time()
        while time.time() - start_time < timeout:
            if service_name in self.discovered_services:
                return self.discovered_services[service_name]
            time.sleep(0.1)

        logger.warning(f"等待服务 {service_name} 超时")
        return None


class ServiceDiscoveryThread(threading.Thread):
    """服务发现线程"""

    def __init__(
        self,
        service_type: str = "_pip_vision._udp.local.",
        on_service_found: Optional[Callable[[str, Dict[str, Any]], None]] = None,
        on_service_lost: Optional[Callable[[str], None]] = None
    ):
        """
        初始化服务发现线程

        Args:
            service_type: mDNS 服务类型
            on_service_found: 服务发现回调
            on_service_lost: 服务移除回调
        """
        super().__init__(daemon=True)
        self.discovery = ServiceDiscovery(service_type)
        self.discovery.on_service_found = on_service_found
        self.discovery.on_service_lost = on_service_lost
        self.is_running = False

    def run(self):
        """线程主循环"""
        try:
            self.discovery.start()
            self.is_running = True

            logger.info("服务发现线程已启动")

            # 保持线程运行
            while self.is_running:
                threading.Event().wait(1.0)

        except Exception as e:
            logger.error(f"服务发现线程异常: {e}")
        finally:
            self.is_running = False
            self.discovery.stop()
            logger.info("服务发现线程已停止")

    def stop(self):
        """停止线程"""
        self.is_running = False
        self.join(timeout=2.0)

    def get_service(self, service_name: str) -> Optional[Dict[str, Any]]:
        """获取服务信息"""
        return self.discovery.get_service(service_name)

    def get_all_services(self) -> Dict[str, Dict[str, Any]]:
        """获取所有服务"""
        return self.discovery.get_all_services()

    def wait_for_service(
        self,
        service_name: str,
        timeout: float = 10.0
    ) -> Optional[Dict[str, Any]]:
        """等待服务被发现"""
        return self.discovery.wait_for_service(service_name, timeout)
