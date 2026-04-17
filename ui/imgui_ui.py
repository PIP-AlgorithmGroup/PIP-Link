"""ImGui UI components"""

import imgui
from typing import Optional, Callable, Dict
from ui.theme import Theme
from config import Config


class ImGuiUI:
    """ImGui UI manager"""

    def __init__(self):
        self.show_menu = False
        Theme.apply(imgui)

    def draw_menu(self, session_state: str, callbacks: Dict[str, Callable], params: Dict, on_param_change: Optional[Callable] = None) -> bool:
        """Draw tabbed menu

        Args:
            session_state: Session state string
            callbacks: Callback dict {"connect": fn, "disconnect": fn, "quit": fn}
            params: Parameters dict
            on_param_change: Callback for parameter changes

        Returns:
            running status
        """
        imgui.set_next_window_position(20, 20, imgui.ALWAYS)
        imgui.set_next_window_size(450, 400, imgui.ALWAYS)

        expanded, opened = imgui.begin("Menu", True)
        running = True

        if expanded:
            # Tab bar
            if imgui.begin_tab_bar("MenuTabs"):
                # Connection tab
                if imgui.begin_tab_item("Connection")[0]:
                    self._draw_connection_tab(session_state, callbacks)
                    imgui.end_tab_item()

                # Parameters tab
                if imgui.begin_tab_item("Parameters")[0]:
                    self._draw_parameters_tab(params, on_param_change)
                    imgui.end_tab_item()

                # Video tab
                if imgui.begin_tab_item("Video")[0]:
                    self._draw_video_tab(params, on_param_change)
                    imgui.end_tab_item()

                # Recording tab
                if imgui.begin_tab_item("Recording")[0]:
                    self._draw_recording_tab(params, on_param_change)
                    imgui.end_tab_item()

                # Debug tab
                if imgui.begin_tab_item("Debug")[0]:
                    self._draw_debug_tab(params, on_param_change)
                    imgui.end_tab_item()

                imgui.end_tab_bar()

        imgui.end()

        # Close button (X) only hides menu, doesn't close program
        if not opened:
            self.show_menu = False

        return running

    def _draw_connection_tab(self, session_state: str, callbacks: Dict[str, Callable]) -> None:
        """Draw connection tab"""
        # State indicator
        self._draw_state_indicator(session_state)
        imgui.same_line()
        imgui.text(f"State: {session_state}")

        imgui.separator()

        # Connect button
        if imgui.button("Connect", width=100):
            if "connect" in callbacks:
                callbacks["connect"]()

        imgui.same_line()

        # Disconnect button
        if imgui.button("Disconnect", width=100):
            if "disconnect" in callbacks:
                callbacks["disconnect"]()

        imgui.separator()

        # Quit button
        if imgui.button("Quit", width=150):
            if "quit" in callbacks:
                callbacks["quit"]()

    def _draw_parameters_tab(self, params: Dict, on_change: Optional[Callable]) -> None:
        """Draw parameters tab"""
        imgui.text("Input Settings")
        imgui.separator()

        # Mouse sensitivity
        sensitivity = params.get("mouse_sensitivity", 1.0)
        changed, new_value = imgui.slider_float("Mouse Sensitivity", sensitivity, 0.1, 5.0)
        if changed and on_change:
            on_change("mouse_sensitivity", new_value)

        # FOV
        fov = params.get("fov", 90.0)
        changed, new_value = imgui.slider_float("FOV", fov, 30.0, 120.0)
        if changed and on_change:
            on_change("fov", new_value)

        # Invert pitch
        invert_pitch = params.get("invert_pitch", False)
        changed, new_value = imgui.checkbox("Invert Pitch", invert_pitch)
        if changed and on_change:
            on_change("invert_pitch", new_value)

    def _draw_video_tab(self, params: Dict, on_change: Optional[Callable]) -> None:
        """Draw video tab"""
        imgui.text("Video Settings")
        imgui.separator()

        # Video quality
        quality = params.get("video_quality", 1)
        changed, new_value = imgui.combo("Quality", quality, ["Low", "Medium", "High", "Ultra"])
        if changed and on_change:
            on_change("video_quality", new_value)

        # Resolution
        resolution = params.get("resolution", 1)
        changed, new_value = imgui.combo("Resolution", resolution, ["720p", "1080p", "1440p"])
        if changed and on_change:
            on_change("resolution", new_value)

        # Window mode
        window_mode = params.get("window_mode", 0)
        changed, new_value = imgui.combo("Window Mode", window_mode, ["Windowed", "Fullscreen"])
        if changed and on_change:
            on_change("window_mode", new_value)

    def _draw_recording_tab(self, params: Dict, on_change: Optional[Callable]) -> None:
        """Draw recording tab"""
        imgui.text("Recording Settings")
        imgui.separator()

        # Recording enabled
        recording_enabled = params.get("recording_enabled", False)
        changed, new_value = imgui.checkbox("Enable Recording", recording_enabled)
        if changed and on_change:
            on_change("recording_enabled", new_value)

        # Recording bitrate
        bitrate = params.get("recording_bitrate", 5000)
        changed, new_value = imgui.slider_int("Bitrate (kbps)", bitrate, 1000, 20000)
        if changed and on_change:
            on_change("recording_bitrate", new_value)

    def _draw_debug_tab(self, params: Dict, on_change: Optional[Callable]) -> None:
        """Draw debug tab"""
        imgui.text("Debug Settings")
        imgui.separator()

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
        imgui.set_next_window_position(Config.RENDER_WIDTH - 320, Config.RENDER_HEIGHT - 150, imgui.ALWAYS)
        imgui.set_next_window_size(300, 130, imgui.ALWAYS)

        expanded, opened = imgui.begin("Status", True)
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
