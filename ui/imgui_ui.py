"""ImGui UI components - CS2 inspired"""

import imgui
import time
from typing import Optional, Callable, Dict
from ui.theme import Theme
from config import Config


class ImGuiUI:
    """ImGui UI manager with animations"""

    def __init__(self):
        self.show_menu = False
        self.menu_alpha = 0.0  # For fade-in animation
        self.menu_scale = 0.95  # For scale animation
        self.menu_open_time = 0.0
        self.animation_duration = 0.3  # seconds
        Theme.apply(imgui)

    def _update_menu_animation(self):
        """Update menu open/close animation"""
        current_time = time.time()
        elapsed = current_time - self.menu_open_time
        progress = min(elapsed / self.animation_duration, 1.0)

        if self.show_menu:
            # Opening animation
            self.menu_alpha = progress
            self.menu_scale = 0.95 + (progress * 0.05)
        else:
            # Closing animation
            self.menu_alpha = 1.0 - progress
            self.menu_scale = 1.0 - (progress * 0.05)

    def draw_menu(self, session_state: str, callbacks: Dict[str, Callable], params: Dict, on_param_change: Optional[Callable] = None) -> None:
        """Draw tabbed menu with animations

        Args:
            session_state: Session state string
            callbacks: Callback dict {"connect": fn, "disconnect": fn, "quit": fn}
            params: Parameters dict
            on_param_change: Callback for parameter changes
        """
        if not self.show_menu and self.menu_alpha <= 0.01:
            return

        self._update_menu_animation()

        # Apply animation
        imgui.push_style_var(imgui.STYLE_ALPHA, self.menu_alpha)

        # Center menu on screen
        menu_width = 700
        menu_height = 550
        center_x = (Config.RENDER_WIDTH - menu_width) / 2
        center_y = (Config.RENDER_HEIGHT - menu_height) / 2

        imgui.set_next_window_position(center_x, center_y, imgui.ALWAYS)
        imgui.set_next_window_size(menu_width, menu_height, imgui.ALWAYS)

        # No title bar, no close button, no move/resize
        expanded, opened = imgui.begin("REMOTE CONTROL", False, imgui.WINDOW_NO_TITLE_BAR | imgui.WINDOW_NO_MOVE | imgui.WINDOW_NO_RESIZE)

        if expanded:
            # Tab bar with better styling
            if imgui.begin_tab_bar("MenuTabs", imgui.TAB_BAR_FITTING_POLICY_SCROLL):
                # Connection tab
                if imgui.begin_tab_item("CONNECTION")[0]:
                    imgui.spacing()
                    self._draw_connection_tab(session_state, callbacks)
                    imgui.end_tab_item()

                # Parameters tab
                if imgui.begin_tab_item("PARAMETERS")[0]:
                    imgui.spacing()
                    self._draw_parameters_tab(params, on_param_change)
                    imgui.end_tab_item()

                # Video tab
                if imgui.begin_tab_item("VIDEO")[0]:
                    imgui.spacing()
                    self._draw_video_tab(params, on_param_change)
                    imgui.end_tab_item()

                # Recording tab
                if imgui.begin_tab_item("RECORDING")[0]:
                    imgui.spacing()
                    self._draw_recording_tab(params, on_param_change)
                    imgui.end_tab_item()

                # Debug tab
                if imgui.begin_tab_item("DEBUG")[0]:
                    imgui.spacing()
                    self._draw_debug_tab(params, on_param_change)
                    imgui.end_tab_item()

                imgui.end_tab_bar()

        imgui.end()
        imgui.pop_style_var()

    def _draw_connection_tab(self, session_state: str, callbacks: Dict[str, Callable]) -> None:
        """Draw connection tab"""
        imgui.text("CONNECTION STATUS")
        imgui.separator()
        imgui.spacing()

        # State indicator
        self._draw_state_indicator(session_state)
        imgui.same_line(100)
        imgui.text(f"{session_state.upper()}")

        imgui.spacing()
        imgui.spacing()

        # Buttons
        if imgui.button("CONNECT", width=150, height=40):
            if "connect" in callbacks:
                callbacks["connect"]()

        imgui.same_line(170)

        if imgui.button("DISCONNECT", width=150, height=40):
            if "disconnect" in callbacks:
                callbacks["disconnect"]()

        imgui.spacing()
        imgui.spacing()

        if imgui.button("QUIT", width=320, height=40):
            if "quit" in callbacks:
                callbacks["quit"]()

    def _draw_parameters_tab(self, params: Dict, on_change: Optional[Callable]) -> None:
        """Draw parameters tab"""
        imgui.text("INPUT SETTINGS")
        imgui.separator()
        imgui.spacing()

        # Mouse sensitivity
        sensitivity = params.get("mouse_sensitivity", 1.0)
        changed, new_value = imgui.input_float("Mouse Sensitivity##sens", sensitivity, step=0.1, step_fast=1.0)
        if changed and on_change:
            on_change("mouse_sensitivity", max(0.1, min(5.0, new_value)))

        # FOV
        fov = params.get("fov", 90.0)
        changed, new_value = imgui.input_float("FOV##fov", fov, step=1.0, step_fast=10.0)
        if changed and on_change:
            on_change("fov", max(30.0, min(120.0, new_value)))

        # Invert pitch
        invert_pitch = params.get("invert_pitch", False)
        changed, new_value = imgui.checkbox("Invert Pitch", invert_pitch)
        if changed and on_change:
            on_change("invert_pitch", new_value)

    def _draw_video_tab(self, params: Dict, on_change: Optional[Callable]) -> None:
        """Draw video tab"""
        imgui.text("VIDEO SETTINGS")
        imgui.separator()
        imgui.spacing()

        # Video quality
        quality = params.get("video_quality", 1)
        changed, new_value = imgui.combo("Quality", quality, ["LOW", "MEDIUM", "HIGH", "ULTRA"])
        if changed and on_change:
            on_change("video_quality", new_value)

        # Resolution
        resolution = params.get("resolution", 1)
        changed, new_value = imgui.combo("Resolution", resolution, ["1280x720", "1920x1080", "2560x1440", "3840x2160"])
        if changed and on_change:
            on_change("resolution", new_value)

        # Window mode
        window_mode = params.get("window_mode", 0)
        changed, new_value = imgui.combo("Window Mode", window_mode, ["WINDOWED", "FULLSCREEN"])
        if changed and on_change:
            on_change("window_mode", new_value)

    def _draw_recording_tab(self, params: Dict, on_change: Optional[Callable]) -> None:
        """Draw recording tab"""
        imgui.text("RECORDING SETTINGS")
        imgui.separator()
        imgui.spacing()

        # Recording enabled
        recording_enabled = params.get("recording_enabled", False)
        changed, new_value = imgui.checkbox("Enable Recording", recording_enabled)
        if changed and on_change:
            on_change("recording_enabled", new_value)

        # Recording bitrate
        bitrate = params.get("recording_bitrate", 5000)
        changed, new_value = imgui.input_int("Bitrate (kbps)##bitrate", bitrate, step=500, step_fast=2000)
        if changed and on_change:
            on_change("recording_bitrate", max(1000, min(20000, new_value)))

    def _draw_debug_tab(self, params: Dict, on_change: Optional[Callable]) -> None:
        """Draw debug tab"""
        imgui.text("DEBUG SETTINGS")
        imgui.separator()
        imgui.spacing()

        # Performance graph
        show_perf = params.get("show_performance_graph", False)
        changed, new_value = imgui.checkbox("Show Performance Graph", show_perf)
        if changed and on_change:
            on_change("show_performance_graph", new_value)

        # Debug info
        show_debug = params.get("show_debug_info", False)
        changed, new_value = imgui.checkbox("Show Debug Info", show_debug)
        if changed and on_change:
            on_change("show_debug_info", new_value)

    def draw_status_bar(self, status: Dict) -> None:
        """Draw status bar

        Args:
            status: Status dict {"fps", "latency_ms", "packet_loss_rate", "frames_received", "session_state"}
        """
        # Bottom-right position
        imgui.set_next_window_position(Config.RENDER_WIDTH - 420, Config.RENDER_HEIGHT - 200, imgui.ALWAYS)
        imgui.set_next_window_size(400, 180, imgui.ALWAYS)

        expanded, opened = imgui.begin("STATUS", True)
        if expanded:
            fps = status.get("fps", 0.0)
            latency = status.get("latency_ms", 0.0)
            loss = status.get("packet_loss_rate", 0.0)
            frames = status.get("frames_received", 0)

            imgui.text(f"FPS: {fps:.1f}")
            imgui.text(f"Latency: {latency:.1f}ms")
            imgui.text(f"Loss: {loss:.2%}")
            imgui.text(f"Frames: {frames}")

        imgui.end()

    def _draw_state_indicator(self, state: str) -> None:
        """Draw colored state indicator

        Args:
            state: State string
        """
        color = Theme.STATE_COLORS.get(state, Theme.STATE_COLORS["idle"])
        imgui.text_colored("●", *color)
