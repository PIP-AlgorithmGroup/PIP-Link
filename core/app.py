"""主应用"""

import pygame
from pygame.locals import *
from OpenGL.GL import *
from OpenGL.GLU import *
import imgui
from imgui.integrations.pygame import PygameRenderer
import time

from config import Config
from network.session import SessionManager
from ui.renderer import VideoRenderer
from ui.imgui_ui import ImGuiUI
from ui.input_handler import InputHandler
from logic.input_mapper import InputMapper
from logic.param_manager import ParamManager
from logic.status_monitor import StatusMonitor


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

        # 初始化 ImGui
        imgui.create_context()
        self.imgui_renderer = PygameRenderer()
        io = imgui.get_io()
        io.display_size = (Config.RENDER_WIDTH, Config.RENDER_HEIGHT)

        # 加载中文字体
        try:
            io.fonts.add_font_from_file_ttf("C:\\Windows\\Fonts\\msyh.ttc", 16, io.fonts.get_glyph_ranges_chinese_simplified())
        except:
            pass  # 字体加载失败时使用默认字体

        # 组件
        self.session = SessionManager()
        self.video_renderer = VideoRenderer(Config.RENDER_WIDTH, Config.RENDER_HEIGHT)
        self.video_renderer.init_texture()
        self.imgui_ui = ImGuiUI()
        self.input_handler = InputHandler()
        self.input_mapper = InputMapper()
        self.param_manager = ParamManager()
        self.status_monitor = StatusMonitor()

        # 状态
        self.running = True
        self.fps_clock = pygame.time.Clock()

        # 设置回调
        self.session.on_state_changed = self._on_session_state_changed
        self.input_handler.on_toggle_menu = self._on_toggle_menu

    def _on_session_state_changed(self, state):
        """Session state changed callback"""
        print(f"[App] Session state: {state.value}")

    def _on_toggle_menu(self):
        """Toggle menu"""
        self.imgui_ui.show_menu = not self.imgui_ui.show_menu
        # Show mouse cursor when menu opens, hide when closes
        pygame.mouse.set_visible(self.imgui_ui.show_menu)

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

            # 5. Clear framebuffer once at start
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)

            # 6. Render video
            self.video_renderer.render()

            # 7. ImGui UI
            imgui.new_frame()
            if self.imgui_ui.show_menu:
                self.imgui_ui.draw_menu(
                    self.session.state.value,
                    callbacks={
                        "connect": lambda: self.session.start_discovery(Config.MDNS_SERVICE_NAME),
                        "disconnect": self.session.disconnect,
                        "quit": lambda: setattr(self, "running", False),
                    },
                    params=self.param_manager.get_all_params(),
                    on_param_change=self.param_manager.set_param
                )
            else:
                self.imgui_ui.draw_status_bar(status)

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
