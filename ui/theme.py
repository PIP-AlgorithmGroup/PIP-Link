"""UI Theme - CS2 inspired modern style"""

import imgui


class Theme:
    """CS2 inspired modern theme - flight control aesthetic"""

    # Background layers (deep to shallow)
    BG_WINDOW = (0.08, 0.08, 0.11, 0.95)
    BG_PANEL = (0.05, 0.05, 0.08, 1.0)
    BG_INPUT = (0.10, 0.10, 0.14, 1.0)

    # Text colors
    TEXT_PRIMARY = (0.92, 0.94, 1.0, 1.0)
    TEXT_SECONDARY = (0.50, 0.52, 0.58, 1.0)

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
        style.colors[imgui.COLOR_WINDOW_BACKGROUND] = Theme.BG_WINDOW
        style.colors[imgui.COLOR_TITLE_BACKGROUND] = (0.03, 0.03, 0.05, 1.0)
        style.colors[imgui.COLOR_TITLE_BACKGROUND_ACTIVE] = Theme.ACCENT_PRIMARY
        style.colors[imgui.COLOR_CHILD_BACKGROUND] = Theme.BG_PANEL

        # Frame
        style.colors[imgui.COLOR_FRAME_BACKGROUND] = Theme.BG_INPUT
        style.colors[imgui.COLOR_FRAME_BACKGROUND_HOVERED] = (0.14, 0.14, 0.20, 1.0)
        style.colors[imgui.COLOR_FRAME_BACKGROUND_ACTIVE] = (0.18, 0.18, 0.25, 1.0)

        # Button
        style.colors[imgui.COLOR_BUTTON] = (0.10, 0.10, 0.15, 1.0)
        style.colors[imgui.COLOR_BUTTON_HOVERED] = (0.0, 0.6, 0.8, 0.8)
        style.colors[imgui.COLOR_BUTTON_ACTIVE] = Theme.ACCENT_PRIMARY

        # Text
        style.colors[imgui.COLOR_TEXT] = Theme.TEXT_PRIMARY
        style.colors[imgui.COLOR_TEXT_DISABLED] = Theme.TEXT_SECONDARY

        # Border
        style.colors[imgui.COLOR_BORDER] = (0.18, 0.18, 0.28, 0.6)
        style.colors[imgui.COLOR_SEPARATOR] = (0.18, 0.18, 0.28, 0.8)

        # Tab - hover shows cyan, active stays dark
        style.colors[imgui.COLOR_TAB] = (0.06, 0.06, 0.10, 0.8)
        style.colors[imgui.COLOR_TAB_HOVERED] = Theme.ACCENT_PRIMARY
        style.colors[imgui.COLOR_TAB_ACTIVE] = (0.12, 0.12, 0.18, 1.0)
        style.colors[imgui.COLOR_TAB_UNFOCUSED] = (0.06, 0.06, 0.10, 0.6)
        style.colors[imgui.COLOR_TAB_UNFOCUSED_ACTIVE] = (0.10, 0.10, 0.16, 0.9)

        # Slider
        style.colors[imgui.COLOR_SLIDER_GRAB] = Theme.ACCENT_PRIMARY
        style.colors[imgui.COLOR_SLIDER_GRAB_ACTIVE] = Theme.ACCENT_HIGHLIGHT

        # Checkbox
        style.colors[imgui.COLOR_CHECK_MARK] = Theme.ACCENT_PRIMARY

        # Header
        style.colors[imgui.COLOR_HEADER] = (0.08, 0.08, 0.12, 1.0)
        style.colors[imgui.COLOR_HEADER_HOVERED] = (0.12, 0.12, 0.18, 1.0)
        style.colors[imgui.COLOR_HEADER_ACTIVE] = Theme.ACCENT_PRIMARY

        # Popup
        style.colors[imgui.COLOR_POPUP_BACKGROUND] = Theme.BG_PANEL

        # Scrollbar - hidden for smooth appearance (alpha 0 but size > 0)
        style.colors[imgui.COLOR_SCROLLBAR_BACKGROUND] = (0.05, 0.05, 0.08, 0.0)
        style.colors[imgui.COLOR_SCROLLBAR_GRAB] = (0.20, 0.20, 0.28, 0.0)
        style.colors[imgui.COLOR_SCROLLBAR_GRAB_HOVERED] = (0.30, 0.30, 0.40, 0.0)
        style.colors[imgui.COLOR_SCROLLBAR_GRAB_ACTIVE] = (0.0, 0.85, 1.0, 0.0)

        # Rounding - tighter, more industrial
        style.frame_rounding = 4.0
        style.window_rounding = 6.0
        style.grab_rounding = 3.0
        style.tab_rounding = 4.0

        # Border
        style.window_border_size = 1.0
        style.frame_border_size = 1.0

        # Padding and spacing
        style.frame_padding = (10, 7)
        style.item_spacing = (10, 10)
        style.item_inner_spacing = (6, 6)
        style.window_padding = (14, 14)
        style.indent_spacing = 18.0
