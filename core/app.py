"""主应用"""

import pygame
from pygame.locals import *
from OpenGL.GL import *
from OpenGL.GLU import *
import imgui
from imgui.integrations.pygame import PygameRenderer
import time

import builtins

from config import Config
from network.session import SessionManager
from ui.renderer import VideoRenderer
from ui.imgui_ui import ImGuiUI
from ui.input_handler import InputHandler
from ui.console import GameConsole
from logic.input_mapper import InputMapper
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
        self.input_mapper = InputMapper()
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

        # Always show cursor
        pygame.mouse.set_visible(True)

        # Menu starts closed, animation state already initialized in ImGuiUI.__init__

        # Set callbacks
        self.session.on_state_changed = self._on_session_state_changed
        self.input_handler.on_toggle_menu = self._on_toggle_menu
        self.input_handler.on_toggle_console = self._on_toggle_console
        self.input_handler.on_key_capture = self.imgui_ui.on_key_captured

        # Load persisted key bindings
        saved_bindings = self.config_manager.get_key_bindings()
        if saved_bindings:
            self.imgui_ui._key_bindings.update(saved_bindings)
            print("[App] Loaded key bindings from config")

    def _on_session_state_changed(self, state):
        """Session state changed callback"""
        print(f"[App] Session state: {state.value}")

    def _intercepted_print(self, *args, **kwargs):
        """拦截 print，同时输出到终端和控制台"""
        text = " ".join(str(a) for a in args)
        self._original_print(*args, **kwargs)
        self.console.log(text)

    def _on_toggle_console(self):
        """Toggle developer console"""
        self.console.toggle()

    def _on_toggle_menu(self):
        """Toggle menu"""
        self.imgui_ui.show_menu = not self.imgui_ui.show_menu
        print(f"[App] Menu toggled: {self.imgui_ui.show_menu}")

    def _on_param_change(self, key: str, value):
        """Handle parameter change and persist to config"""
        self.param_manager.set_param(key, value)
        # Persist key bindings and other important settings
        if key == "key_bindings":
            self.config_manager.config["key_bindings"] = value
            self.config_manager.save()
        elif key in ["mouse_sensitivity", "fov", "invert_pitch", "video_quality", "recording_enabled"]:
            self.config_manager.save()

    def run(self):
        """Run application"""
        print("[App] Starting application")

        while self.running:
            # 1. Handle input (pass imgui_renderer for event forwarding)
            self.running = self.input_handler.handle_events(self.imgui_renderer)

            # 2. Non-menu mode: handle control input
            if not self.imgui_ui.show_menu:
                dx, dy = self.input_handler.get_mouse_delta()
                buttons = self.input_handler.get_mouse_buttons()
                sensitivity = self.param_manager.get_param("mouse_sensitivity")
                # input_mapper interface reserved, control sending handled by ControlSender
                # mouse_cmd = self.input_mapper.map_mouse_to_command(dx, dy, buttons, sensitivity)

            # 3. Get latest frame and decode
            if self.session.video_receiver:
                raw = self.session.video_receiver.get_latest_frame()
                if raw:
                    # Assume raw is numpy array, otherwise needs decoding
                    self.video_renderer.update_frame(raw)
                    self.status_monitor.tick_frame()

            # 4. Update status
            stats = self.session.get_statistics()
            stats["session_state"] = self.session.state.value
            self.status_monitor.update(stats)
            status = self.status_monitor.get_status()

            # Feed performance history for DEBUG tab graphs
            self.imgui_ui.update_perf_history(
                status.get("fps", 0.0),
                status.get("latency_ms", 0.0),
            )

            # 5. Clear framebuffer once at start
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
                        "connect": lambda svc=None: self.session.start_discovery(svc or Config.MDNS_SERVICE_NAME),
                        "disconnect": self.session.disconnect,
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

            # Developer console overlay (renders on top of everything)
            self.console.draw()

            imgui.render()
            self.imgui_renderer.render(imgui.get_draw_data())

            # 8. Swap buffers
            pygame.display.flip()

            # 8. Frame rate control
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
