"""UI Theme - Dark game style"""

import imgui


class Theme:
    """Dark game style theme"""

    # Color constants (RGBA 0-1)
    BG_DARK = (0.1, 0.1, 0.1, 0.9)
    TEXT_WHITE = (1.0, 1.0, 1.0, 1.0)

    # Connection state colors
    STATE_COLORS = {
        "idle": (0.5, 0.5, 0.5, 1.0),  # Gray
        "discovering": (1.0, 0.8, 0.0, 1.0),  # Yellow
        "connecting": (1.0, 0.8, 0.0, 1.0),  # Yellow
        "connected": (0.0, 0.9, 0.3, 1.0),  # Green
        "disconnected": (0.9, 0.2, 0.2, 1.0),  # Red
        "reconnecting": (1.0, 0.8, 0.0, 1.0),  # Yellow
    }

    @staticmethod
    def apply(imgui_ctx):
        """Apply dark game style theme"""
        style = imgui.get_style()

        # Background color
        style.colors[imgui.COLOR_WINDOW_BACKGROUND] = Theme.BG_DARK
        style.colors[imgui.COLOR_FRAME_BACKGROUND] = (0.15, 0.15, 0.15, 0.9)
        style.colors[imgui.COLOR_FRAME_BACKGROUND_HOVERED] = (0.2, 0.2, 0.2, 0.9)
        style.colors[imgui.COLOR_FRAME_BACKGROUND_ACTIVE] = (0.25, 0.25, 0.25, 0.9)

        # Button
        style.colors[imgui.COLOR_BUTTON] = (0.2, 0.2, 0.2, 0.9)
        style.colors[imgui.COLOR_BUTTON_HOVERED] = (0.3, 0.3, 0.3, 0.9)
        style.colors[imgui.COLOR_BUTTON_ACTIVE] = (0.4, 0.4, 0.4, 0.9)

        # Text
        style.colors[imgui.COLOR_TEXT] = Theme.TEXT_WHITE

        # Border
        style.colors[imgui.COLOR_BORDER] = (0.3, 0.3, 0.3, 0.9)
