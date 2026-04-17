"""ImGui UI 组件"""

import imgui
from typing import Optional, Callable, Dict
from ui.theme import Theme


class ImGuiUI:
    """ImGui UI 管理"""

    def __init__(self):
        self.show_menu = False
        self.show_params = False
        Theme.apply(imgui)

    def draw_menu(self, session_state: str, callbacks: Dict[str, Callable]) -> bool:
        """Draw menu

        Args:
            session_state: Session state string
            callbacks: Callback dict {"connect": fn, "disconnect": fn, "quit": fn}

        Returns:
            running status
        """
        imgui.set_next_window_position(100, 100, imgui.ALWAYS)
        imgui.set_next_window_size(400, 300, imgui.ALWAYS)

        expanded, opened = imgui.begin("Menu", True)
        if expanded:
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

            # Params button
            if imgui.button("Params", width=100):
                self.show_params = not self.show_params

            imgui.separator()

            # Quit button
            if imgui.button("Quit", width=100):
                if "quit" in callbacks:
                    callbacks["quit"]()
                return False

            imgui.end()

        return opened

    def draw_status_bar(self, status: Dict) -> None:
        """Draw status bar

        Args:
            status: Status dict {"fps", "latency_ms", "packet_loss_rate", "frames_received", "session_state"}
        """
        # Bottom-right adaptive position
        imgui.set_next_window_position(1400, 900, imgui.ALWAYS)
        imgui.set_next_window_size(500, 150, imgui.ALWAYS)

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

    def draw_params_panel(self, params: Dict, on_change: Optional[Callable] = None) -> None:
        """Draw params panel

        Args:
            params: Params dict
            on_change: Param change callback on_change(key, value)
        """
        if not self.show_params:
            return

        imgui.set_next_window_position(100, 450, imgui.ALWAYS)
        imgui.set_next_window_size(400, 300, imgui.ALWAYS)

        expanded, opened = imgui.begin("Parameters", True)
        if expanded:
            # Mouse sensitivity
            sensitivity = params.get("mouse_sensitivity", 1.0)
            changed, new_value = imgui.slider_float("Sensitivity", sensitivity, 0.1, 5.0)
            if changed and on_change:
                on_change("mouse_sensitivity", new_value)

            # FOV
            fov = params.get("fov", 90.0)
            changed, new_value = imgui.slider_float("FOV", fov, 30.0, 120.0)
            if changed and on_change:
                on_change("fov", new_value)

            imgui.end()

        self.show_params = opened

    def _draw_state_indicator(self, state: str) -> None:
        """绘制带颜色的状态圆点

        Args:
            state: 状态字符串
        """
        color = Theme.STATE_COLORS.get(state, Theme.STATE_COLORS["idle"])
        imgui.text_colored("●", *color)
