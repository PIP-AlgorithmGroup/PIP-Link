"""主应用"""

import ctypes
import ctypes.wintypes
import os
import time as _time
from typing import Optional

# DPI awareness — must be set BEFORE pygame.init() / any SDL call
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)  # PROCESS_PER_MONITOR_DPI_AWARE
except Exception:
    try:
        ctypes.windll.user32.SetProcessDPIAware()
    except Exception:
        pass

import pygame
from pygame.locals import DOUBLEBUF, OPENGL
from OpenGL.GL import *
import imgui
from imgui.integrations.pygame import PygameRenderer

import builtins
import numpy as np

from config import Config
from core.window_manager import WindowManager
from core.recorder import Recorder
from network.session import SessionManager, SessionState
from ui.renderer import VideoRenderer
from ui.imgui_ui import ImGuiUI
from ui.input_handler import InputHandler
from ui.console import GameConsole
from logic.param_manager import ParamManager
from logic.status_monitor import StatusMonitor
from logic.config_manager import ConfigManager
from logic.audit_logger import AuditLogger


class Application:
    """主应用 — 协调所有子系统"""

    # stream_* → 机载端参数名映射
    _STREAM_PARAM_MAP = {
        "stream_encoder":       ("encoder",        lambda v: "h264" if v == 1 else "jpeg"),
        "stream_bitrate":       ("bitrate",         int),
        "stream_fps":           ("target_fps",      int),
        "stream_fec_enabled":   ("fec_enabled",     bool),
        "stream_fec_redundancy":("fec_redundancy",  float),
    }

    # 机载端参数名 → stream_* 反向映射
    _REMOTE_TO_STREAM = {
        "encoder":       ("stream_encoder",        lambda v: 1 if v == "h264" else 0),
        "bitrate":       ("stream_bitrate",         int),
        "target_fps":    ("stream_fps",             int),
        "fec_enabled":   ("stream_fec_enabled",     bool),
        "fec_redundancy":("stream_fec_redundancy",  float),
    }

    # 仅本地参数（不同步到机载端）
    _LOCAL_ONLY_PARAMS = {
        "key_bindings", "mouse_sensitivity", "fov", "invert_pitch",
        "recording_enabled", "recording_bitrate", "recording_format",
        "show_performance_graph", "show_debug_info", "window_mode",
        "fullscreen_display", "resolution",
    }

    def __init__(self):
        pygame.init()
        self._display_flags = DOUBLEBUF | OPENGL
        pygame.display.set_mode((Config.RENDER_WIDTH, Config.RENDER_HEIGHT), self._display_flags)
        pygame.display.set_caption("PIP-Link Ground Unit")

        # 设置窗口图标（优先 icon.png，fallback icon.ico）
        try:
            from config import _asset
            _icon_path = _asset("assets/icon.png")
            if not os.path.exists(_icon_path):
                _icon_path = _asset("assets/icon.ico")
            _icon = pygame.image.load(_icon_path)
            pygame.display.set_icon(_icon)
        except Exception as _e:
            print(f"[App] Icon load failed: {_e}")

        # OpenGL — 正交 2D 投影
        glClearColor(0.0, 0.0, 0.0, 1.0)
        glEnable(GL_TEXTURE_2D)
        glMatrixMode(GL_PROJECTION)
        glOrtho(-1, 1, -1, 1, -1, 1)
        glMatrixMode(GL_MODELVIEW)

        # ImGui — 字体加载必须在 renderer 初始化之前
        imgui.create_context()
        io = imgui.get_io()
        io.display_size = (Config.RENDER_WIDTH, Config.RENDER_HEIGHT)
        font_title = font_body = font_mono = None
        try:
            font_title = io.fonts.add_font_from_file_ttf(Config.FONT_PATH_BOLD, Config.FONT_SIZE_TITLE)
            font_body  = io.fonts.add_font_from_file_ttf(Config.FONT_PATH,      Config.FONT_SIZE_BODY)
            font_mono  = io.fonts.add_font_from_file_ttf(Config.FONT_PATH_MONO, Config.FONT_SIZE_MONO)
            print("[App] Loaded 3-tier fonts: title(Bold 22), body(16), mono(18)")
        except Exception as e:
            print(f"[App] Font loading error: {e}, using defaults")
        self.imgui_renderer = PygameRenderer()

        # 子系统
        self.session        = SessionManager()
        self.video_renderer = VideoRenderer(Config.RENDER_WIDTH, Config.RENDER_HEIGHT)
        self.video_renderer.init_texture()
        self.imgui_ui       = ImGuiUI(font_title=font_title, font_body=font_body, font_mono=font_mono)
        self.input_handler  = InputHandler()
        self.param_manager  = ParamManager()
        self.status_monitor = StatusMonitor()
        self.config_manager = ConfigManager("config.json")
        self.audit_logger   = AuditLogger(log_dir="logs")
        self.console        = GameConsole(font_mono=font_mono, font_body=font_body)
        self.window_manager = WindowManager(self._display_flags)
        self.recorder       = Recorder(self.param_manager, self.audit_logger)

        # 拦截 print → 同时输出到终端和开发者控制台
        self._original_print = builtins.print
        builtins.print = self._intercepted_print
        if font_mono:
            self.console.log("[App] Loaded 3-tier fonts: title(Bold 22), body(16), mono(18)", "system")

        # 应用状态
        self.running = True
        self.fps_clock = pygame.time.Clock()
        self._discovered_devices: list = []
        self._discovered_services_raw: dict = {}
        self._pending_window_mode: Optional[int] = None
        self._pending_resolution: Optional[int] = None

        pygame.mouse.set_visible(True)
        pygame.key.stop_text_input()

        # 绑定回调
        self.session.on_state_changed      = self._on_session_state_changed
        self.session.on_services_discovered = self._on_services_discovered
        self.session.on_param_response     = self._on_param_response
        self.session.on_ready_changed      = self._on_ready_changed
        self.input_handler.on_toggle_menu    = self._on_toggle_menu
        self.input_handler.on_toggle_console = self._on_toggle_console
        self.input_handler.on_toggle_hud     = self._on_toggle_hud
        self.input_handler.on_key_capture    = self._on_key_captured

        saved_bindings = self.config_manager.get_key_bindings()
        if saved_bindings:
            self.imgui_ui._key_bindings.update(saved_bindings)
            self.input_handler.set_bindings(saved_bindings)
            print("[App] Loaded key bindings from config")

    # -------------------------------------------------------------------------
    # Session 回调
    # -------------------------------------------------------------------------

    def _on_session_state_changed(self, state: SessionState) -> None:
        print(f"[App] Session state: {state.value}")
        if state == SessionState.CONNECTED:
            self.audit_logger.log("connect", f"Session connected: {state.value}")
        elif state in (SessionState.IDLE, SessionState.DISCONNECTED):
            self.audit_logger.log("disconnect", f"Session state: {state.value}")
            self.video_renderer.frame_data = None
            self._force_not_ready()

    def _on_param_response(self, params: dict) -> None:
        """同步机载端参数，跳过本地专属参数"""
        synced = 0
        for key, value in params.items():
            if key not in self._LOCAL_ONLY_PARAMS:
                if key in self._REMOTE_TO_STREAM:
                    stream_key, transform = self._REMOTE_TO_STREAM[key]
                    self.param_manager.set_param(stream_key, transform(value))
                self.param_manager.set_param(key, value)
                synced += 1
        if synced:
            print(f"[App] Synced {synced} params from air unit")

    def _on_ready_changed(self, is_ready: bool) -> None:
        self.imgui_ui.is_ready = is_ready
        self.input_handler.set_mouse_locked(is_ready)
        print(f"[App] Ready: {is_ready}")

    def _force_not_ready(self) -> None:
        if self.session.control_sender:
            self.session.control_sender.set_ready(False)
        else:
            self.imgui_ui.is_ready = False
            self.input_handler.set_mouse_locked(False)

    # -------------------------------------------------------------------------
    # 服务发现回调
    # -------------------------------------------------------------------------

    def _on_services_discovered(self, services: dict) -> None:
        """增量更新已发现设备列表"""
        svc_suffix = "._pip-link._udp.local."
        for name, info in services.items():
            short = name[:-len(svc_suffix)] if name.endswith(svc_suffix) else name
            if not any(d["name"] == short for d in self._discovered_devices):
                addrs = info.get('addresses', [])
                ip = addrs[0] if addrs else '0.0.0.0'
                port = info.get('port', 0)
                self._discovered_devices.append({"name": short, "ip": ip, "port": port, "selected": False})
                self._discovered_services_raw[short] = info
                print(f"[App] Discovered device: {short} ({ip}:{port})")

    def _on_select_device(self, idx: int) -> None:
        if idx < 0 or idx >= len(self._discovered_devices):
            return
        device = self._discovered_devices[idx]
        service_info = self._discovered_services_raw.get(device["name"], {})
        if service_info:
            print(f"[App] Connecting to: {device['name']} ({device['ip']}:{device['port']})")
            self.session.connect_to_service(device["name"], service_info)

    def _on_connect_by_name(self, device_name: str) -> None:
        if not device_name:
            print("[App] No device name entered")
            return
        for device in self._discovered_devices:
            if device["name"] == device_name:
                service_info = self._discovered_services_raw.get(device["name"], {})
                if service_info:
                    print(f"[App] Connecting to: {device['name']} ({device['ip']}:{device['port']})")
                    self.session.connect_to_service(device["name"], service_info)
                    return
        print(f"[App] Resolving device: {device_name} ...")
        import threading
        threading.Thread(target=self._resolve_and_connect, args=(device_name,), daemon=True).start()

    def _resolve_and_connect(self, device_name: str) -> None:
        """后台线程：mDNS resolve 后直接连接"""
        from zeroconf import Zeroconf
        import socket as _socket
        svc_type = "_pip-link._udp.local."
        zc = None
        try:
            zc = Zeroconf()
            info = zc.get_service_info(svc_type, f"{device_name}.{svc_type}", timeout=5000)
            if info and info.addresses and info.port:
                service_data = {
                    'name': device_name,
                    'addresses': [_socket.inet_ntoa(a) for a in info.addresses],
                    'port': info.port,
                    'properties': {
                        (k.decode() if isinstance(k, bytes) else k): (v.decode() if isinstance(v, bytes) else str(v))
                        for k, v in (info.properties or {}).items()
                    },
                }
                ip, port = service_data['addresses'][0], service_data['port']
                print(f"[App] Resolved {device_name}: {ip}:{port}")
                if not any(d["name"] == device_name for d in self._discovered_devices):
                    self._discovered_devices.append({"name": device_name, "ip": ip, "port": port, "selected": False})
                self._discovered_services_raw[device_name] = service_data
                self.session.connect_to_service(device_name, service_data)
            else:
                print(f"[App] Device '{device_name}' not found on network")
        except Exception as e:
            print(f"[App] Resolve failed: {e}")
        finally:
            if zc:
                zc.close()

    # -------------------------------------------------------------------------
    # 输入回调
    # -------------------------------------------------------------------------

    def _intercepted_print(self, *args, **kwargs) -> None:
        self._original_print(*args, **kwargs)
        self.console.log(" ".join(str(a) for a in args))

    def _on_toggle_console(self) -> None:
        self.console.toggle()

    def _on_toggle_hud(self) -> None:
        self.imgui_ui.show_hud = not self.imgui_ui.show_hud

    def _on_toggle_menu(self) -> None:
        self.imgui_ui.show_menu = not self.imgui_ui.show_menu
        if self.imgui_ui.show_menu and self.imgui_ui.is_ready:
            self._force_not_ready()
        print(f"[App] Menu toggled: {self.imgui_ui.show_menu}")

    def _on_key_captured(self, pygame_key: int, key_name: str) -> None:
        self.imgui_ui.on_key_captured(pygame_key, key_name)
        self.input_handler.set_bindings(self.imgui_ui._key_bindings)

    # -------------------------------------------------------------------------
    # 参数变更
    # -------------------------------------------------------------------------

    def _on_param_change(self, key: str, value) -> None:
        self.param_manager.set_param(key, value)

        if key == "key_bindings":
            self.config_manager.config["key_bindings"] = value
            self.config_manager.save()
            self.input_handler.set_bindings(value)
        elif key in {"mouse_sensitivity", "fov", "invert_pitch", "video_quality", "recording_enabled"}:
            self.config_manager.save()

        if key not in {"key_bindings", "show_performance_graph", "show_debug_info"}:
            self.audit_logger.log("param_change", f"{key}={value}")

        if key == "window_mode":
            self._pending_window_mode = value
            return
        if key == "fullscreen_display":
            if self.window_manager.current_window_mode != 0:
                self._pending_window_mode = self.window_manager.current_window_mode
            return
        if key == "resolution":
            self._pending_resolution = value
            return

        if key in self._STREAM_PARAM_MAP:
            remote_key, transform = self._STREAM_PARAM_MAP[key]
            self.session.send_param_update({remote_key: transform(value)})
            return

        if key not in self._LOCAL_ONLY_PARAMS:
            self.session.send_param_update({key: value})

    # -------------------------------------------------------------------------
    # 主循环
    # -------------------------------------------------------------------------

    def run(self) -> None:
        print("[App] Starting application")

        while self.running:
            # 帧开头应用延迟的窗口/分辨率切换
            if self._pending_window_mode is not None:
                preferred = self.param_manager.get_param("fullscreen_display") or -1
                self.window_manager.apply_window_mode(self._pending_window_mode, preferred)
                self.imgui_renderer = self.window_manager.reinit_gl(self.video_renderer, self.imgui_renderer)
                self._pending_window_mode = None
            if self._pending_resolution is not None:
                self.window_manager.apply_resolution(self._pending_resolution, self.param_manager.resolutions)
                self.imgui_renderer = self.window_manager.reinit_gl(self.video_renderer, self.imgui_renderer)
                self._pending_resolution = None

            self.running = self.input_handler.handle_events(self.imgui_renderer)

            if not pygame.mouse.get_focused() and self.imgui_ui.is_ready:
                self._force_not_ready()

            # 非菜单模式：转发鼠标输入到控制发送器
            if not self.imgui_ui.show_menu:
                dx, dy = self.input_handler.get_mouse_delta()
                buttons = self.input_handler.get_mouse_buttons()
                scroll = self.input_handler.get_scroll()
                side = self.input_handler.mouse_side_buttons
                btn_mask = (
                    (1  if buttons[0] else 0) |
                    (2  if buttons[1] else 0) |
                    (4  if buttons[2] else 0) |
                    (8  if side[0]    else 0) |
                    (16 if side[1]    else 0)
                )
                if self.session.control_sender:
                    self.session.control_sender.update_mouse(dx, dy, btn_mask, scroll)

            # 获取最新视频帧
            if self.session.video_receiver:
                frame = self.session.video_receiver.get_latest_frame()
                if frame is not None:
                    try:
                        if isinstance(frame, np.ndarray) and frame.shape == (Config.RENDER_HEIGHT, Config.RENDER_WIDTH, 3):
                            self.video_renderer.update_frame(frame)
                            self.status_monitor.tick_frame()
                    except Exception:
                        pass

            # 更新状态统计
            stats = self.session.get_statistics()
            stats["session_state"] = self.session.state.value
            stats["discovered_devices"] = self._discovered_devices
            self.status_monitor.bandwidth_kbps = self.imgui_ui._bandwidth_kbps
            self.status_monitor.update(stats)
            status = self.status_monitor.get_status()
            self.imgui_ui.update_perf_history(status.get("fps", 0.0), status.get("latency_ms", 0.0))

            # 渲染
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)
            self.video_renderer.render()

            imgui.new_frame()

            if self.video_renderer.frame_data is None:
                self.imgui_ui.draw_no_signal()

            if self.imgui_ui.show_menu or self.imgui_ui.menu_alpha > 0.01:
                self.imgui_ui.draw_menu(
                    self.session.state.value,
                    callbacks={
                        "connect":               lambda: self.session.start_discovery(Config.MDNS_SERVICE_NAME),
                        "connect_by_name":       self._on_connect_by_name,
                        "disconnect":            self.session.disconnect,
                        "scan_devices":          lambda: self.session.start_discovery(Config.MDNS_SERVICE_NAME),
                        "select_device":         self._on_select_device,
                        "quit":                  lambda: setattr(self, "running", False),
                        "start_key_capture":     self.input_handler.start_key_capture,
                        "start_recording":       self.recorder.start_recording,
                        "stop_recording":        self.recorder.stop_recording,
                        "screenshot":            self.recorder.request_screenshot,
                        "open_recordings_folder":self.recorder.open_save_folder,
                        "get_history":           self.status_monitor.get_history,
                        "audit_logger":          self.audit_logger,
                    },
                    params=self.param_manager.get_all_params(),
                    on_param_change=self._on_param_change,
                    stats=stats,
                    live_status=status,
                    console_height=self.console._anim_h,
                )

            if not self.imgui_ui.show_menu:
                self.imgui_ui.draw_status_bar(status)
                kb_state = b'\x00' * 10
                if self.session.control_sender and self.imgui_ui.is_ready:
                    kb_state = self.session.control_sender.keyboard.get_state()
                mdx, mdy = self.input_handler.get_mouse_delta()
                self.imgui_ui.draw_input_hud(
                    kb_state, mdx, mdy,
                    self.input_handler.get_mouse_buttons(),
                    self.input_handler.get_scroll(),
                    self.input_handler.mouse_side_buttons,
                )

            self.imgui_ui.draw_ready_indicator()
            self.console.draw()

            imgui.render()
            self.imgui_renderer.render(imgui.get_draw_data())

            # 按需启用 SDL 文本输入（仅 ImGui 需要时）
            io = imgui.get_io()
            if io.want_text_input:
                pygame.key.start_text_input()
            else:
                pygame.key.stop_text_input()

            # 录制 / 截图（在 flip 之前捕获帧）
            if self.recorder.is_recording or self.recorder.pending_screenshot:
                self.recorder.process_frame(_time.monotonic())

            pygame.display.flip()
            self.fps_clock.tick(Config.TARGET_FPS)

        self.session.disconnect()
        self.video_renderer.cleanup()
        pygame.quit()
        print("[App] Application exited")

    # -------------------------------------------------------------------------
    # 公开 API
    # -------------------------------------------------------------------------

    def connect(self, service_name: str) -> None:
        """启动 mDNS 发现并连接"""
        self.session.start_discovery(service_name)

    def disconnect(self) -> None:
        """断开当前会话"""
        self.session.disconnect()


if __name__ == "__main__":
    app = Application()
    app.run()
