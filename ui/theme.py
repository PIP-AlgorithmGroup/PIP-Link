"""UI Theme - CS2 inspired modern style"""

import imgui


class Theme:
    """CS2 inspired modern theme"""

    # Color constants (RGBA 0-1)
    BG_DARK = (0.04, 0.04, 0.06, 1.0)
    BG_DARKER = (0.02, 0.02, 0.03, 1.0)
    TEXT_WHITE = (0.98, 0.98, 1.0, 1.0)
    TEXT_SECONDARY = (0.65, 0.65, 0.7, 1.0)

    # Accent colors - CS2 style
    ACCENT_PRIMARY = (0.0, 0.85, 1.0, 1.0)  # Bright cyan
    ACCENT_SECONDARY = (0.2, 0.7, 1.0, 1.0)  # Blue
    ACCENT_HIGHLIGHT = (1.0, 0.3, 0.8, 1.0)  # Pink accent

    # Connection state colors
    STATE_COLORS = {
        "idle": (0.35, 0.35, 0.4, 1.0),
        "discovering": (1.0, 0.65, 0.0, 1.0),
        "connecting": (1.0, 0.65, 0.0, 1.0),
        "connected": (0.0, 1.0, 0.5, 1.0),
        "disconnected": (1.0, 0.2, 0.2, 1.0),
        "reconnecting": (1.0, 0.65, 0.0, 1.0),
    }

    @staticmethod
    def apply(imgui_ctx):
        """Apply CS2 inspired theme"""
        style = imgui.get_style()

        # Window
        style.colors[imgui.COLOR_WINDOW_BACKGROUND] = Theme.BG_DARK
        style.colors[imgui.COLOR_TITLE_BACKGROUND] = (0.03, 0.03, 0.05, 1.0)
        style.colors[imgui.COLOR_TITLE_BACKGROUND_ACTIVE] = Theme.ACCENT_PRIMARY

        # Frame
        style.colors[imgui.COLOR_FRAME_BACKGROUND] = (0.06, 0.06, 0.1, 1.0)
        style.colors[imgui.COLOR_FRAME_BACKGROUND_HOVERED] = (0.1, 0.1, 0.15, 1.0)
        style.colors[imgui.COLOR_FRAME_BACKGROUND_ACTIVE] = Theme.ACCENT_SECONDARY

        # Button
        style.colors[imgui.COLOR_BUTTON] = (0.08, 0.08, 0.12, 1.0)
        style.colors[imgui.COLOR_BUTTON_HOVERED] = Theme.ACCENT_PRIMARY
        style.colors[imgui.COLOR_BUTTON_ACTIVE] = Theme.ACCENT_SECONDARY

        # Text
        style.colors[imgui.COLOR_TEXT] = Theme.TEXT_WHITE
        style.colors[imgui.COLOR_TEXT_DISABLED] = Theme.TEXT_SECONDARY

        # Border
        style.colors[imgui.COLOR_BORDER] = (0.15, 0.15, 0.25, 1.0)
        style.colors[imgui.COLOR_SEPARATOR] = (0.15, 0.15, 0.25, 1.0)

        # Tab
        style.colors[imgui.COLOR_TAB] = (0.06, 0.06, 0.1, 1.0)
        style.colors[imgui.COLOR_TAB_HOVERED] = (0.12, 0.12, 0.18, 1.0)
        style.colors[imgui.COLOR_TAB_ACTIVE] = Theme.ACCENT_PRIMARY
        style.colors[imgui.COLOR_TAB_UNFOCUSED] = (0.06, 0.06, 0.1, 1.0)
        style.colors[imgui.COLOR_TAB_UNFOCUSED_ACTIVE] = (0.1, 0.1, 0.15, 1.0)

        # Slider
        style.colors[imgui.COLOR_SLIDER_GRAB] = Theme.ACCENT_PRIMARY
        style.colors[imgui.COLOR_SLIDER_GRAB_ACTIVE] = Theme.ACCENT_HIGHLIGHT

        # Checkbox
        style.colors[imgui.COLOR_CHECK_MARK] = Theme.ACCENT_PRIMARY

        # Header
        style.colors[imgui.COLOR_HEADER] = (0.08, 0.08, 0.12, 1.0)
        style.colors[imgui.COLOR_HEADER_HOVERED] = (0.12, 0.12, 0.18, 1.0)
        style.colors[imgui.COLOR_HEADER_ACTIVE] = Theme.ACCENT_PRIMARY

        # Rounding and spacing - more generous
        style.frame_rounding = 8.0
        style.window_rounding = 12.0
        style.grab_rounding = 6.0
        style.tab_rounding = 8.0

        # Padding and spacing - more spacious
        style.frame_padding = (12, 8)
        style.item_spacing = (12, 12)
        style.item_inner_spacing = (8, 8)
        style.window_padding = (16, 16)
        style.indent_spacing = 20.0
