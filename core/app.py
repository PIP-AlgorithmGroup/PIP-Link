"""主应用"""

import ctypes
import ctypes.wintypes

# DPI awareness — must be set BEFORE pygame.init() / any SDL call
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)  # PROCESS_PER_MONITOR_DPI_AWARE
except Exception:
    try:
        ctypes.windll.user32.SetProcessDPIAware()
    except Exception:
        pass

import pygame
from pygame.locals import *
from OpenGL.GL import *
from OpenGL.GLU import *
import imgui
from imgui.integrations.pygame import PygameRenderer

import builtins

import numpy as np

from config import Config
from network.session import SessionManager, SessionState
from ui.renderer import VideoRenderer
from ui.imgui_ui import ImGuiUI
from ui.input_handler import InputHandler
from ui.console import GameConsole
from logic.param_manager import ParamManager
from logic.status_monitor import StatusMonitor
from logic.config_manager import ConfigManager


class Application:
    """主应用"""

    def __init__(self):
        # 初始化 Pygame
        pygame.init()
        pygame.display.set_mode((Config.RENDER_WIDTH, Config.RENDER_HEIGHT), DOUBLEBUF | OPENGL)
        pygame.display.set_caption("PIP-Link Ground Unit")
        self._display_flags = DOUBLEBUF | OPENGL

        # 初始化 OpenGL - 正交投影（2D 全屏）
        glClearColor(0.0, 0.0, 0.0, 1.0)
        glEnable(GL_TEXTURE_2D)
        glMatrixMode(GL_PROJECTION)
        glOrtho(-1, 1, -1, 1, -1, 1)
        glMatrixMode(GL_MODELVIEW)

        # Initialize ImGui
        imgui.create_context()
        io = imgui.get_io()
        io.display_size = (Config.RENDER_WIDTH, Config.RENDER_HEIGHT)

        # Load three-tier font system BEFORE renderer initialization
        font_title = None
        font_body = None
        font_mono = None
        try:
            font_title = io.fonts.add_font_from_file_ttf(Config.FONT_PATH_BOLD, Config.FONT_SIZE_TITLE)
            font_body = io.fonts.add_font_from_file_ttf(Config.FONT_PATH, Config.FONT_SIZE_BODY)
            font_mono = io.fonts.add_font_from_file_ttf(Config.FONT_PATH_MONO, Config.FONT_SIZE_MONO)
            print("[App] Loaded 3-tier fonts: title(Bold 22), body(16), mono(18)")
        except Exception as e:
            print(f"[App] Font loading error: {e}, using defaults")

        # Initialize renderer AFTER font loading
        self.imgui_renderer = PygameRenderer()

        # Components
        self.session = SessionManager()
        self.video_renderer = VideoRenderer(Config.RENDER_WIDTH, Config.RENDER_HEIGHT)
        self.video_renderer.init_texture()
        self.imgui_ui = ImGuiUI(font_title=font_title, font_body=font_body, font_mono=font_mono)
        self.input_handler = InputHandler()
        self.param_manager = ParamManager()
        self.status_monitor = StatusMonitor()
        self.config_manager = ConfigManager("config.json")

        # Developer console (overlay)
        self.console = GameConsole(font_mono=font_mono, font_body=font_body)

        # Install print interceptor — tee to both terminal and console
        self._original_print = builtins.print
        builtins.print = self._intercepted_print

        # Replay early boot messages (printed before interceptor was installed)
        if font_mono:
            self.console.log("[App] Loaded 3-tier fonts: title(Bold 22), body(16), mono(18)", "system")

        # State
        self.running = True
        self.fps_clock = pygame.time.Clock()
        self._discovered_devices = []  # 发现的设备列表
        self._discovered_services_raw = {}  # 原始服务数据
        self._pending_window_mode = None  # 延迟到帧开头执行
        self._pending_resolution = None
        self._current_window_mode = 0

        # Always show cursor
        pygame.mouse.set_visible(True)

        # 禁用输入法 — SDL_StopTextInput 阻止 IME 拦截按键
        pygame.key.stop_text_input()

        # Menu starts closed, animation state already initialized in ImGuiUI.__init__

        # Set callbacks
        self.session.on_state_changed = self._on_session_state_changed
        self.session.on_services_discovered = self._on_services_discovered
        self.session.on_param_response = self._on_param_response
        self.session.on_ready_changed = self._on_ready_changed
        self.input_handler.on_toggle_menu = self._on_toggle_menu
        self.input_handler.on_toggle_console = self._on_toggle_console
        self.input_handler.on_toggle_hud = self._on_toggle_hud
        self.input_handler.on_key_capture = self._on_key_captured

        # Load persisted key bindings
        saved_bindings = self.config_manager.get_key_bindings()
        if saved_bindings:
            self.imgui_ui._key_bindings.update(saved_bindings)
            self.input_handler.set_bindings(saved_bindings)
            print("[App] Loaded key bindings from config")

    def _on_session_state_changed(self, state):
        """Session state changed callback"""
        print(f"[App] Session state: {state.value}")
        if state in (SessionState.IDLE, SessionState.DISCONNECTED):
            self.video_renderer.frame_data = None
            self._force_not_ready()

    # stream_* → air unit param name mapping
    _STREAM_PARAM_MAP = {
        "stream_encoder": ("encoder", lambda v: "h264" if v == 1 else "jpeg"),
        "stream_bitrate": ("bitrate", int),
        "stream_fps": ("target_fps", int),
        "stream_fec_enabled": ("fec_enabled", bool),
        "stream_fec_redundancy": ("fec_redundancy", float),
    }

    # air unit → stream_* reverse mapping
    _REMOTE_TO_STREAM = {
        "encoder": ("stream_encoder", lambda v: 1 if v == "h264" else 0),
        "bitrate": ("stream_bitrate", int),
        "target_fps": ("stream_fps", int),
        "fec_enabled": ("stream_fec_enabled", bool),
        "fec_redundancy": ("stream_fec_redundancy", float),
    }

    def _on_param_response(self, params: dict):
        """机载端参数同步回调 — 仅更新远端参数，跳过客户端本地参数"""
        local_keys = {"resolution", "window_mode", "fullscreen_display",
                       "mouse_sensitivity", "fov", "invert_pitch",
                       "key_bindings", "recording_enabled", "recording_bitrate",
                       "recording_format", "show_performance_graph", "show_debug_info",
                       "video_quality"}
        synced = 0
        for key, value in params.items():
            if key not in local_keys:
                # Reverse-map air unit params to stream_* for UI display
                if key in self._REMOTE_TO_STREAM:
                    stream_key, transform = self._REMOTE_TO_STREAM[key]
                    self.param_manager.set_param(stream_key, transform(value))
                self.param_manager.set_param(key, value)
                synced += 1
        if synced:
            print(f"[App] Synced {synced} params from air unit")

    def _on_ready_changed(self, is_ready: bool):
        """READY 状态变化回调 — 同步 UI + 鼠标锁定"""
        self.imgui_ui.is_ready = is_ready
        self.input_handler.set_mouse_locked(is_ready)
        print(f"[App] Ready: {is_ready}")

    def _force_not_ready(self):
        """强制回 NOT READY（断连、失焦等）"""
        if self.session.control_sender:
            self.session.control_sender.set_ready(False)
        else:
            self.imgui_ui.is_ready = False
            self.input_handler.set_mouse_locked(False)

    def _on_services_discovered(self, services: dict):
        """服务发现回调 — 增量更新（搜到一个就显示一个）"""
        svc_suffix = "._pip-link._udp.local."
        for name, info in services.items():
            short = name[:-len(svc_suffix)] if name.endswith(svc_suffix) else name
            if not any(d["name"] == short for d in self._discovered_devices):
                addrs = info.get('addresses', [])
                ip = addrs[0] if addrs else '0.0.0.0'
                port = info.get('port', 0)
                self._discovered_devices.append({
                    "name": short,
                    "ip": ip,
                    "port": port,
                    "selected": False,
                })
                self._discovered_services_raw[short] = info
                print(f"[App] Discovered device: {short} ({ip}:{port})")

    def _on_select_device(self, idx: int):
        """用户选择设备回调"""
        if idx < 0 or idx >= len(self._discovered_devices):
            return
        device = self._discovered_devices[idx]
        service_name = device["name"]
        service_info = self._discovered_services_raw.get(service_name, {})
        if service_info:
            print(f"[App] Connecting to: {device['name']} ({device['ip']}:{device['port']})")
            self.session.connect_to_service(service_name, service_info)

    def _on_connect_by_name(self, device_name: str):
        """手动输入设备名后直接连接（mDNS resolve）"""
        if not device_name:
            print("[App] No device name entered")
            return

        # 先查已发现列表
        for device in self._discovered_devices:
            if device["name"] == device_name:
                service_info = self._discovered_services_raw.get(device["name"], {})
                if service_info:
                    print(f"[App] Connecting to: {device['name']} ({device['ip']}:{device['port']})")
                    self.session.connect_to_service(device["name"], service_info)
                    return

        # 未在列表中 — 直接 mDNS resolve
        print(f"[App] Resolving device: {device_name} ...")
        import threading
        threading.Thread(
            target=self._resolve_and_connect,
            args=(device_name,),
            daemon=True,
        ).start()

    def _intercepted_print(self, *args, **kwargs):
        """拦截 print，同时输出到终端和控制台"""
        text = " ".join(str(a) for a in args)
        self._original_print(*args, **kwargs)
        self.console.log(text)

    def _on_toggle_console(self):
        """Toggle developer console"""
        self.console.toggle()

    def _on_toggle_hud(self):
        """Toggle input HUD visibility"""
        self.imgui_ui.show_hud = not self.imgui_ui.show_hud

    def _on_toggle_menu(self):
        """Toggle menu — 打开菜单时自动回 NOT READY"""
        self.imgui_ui.show_menu = not self.imgui_ui.show_menu
        if self.imgui_ui.show_menu and self.imgui_ui.is_ready:
            self._force_not_ready()
        print(f"[App] Menu toggled: {self.imgui_ui.show_menu}")

    def _on_key_captured(self, pygame_key: int, key_name: str):
        """Key captured during rebinding — update UI label + InputHandler binding"""
        self.imgui_ui.on_key_captured(pygame_key, key_name)
        action = self.imgui_ui._rebinding_action  # None after on_key_captured
        # Find which action was just rebound by checking what changed
        bindings = self.imgui_ui._key_bindings
        self.input_handler.set_bindings(bindings)

    def _resolve_and_connect(self, device_name: str):
        """后台线程：直接 mDNS resolve 设备名并连接"""
        from zeroconf import Zeroconf, ServiceInfo
        import socket as _socket

        svc_type = "_pip-link._udp.local."
        full_name = f"{device_name}.{svc_type}"
        zc = None
        try:
            zc = Zeroconf()
            info = zc.get_service_info(svc_type, full_name, timeout=5000)
            if info and info.addresses and info.port:
                service_data = {
                    'name': device_name,
                    'addresses': [_socket.inet_ntoa(a) for a in info.addresses],
                    'port': info.port,
                    'properties': {},
                }
                if info.properties:
                    for k, v in info.properties.items():
                        key = k.decode() if isinstance(k, bytes) else k
                        val = v.decode() if isinstance(v, bytes) else str(v)
                        service_data['properties'][key] = val

                ip = service_data['addresses'][0]
                port = service_data['port']
                print(f"[App] Resolved {device_name}: {ip}:{port}")

                # 加入已发现列表
                if not any(d["name"] == device_name for d in self._discovered_devices):
                    self._discovered_devices.append({
                        "name": device_name, "ip": ip, "port": port, "selected": False,
                    })
                self._discovered_services_raw[device_name] = service_data
                self.session.connect_to_service(device_name, service_data)
            else:
                print(f"[App] Device '{device_name}' not found on network")
        except Exception as e:
            print(f"[App] Resolve failed: {e}")
        finally:
            if zc:
                zc.close()

    def _on_param_change(self, key: str, value):
        """Handle parameter change and persist to config"""
        self.param_manager.set_param(key, value)
        # Persist key bindings and other important settings
        if key == "key_bindings":
            self.config_manager.config["key_bindings"] = value
            self.config_manager.save()
            self.input_handler.set_bindings(value)
        elif key in ["mouse_sensitivity", "fov", "invert_pitch", "video_quality", "recording_enabled"]:
            self.config_manager.save()

        # Fullscreen toggle — deferred to next frame start
        if key == "window_mode":
            self._pending_window_mode = value
            return
        if key == "fullscreen_display":
            if self._current_window_mode != 0:
                self._pending_window_mode = self._current_window_mode
            return

        # Resolution change — deferred to next frame start
        if key == "resolution":
            self._pending_resolution = value
            return

        # Send stream_* params to air unit with name translation
        if key in self._STREAM_PARAM_MAP:
            remote_key, transform = self._STREAM_PARAM_MAP[key]
            self.session.send_param_update({remote_key: transform(value)})
            return

        # Send remote params to air unit
        local_only_keys = {"key_bindings", "mouse_sensitivity", "fov", "invert_pitch",
                           "recording_enabled", "recording_bitrate", "recording_format",
                           "show_performance_graph", "show_debug_info", "window_mode",
                           "fullscreen_display", "resolution"}
        if key not in local_only_keys:
            self.session.send_param_update({key: value})

    def _apply_resolution(self, res_index: int):
        """切换窗口分辨率 — 用 set_mode 重建 surface + GL 上下文"""
        res = self.param_manager.resolutions.get(res_index)
        if not res:
            return
        w, h = res[0], res[1]
        if self._current_window_mode == 1:
            return
        try:
            pygame.display.set_mode((w, h), self._display_flags)
            pygame.display.set_caption("PIP-Link Ground Unit")
            pygame.event.pump()
            pygame.event.get()

            self._full_gl_reinit()

            new_w, new_h = pygame.display.get_window_size()
            print(f"[App] Resolution: {new_w}x{new_h}")
        except Exception as e:
            print(f"[App] Resolution change failed: {e}")

    @staticmethod
    def _enum_monitors() -> list:
        """Win32 EnumDisplayMonitors — 返回真实物理像素坐标和 DPI"""
        MONITORINFOF_PRIMARY = 0x00000001

        class MONITORINFOEX(ctypes.Structure):
            _fields_ = [
                ("cbSize", ctypes.wintypes.DWORD),
                ("rcMonitor", ctypes.wintypes.RECT),
                ("rcWork", ctypes.wintypes.RECT),
                ("dwFlags", ctypes.wintypes.DWORD),
                ("szDevice", ctypes.c_wchar * 32),
            ]

        results = []

        def _cb(hMon, _hdc, _lprc, _data):
            info = MONITORINFOEX()
            info.cbSize = ctypes.sizeof(MONITORINFOEX)
            ctypes.windll.user32.GetMonitorInfoW(hMon, ctypes.byref(info))
            m = info.rcMonitor
            results.append({
                "x": m.left, "y": m.top,
                "w": m.right - m.left, "h": m.bottom - m.top,
                "primary": bool(info.dwFlags & MONITORINFOF_PRIMARY),
            })
            return True

        ENUMPROC = ctypes.WINFUNCTYPE(
            ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p,
            ctypes.POINTER(ctypes.wintypes.RECT), ctypes.c_long,
        )
        ctypes.windll.user32.EnumDisplayMonitors(None, None, ENUMPROC(_cb), 0)
        results.sort(key=lambda m: (not m["primary"], m["x"], m["y"]))
        return results

    def _get_current_display(self) -> int:
        """获取当前窗口所在的显示器索引"""
        try:
            monitors = self._enum_monitors()
            if len(monitors) <= 1:
                return 0
            hwnd = pygame.display.get_wm_info()["window"]
            rect = ctypes.wintypes.RECT()
            ctypes.windll.user32.GetWindowRect(hwnd, ctypes.byref(rect))
            cx = (rect.left + rect.right) // 2
            cy = (rect.top + rect.bottom) // 2
            for i, m in enumerate(monitors):
                if m["x"] <= cx < m["x"] + m["w"] and m["y"] <= cy < m["y"] + m["h"]:
                    return i
            return 0
        except Exception:
            return 0

    def _get_target_display(self) -> int:
        """获取目标显示器：-1 表示当前窗口所在屏幕"""
        idx = self.param_manager.get_param("fullscreen_display")
        if idx is None or idx < 0:
            return self._get_current_display()
        monitors = self._enum_monitors()
        return min(idx, len(monitors) - 1)

    def _apply_window_mode(self, mode: int):
        """切换窗口模式 — set_mode 重建 surface，Win32 API 定位"""
        try:
            display_idx = self._get_target_display()
            monitors = self._enum_monitors()

            if mode == 1:
                mon = monitors[display_idx] if display_idx < len(monitors) else monitors[0]
                dw, dh = mon["w"], mon["h"]
                mx, my = mon["x"], mon["y"]

                pygame.display.set_mode((dw, dh), self._display_flags | NOFRAME)
                pygame.event.pump()

                hwnd = pygame.display.get_wm_info()["window"]
                SWP_NOZORDER = 0x0004
                SWP_FRAMECHANGED = 0x0020
                ctypes.windll.user32.SetWindowPos(
                    hwnd, 0, mx, my, dw, dh, SWP_NOZORDER | SWP_FRAMECHANGED,
                )
            else:
                pygame.display.set_mode((Config.RENDER_WIDTH, Config.RENDER_HEIGHT), self._display_flags)
                pygame.event.pump()
                # 居中到当前所在显示器
                cur = self._get_current_display()
                mon = monitors[cur] if cur < len(monitors) else monitors[0]
                cx = mon["x"] + (mon["w"] - Config.RENDER_WIDTH) // 2
                cy = mon["y"] + (mon["h"] - Config.RENDER_HEIGHT) // 2
                hwnd = pygame.display.get_wm_info()["window"]
                SWP_NOZORDER = 0x0004
                ctypes.windll.user32.SetWindowPos(hwnd, 0, cx, cy, 0, 0, SWP_NOZORDER | 0x0001)  # SWP_NOSIZE

            pygame.display.set_caption("PIP-Link Ground Unit")
            pygame.event.pump()
            pygame.event.get()

            self._current_window_mode = mode
            self._full_gl_reinit()

            mode_names = {0: "Windowed", 1: "Fullscreen"}
            print(f"[App] Window mode: {mode_names.get(mode, mode)} (display {display_idx})")
        except Exception as e:
            print(f"[App] Window mode switch failed: {e}")

    def _reinit_after_mode_switch(self):
        """模式切换后刷新 viewport / 投影矩阵（GL 上下文未销毁）"""
        w, h = pygame.display.get_window_size()
        glViewport(0, 0, w, h)
        glMatrixMode(GL_PROJECTION)
        glLoadIdentity()
        glOrtho(-1, 1, -1, 1, -1, 1)
        glMatrixMode(GL_MODELVIEW)
        glLoadIdentity()
        io = imgui.get_io()
        io.display_size = (w, h)

    def _full_gl_reinit(self):
        """set_mode 重建 surface 后完整重建 GL 状态 + ImGui renderer"""
        glClearColor(0.0, 0.0, 0.0, 1.0)
        glEnable(GL_TEXTURE_2D)
        glMatrixMode(GL_PROJECTION)
        glLoadIdentity()
        glOrtho(-1, 1, -1, 1, -1, 1)
        glMatrixMode(GL_MODELVIEW)
        glLoadIdentity()

        self.video_renderer.init_texture()

        w, h = pygame.display.get_window_size()
        glViewport(0, 0, w, h)
        io = imgui.get_io()
        io.display_size = (w, h)

        try:
            if hasattr(self.imgui_renderer, 'shutdown'):
                self.imgui_renderer.shutdown()
        except Exception:
            pass
        self.imgui_renderer = PygameRenderer()

    def run(self):
        """Run application"""
        print("[App] Starting application")

        while self.running:
            # 0. Apply deferred window mode / resolution switch (before any rendering)
            if self._pending_window_mode is not None:
                self._apply_window_mode(self._pending_window_mode)
                self._pending_window_mode = None
            if self._pending_resolution is not None:
                self._apply_resolution(self._pending_resolution)
                self._pending_resolution = None

            # 1. Handle input (pass imgui_renderer for event forwarding)
            self.running = self.input_handler.handle_events(self.imgui_renderer)

            # 1.5 失焦检测 — 窗口失去焦点时自动回 NOT READY
            if not pygame.mouse.get_focused() and self.imgui_ui.is_ready:
                self._force_not_ready()

            # 2. Non-menu mode: handle control input
            if not self.imgui_ui.show_menu:
                dx, dy = self.input_handler.get_mouse_delta()
                buttons = self.input_handler.get_mouse_buttons()
                sensitivity = self.param_manager.get_param("mouse_sensitivity")

            # 3. Get latest frame and decode
            if self.session.video_receiver:
                frame = self.session.video_receiver.get_latest_frame()
                if frame is not None:
                    try:
                        if isinstance(frame, np.ndarray) and frame.shape == (Config.RENDER_HEIGHT, Config.RENDER_WIDTH, 3):
                            self.video_renderer.update_frame(frame)
                            self.status_monitor.tick_frame()
                    except Exception:
                        pass

            # 4. Update status
            stats = self.session.get_statistics()
            stats["session_state"] = self.session.state.value
            stats["discovered_devices"] = self._discovered_devices
            self.status_monitor.update(stats)
            status = self.status_monitor.get_status()

            self.imgui_ui.update_perf_history(
                status.get("fps", 0.0),
                status.get("latency_ms", 0.0),
            )

            # 5. Clear framebuffer
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)

            # 6. Render video
            self.video_renderer.render()

            # 7. ImGui UI
            imgui.new_frame()

            # No signal overlay when no video frame
            if self.video_renderer.frame_data is None:
                self.imgui_ui.draw_no_signal()

            # Draw menu when open OR while fade-out animation is playing
            if self.imgui_ui.show_menu or self.imgui_ui.menu_alpha > 0.01:
                self.imgui_ui.draw_menu(
                    self.session.state.value,
                    callbacks={
                        "connect": lambda: self.session.start_discovery(Config.MDNS_SERVICE_NAME),
                        "connect_by_name": self._on_connect_by_name,
                        "disconnect": self.session.disconnect,
                        "scan_devices": lambda: self.session.start_discovery(Config.MDNS_SERVICE_NAME),
                        "select_device": self._on_select_device,
                        "quit": lambda: setattr(self, "running", False),
                        "start_key_capture": self.input_handler.start_key_capture,
                    },
                    params=self.param_manager.get_all_params(),
                    on_param_change=self._on_param_change,
                    stats=stats,
                    live_status=status,
                )
            if not self.imgui_ui.show_menu:
                self.imgui_ui.draw_status_bar(status)
                # Input HUD
                kb_state = b'\x00' * 10
                if self.session.control_sender and self.imgui_ui.is_ready:
                    kb_state = self.session.control_sender.keyboard.get_state()
                mdx, mdy = self.input_handler.get_mouse_delta()
                mbtns = self.input_handler.get_mouse_buttons()
                scroll = self.input_handler.get_scroll()
                side = self.input_handler.mouse_side_buttons
                self.imgui_ui.draw_input_hud(kb_state, mdx, mdy, mbtns,
                                             scroll, side)
            self.imgui_ui.draw_ready_indicator()

            self.console.draw()

            imgui.render()
            self.imgui_renderer.render(imgui.get_draw_data())

            # 动态切换 SDL text input — 仅在 imgui 需要文本输入时启用
            io = imgui.get_io()
            if io.want_text_input:
                pygame.key.start_text_input()
            else:
                pygame.key.stop_text_input()

            # 8. Swap buffers
            pygame.display.flip()

            # 9. Frame rate control
            self.fps_clock.tick(Config.TARGET_FPS)

        self.session.disconnect()
        self.video_renderer.cleanup()
        pygame.quit()
        print("[App] Application exited")

    def connect(self, service_name: str):
        """Connect"""
        self.session.start_discovery(service_name)

    def disconnect(self):
        """Disconnect"""
        self.session.disconnect()


if __name__ == "__main__":
    app = Application()
    app.run()
