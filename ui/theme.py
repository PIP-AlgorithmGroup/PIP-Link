"""UI Theme - Modern dark style"""

import imgui


class Theme:
    """Modern dark theme"""

    # Color constants (RGBA 0-1)
    BG_DARK = (0.08, 0.08, 0.1, 1.0)
    BG_DARKER = (0.05, 0.05, 0.07, 1.0)
    TEXT_WHITE = (0.95, 0.95, 0.95, 1.0)
    ACCENT_BLUE = (0.2, 0.6, 1.0, 1.0)
    ACCENT_GREEN = (0.2, 0.8, 0.4, 1.0)

    # Connection state colors
    STATE_COLORS = {
        "idle": (0.4, 0.4, 0.4, 1.0),  # Gray
        "discovering": (1.0, 0.7, 0.0, 1.0),  # Orange
        "connecting": (1.0, 0.7, 0.0, 1.0),  # Orange
        "connected": (0.2, 0.8, 0.4, 1.0),  # Green
        "disconnected": (0.9, 0.2, 0.2, 1.0),  # Red
        "reconnecting": (1.0, 0.7, 0.0, 1.0),  # Orange
    }

    @staticmethod
    def apply(imgui_ctx):
        """Apply modern dark theme"""
        style = imgui.get_style()

        # Window
        style.colors[imgui.COLOR_WINDOW_BACKGROUND] = Theme.BG_DARK
        style.colors[imgui.COLOR_TITLE_BACKGROUND] = Theme.BG_DARKER
        style.colors[imgui.COLOR_TITLE_BACKGROUND_ACTIVE] = Theme.ACCENT_BLUE

        # Frame
        style.colors[imgui.COLOR_FRAME_BACKGROUND] = Theme.BG_DARKER
        style.colors[imgui.COLOR_FRAME_BACKGROUND_HOVERED] = (0.1, 0.1, 0.15, 1.0)
        style.colors[imgui.COLOR_FRAME_BACKGROUND_ACTIVE] = Theme.ACCENT_BLUE

        # Button
        style.colors[imgui.COLOR_BUTTON] = (0.12, 0.12, 0.15, 1.0)
        style.colors[imgui.COLOR_BUTTON_HOVERED] = Theme.ACCENT_BLUE
        style.colors[imgui.COLOR_BUTTON_ACTIVE] = (0.15, 0.5, 0.9, 1.0)

        # Text
        style.colors[imgui.COLOR_TEXT] = Theme.TEXT_WHITE
        style.colors[imgui.COLOR_TEXT_DISABLED] = (0.5, 0.5, 0.5, 1.0)

        # Border
        style.colors[imgui.COLOR_BORDER] = (0.15, 0.15, 0.2, 1.0)
        style.colors[imgui.COLOR_SEPARATOR] = (0.15, 0.15, 0.2, 1.0)

        # Slider
        style.colors[imgui.COLOR_SLIDER_GRAB] = Theme.ACCENT_BLUE
        style.colors[imgui.COLOR_SLIDER_GRAB_ACTIVE] = (0.15, 0.7, 1.0, 1.0)

        # Rounding
        style.frame_rounding = 4.0
        style.window_rounding = 6.0
        style.grab_rounding = 3.0
