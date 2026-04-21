"""ImGui UI components - CS2 inspired flight control aesthetic"""

import imgui
import pygame
import time
import math
import psutil
import os
from array import array
from typing import Optional, Callable, Dict
from ui.theme import Theme
from config import Config


class ImGuiUI:
    """ImGui UI manager with multi-font rendering"""

    def __init__(self, font_title=None, font_body=None, font_mono=None):
        self.show_menu = False
        self.menu_alpha = 0.0
        self.menu_scale = 0.95
        self._menu_anim_tau = 0.15  # 菜单动画时间常数（秒）

        self.font_title = font_title
        self.font_body = font_body
        self.font_mono = font_mono

        # Track connect time for uptime display
        self._connect_time: Optional[float] = None
        self._last_state: str = "idle"

        # READY 状态（F5 切换）
        self.is_ready = False
        self._ready_anim_dir = False
        self._ready_toggle_time = 0.0

        # Input HUD state
        self._stick_x = 0.0
        self._stick_y = 0.0
        self.show_hud = True
        self._hud_alpha = 1.0

        # Bandwidth tracking
        self._last_bytes: int = 0
        self._last_bw_time: float = time.time()
        self._bandwidth_kbps: float = 0.0

        # Connection tab state
        self._service_name_buf: bytearray = bytearray(b'air_unit_01' + b'\x00' * (128 - 11))

        # Resolution list for VIDEO tab combo
        self._resolution_labels = [
            "960x540 (16:9)",
            "1024x576 (16:9)",
            "1280x720 (16:9)",
            "1366x768 (16:9)",
            "1600x900 (16:9)",
            "1920x1080 (16:9)",
            "2560x1440 (16:9)",
            "3840x2160 (16:9)",
            "1280x800 (16:10)",
            "1440x900 (16:10)",
            "1680x1050 (16:10)",
            "1920x1200 (16:10)",
            "2560x1600 (16:10)",
            "1024x768 (4:3)",
            "1280x960 (4:3)",
            "1600x1200 (4:3)",
            "1280x1024 (5:4)",
            "1600x1280 (5:4)",
        ]

        # Custom tab bar state
        self._active_tab = 0
        self._tab_scroll_x = 0.0
        self._content_scroll_y = 0.0  # elastic scroll target for tab content

        # Frame timing for dt-based animations
        self._last_frame_time: float = time.time()
        # Scroll spring time constant (seconds): smaller = snappier
        self.scroll_tau: float = 0.08

        # Animated combo state: key -> {"open": bool, "h": float, "scroll_target": float}
        self._combo_anim: dict = {}
        # Combo expand time constant (seconds)
        self.combo_tau: float = 0.06
        # Combo item hover alpha: key -> float (0.0 ~ 1.0)
        self._combo_hover_alpha: dict = {}

        # Confirmation dialog state (None = inactive)
        self._confirm_dialog = None

        # Recording tab state
        self._recording_active: bool = False
        self._recording_start_time: Optional[float] = None
        self._recording_file_size_bytes: int = 0

        # Device list hover alpha: idx -> float
        self._device_hover_alpha: dict = {}

        # Key rebinding state: which action is waiting for input (None = not rebinding)
        self._rebinding_action: Optional[str] = None
        # Current key bindings (action -> display label)
        self._key_bindings: dict = {
            "toggle_ready": "F5",
            "toggle_hud": "Tab",
            "toggle_console": "`",
            "toggle_menu": "Esc",
        }

        # Performance history ring buffers (120 samples ~ 1-2 seconds at 120fps)
        self._perf_history_size: int = 120
        self._fps_history: list = [0.0] * self._perf_history_size
        self._latency_history: list = [0.0] * self._perf_history_size
        self._perf_write_idx: int = 0

        # System resource cache (updated once per second)
        self._sys_res_last_update: float = 0.0
        self._sys_res_cpu: float = 0.0
        self._sys_res_mem_percent: float = 0.0
        self._sys_res_mem_used: int = 0
        self._sys_res_mem_total: int = 0
        self._sys_res_proc_mem: int = 0

        Theme.apply(imgui)

    # -------------------------------------------------------------------------
    # Font helpers
    # -------------------------------------------------------------------------

    def _push_font(self, font) -> bool:
        if font is not None:
            imgui.push_font(font)
            return True
        return False

    def _pop_font(self, pushed: bool):
        if pushed:
            imgui.pop_font()

    # -------------------------------------------------------------------------
    # Layout helpers
    # -------------------------------------------------------------------------

    def _draw_section_title(self, text: str):
        pushed = self._push_font(self.font_title)
        imgui.text(text)
        self._pop_font(pushed)
        imgui.spacing()
        imgui.separator()
        imgui.spacing()
        imgui.spacing()

    def _draw_subsection(self, text: str):
        """Smaller secondary heading"""
        pushed = self._push_font(self.font_body)
        imgui.text_colored(text, *Theme.TEXT_SECONDARY)
        self._pop_font(pushed)
        imgui.separator()
        imgui.spacing()

    def _draw_label_value(self, label: str, value: str, accent=False, right_align=False):
        """Label (body, secondary color) + value (mono) on same line"""
        pushed_body = self._push_font(self.font_body)
        imgui.text_colored(label, *Theme.TEXT_SECONDARY)
        self._pop_font(pushed_body)

        pushed_mono = self._push_font(self.font_mono)
        if right_align:
            text_w = imgui.calc_text_size(value).x
            win_w = imgui.get_window_width()
            padding = imgui.get_style().window_padding.x
            imgui.same_line(win_w - text_w - padding)
        else:
            imgui.same_line(0, 8)

        if accent:
            imgui.text_colored(value, *Theme.ACCENT_PRIMARY)
        else:
            imgui.text(value)
        self._pop_font(pushed_mono)

    def _draw_kv_row(self, label: str, value: str, label_width: float = 160, accent=False):
        """Fixed-width label column + value, for aligned tables"""
        pushed_body = self._push_font(self.font_body)
        imgui.text_colored(label, *Theme.TEXT_SECONDARY)
        self._pop_font(pushed_body)

        imgui.same_line(label_width)

        pushed_mono = self._push_font(self.font_mono)
        if accent:
            imgui.text_colored(value, *Theme.ACCENT_PRIMARY)
        else:
            imgui.text(value)
        self._pop_font(pushed_mono)

    def _slider_float_with_hint(self, label: str, value: float, min_val: float, max_val: float, format_str: str = "%.2f") -> tuple:
        """Slider with hint text"""
        imgui.set_next_item_width(300)
        changed, new_val = imgui.slider_float(label, value, min_val, max_val, format_str)
        if imgui.is_item_hovered():
            pushed = self._push_font(self.font_body)
            imgui.set_tooltip("Ctrl+Click to input value")
            self._pop_font(pushed)
        return changed, new_val

    def _slider_int_with_hint(self, label: str, value: int, min_val: int, max_val: int) -> tuple:
        """Slider with hint text"""
        imgui.set_next_item_width(220)
        changed, new_val = imgui.slider_int(label, value, min_val, max_val)
        if imgui.is_item_hovered():
            pushed = self._push_font(self.font_body)
            imgui.set_tooltip("Ctrl+Click to input value")
            self._pop_font(pushed)
        return changed, new_val

    # -------------------------------------------------------------------------
    # Confirmation dialog
    # -------------------------------------------------------------------------

    def _request_confirm(self, param_key: str, old_value, new_value, label: str,
                         on_confirm: Callable, on_revert: Callable):
        """Request user confirmation for a dangerous setting change.
        Immediately applies new_value, but will revert to old_value if not confirmed within 30s."""
        # Ignore if a confirmation dialog is already active
        if self._confirm_dialog is not None:
            return
        self._confirm_dialog = {
            "param_key": param_key,
            "old_value": old_value,
            "new_value": new_value,
            "label": label,
            "deadline": time.time() + 30.0,
            "on_confirm": on_confirm,
            "on_revert": on_revert,
        }
        # Apply new value immediately so user can see the effect
        if on_confirm:
            on_confirm(param_key, new_value)

    def _draw_confirm_dialog(self):
        """Draw modal confirmation dialog with countdown timer."""
        if self._confirm_dialog is None:
            return

        dialog = self._confirm_dialog
        remaining = max(0.0, dialog["deadline"] - time.time())

        # Auto-revert on timeout
        if remaining <= 0.0:
            if dialog["on_revert"]:
                dialog["on_revert"](dialog["param_key"], dialog["old_value"])
            self._confirm_dialog = None
            return

        # Modal popup
        imgui.open_popup("CONFIRM CHANGE")
        dialog_w = 450
        dialog_h = 260
        imgui.set_next_window_size(dialog_w, dialog_h, imgui.ALWAYS)

        # Center the popup
        disp_w, disp_h = imgui.get_io().display_size
        center_x = (disp_w - dialog_w) / 2
        center_y = (disp_h - dialog_h) / 2
        imgui.set_next_window_position(center_x, center_y, imgui.ALWAYS)

        if imgui.begin_popup_modal("CONFIRM CHANGE", None, imgui.WINDOW_NO_RESIZE | imgui.WINDOW_NO_MOVE)[0]:
            pushed_body = self._push_font(self.font_body)

            # Display change description (centered)
            text_w = imgui.calc_text_size(dialog["label"]).x
            imgui.set_cursor_pos_x((dialog_w - text_w) * 0.5)
            imgui.text(dialog["label"])
            imgui.spacing()
            imgui.spacing()

            # Countdown timer label
            timer_label = "Reverting in:"
            label_w = imgui.calc_text_size(timer_label).x
            imgui.set_cursor_pos_x((dialog_w - label_w) * 0.5)
            imgui.text(timer_label)
            self._pop_font(pushed_body)

            # Large countdown number (centered)
            pushed_title = self._push_font(self.font_title)
            countdown_text = f"{int(remaining)}s"
            countdown_w = imgui.calc_text_size(countdown_text).x
            imgui.set_cursor_pos_x((dialog_w - countdown_w) * 0.5)
            imgui.text_colored(countdown_text, *Theme.ACCENT_PRIMARY)
            self._pop_font(pushed_title)

            imgui.spacing()
            imgui.spacing()

            # Buttons (centered)
            pushed_body = self._push_font(self.font_body)
            btn_w = 150
            btn_gap = 16
            total_btn_w = btn_w * 2 + btn_gap
            imgui.set_cursor_pos_x((dialog_w - total_btn_w) * 0.5)

            if imgui.button("CONFIRM", btn_w, 36):
                # Already applied, just close dialog
                self._confirm_dialog = None
                imgui.close_current_popup()

            imgui.same_line(0, btn_gap)

            if imgui.button("REVERT", btn_w, 36):
                # Revert to old value
                if dialog["on_revert"]:
                    dialog["on_revert"](dialog["param_key"], dialog["old_value"])
                self._confirm_dialog = None
                imgui.close_current_popup()

            self._pop_font(pushed_body)
            imgui.end_popup()

    # -------------------------------------------------------------------------
    # Animation
    # -------------------------------------------------------------------------

    def _animated_combo(self, label: str, current: int, items: list, width: int = 220):
        """Drop-down combo with expand/collapse height animation.
        Returns (changed, new_index) same as imgui.combo."""
        text_h = imgui.get_text_line_height()
        item_pad_y = 4  # vertical padding inside each item
        step_h = text_h + item_pad_y * 2  # total height per item row
        pad_top = 8
        max_visible = 6
        needs_scroll = len(items) > max_visible
        n = max_visible if needs_scroll else len(items)
        full_h = step_h * n + 2 * pad_top

        state = self._combo_anim.setdefault(
            label, {"open": False, "h": 0.0, "scroll_target": 0.0})

        # Animate height toward target (open) or 0 (closed)
        h_target = full_h if state["open"] else 0.0
        diff = h_target - state["h"]
        if abs(diff) > 0.5:
            t = 1.0 - math.exp(-self._dt / self.combo_tau)
            state["h"] = state["h"] + diff * t
        else:
            state["h"] = h_target

        anim_h = max(state["h"], 1.0)

        # Zero out popup padding so the frame matches content exactly
        imgui.push_style_var(imgui.STYLE_POPUP_ROUNDING, 4.0)
        imgui.push_style_var(imgui.STYLE_WINDOW_PADDING, (0, 0))
        imgui.set_next_window_size_constraints((width, anim_h), (width, anim_h))
        imgui.set_next_item_width(width)
        opened = imgui.begin_combo(label, items[current] if 0 <= current < len(items) else "")

        if opened != state["open"]:
            state["open"] = opened
            if opened:
                state["scroll_target"] = 0.0

        changed = False
        new_index = current

        if opened:
            animating = abs(state["h"] - full_h) > 0.5

            # Short lists or animating: no scroll; long lists when done: elastic scroll
            if not needs_scroll or animating:
                child_flags = imgui.WINDOW_NO_SCROLLBAR | imgui.WINDOW_NO_SCROLL_WITH_MOUSE
            else:
                child_flags = imgui.WINDOW_NO_SCROLLBAR

            imgui.begin_child(f"##combo_clip_{label}", width, anim_h,
                              border=False, flags=child_flags)

            # Zero item spacing inside combo so items are flush
            imgui.push_style_var(imgui.STYLE_ITEM_SPACING, (0, 0))

            # Top padding
            imgui.dummy(0, pad_top)

            # Elastic scroll for long lists (we handle wheel ourselves)
            if needs_scroll and not animating:
                if imgui.is_window_hovered(imgui.HOVERED_CHILD_WINDOWS):
                    io = imgui.get_io()
                    if io.mouse_wheel != 0:
                        state["scroll_target"] -= io.mouse_wheel * 100

                max_scroll_y = imgui.get_scroll_max_y()
                state["scroll_target"] = max(0.0, min(state["scroll_target"], max_scroll_y))

                cur_y = imgui.get_scroll_y()
                diff_y = state["scroll_target"] - cur_y
                if abs(diff_y) > 0.5:
                    st = 1.0 - math.exp(-self._dt / self.scroll_tau)
                    imgui.set_scroll_y(cur_y + diff_y * st)
                else:
                    imgui.set_scroll_y(state["scroll_target"])

            for i, item in enumerate(items):
                is_selected = (i == current)
                hover_key = f"{label}_{i}"

                # Invisible button as click target
                item_w = width
                item_h_px = step_h
                cursor_pos = imgui.get_cursor_screen_pos()

                imgui.push_id(f"combo_item_{i}")
                clicked = imgui.invisible_button(f"##ci{i}", item_w, item_h_px)
                hovered = imgui.is_item_hovered()
                imgui.pop_id()

                # Animate hover alpha
                cur_alpha = self._combo_hover_alpha.get(hover_key, 0.0)
                target_alpha = 1.0 if hovered else 0.0
                alpha_diff = target_alpha - cur_alpha
                if abs(alpha_diff) > 0.01:
                    ht = 1.0 - math.exp(-self._dt / 0.06)
                    cur_alpha = cur_alpha + alpha_diff * ht
                else:
                    cur_alpha = target_alpha
                self._combo_hover_alpha[hover_key] = cur_alpha

                draw_list = imgui.get_window_draw_list()
                pad_x = 0  # symmetric left/right inset for hover background
                rounding = 4.0

                # Draw hover/selected background with rounded corners
                if cur_alpha > 0.01 or is_selected:
                    bg_alpha = max(cur_alpha * 0.6, 0.4 if is_selected else 0.0)
                    bg_color = imgui.get_color_u32_rgba(0.12, 0.12, 0.18, bg_alpha)
                    draw_list.add_rect_filled(
                        cursor_pos[0] + pad_x, cursor_pos[1],
                        cursor_pos[0] + item_w - pad_x, cursor_pos[1] + item_h_px,
                        bg_color, rounding)

                # Draw text with left padding
                text_x = cursor_pos[0] + pad_x + 6
                text_y = cursor_pos[1] + (item_h_px - imgui.get_text_line_height()) * 0.5
                text_color = imgui.get_color_u32_rgba(*Theme.ACCENT_PRIMARY) if is_selected \
                    else imgui.get_color_u32_rgba(*Theme.TEXT_PRIMARY)
                draw_list.add_text(text_x, text_y, text_color, item)

                if clicked:
                    new_index = i
                    changed = True
                if is_selected:
                    imgui.set_item_default_focus()

            # Bottom padding so last item can scroll fully into view
            if needs_scroll:
                imgui.dummy(0, step_h)

            imgui.pop_style_var()  # STYLE_ITEM_SPACING
            imgui.end_child()
            imgui.end_combo()

        imgui.pop_style_var(2)  # STYLE_WINDOW_PADDING, STYLE_POPUP_ROUNDING

        return changed, new_index

    def _update_menu_animation(self):
        dt = self._dt  # 由 draw 入口计算的帧间隔
        t = 1.0 - math.exp(-dt / self._menu_anim_tau)

        target_alpha = 1.0 if self.show_menu else 0.0
        target_scale = 1.0 if self.show_menu else 0.95

        self.menu_alpha += (target_alpha - self.menu_alpha) * t
        self.menu_scale += (target_scale - self.menu_scale) * t

    # -------------------------------------------------------------------------
    # Main menu
    # -------------------------------------------------------------------------

    def draw_menu(
        self,
        session_state: str,
        callbacks: Dict[str, Callable],
        params: Dict,
        on_param_change: Optional[Callable] = None,
        stats: Optional[Dict] = None,
        live_status: Optional[Dict] = None,
    ) -> None:
        if not self.show_menu and self.menu_alpha <= 0.01:
            return

        # Track connect time
        if session_state == "connected" and self._last_state != "connected":
            self._connect_time = time.time()
        elif session_state != "connected":
            self._connect_time = None
        self._last_state = session_state

        # Update bandwidth estimate
        if stats:
            bytes_now = stats.get("bytes_received", 0)
            t_now = time.time()
            # 重连后计数器重置，检测到回退则重新开始计算
            if bytes_now < self._last_bytes:
                self._last_bytes = bytes_now
                self._last_bw_time = t_now
                self._bandwidth_kbps = 0.0
            else:
                dt = t_now - self._last_bw_time
                if dt >= 0.5:
                    self._bandwidth_kbps = (bytes_now - self._last_bytes) * 8 / dt / 1000
                    self._last_bytes = bytes_now
                    self._last_bw_time = t_now

        # Delta time for frame-rate-independent animations
        now = time.time()
        self._dt = min(now - self._last_frame_time, 0.1)  # cap at 100ms to avoid jumps
        self._last_frame_time = now

        self._update_menu_animation()
        imgui.push_style_var(imgui.STYLE_ALPHA, self.menu_alpha)

        menu_width = 960
        menu_height = 750
        disp_w, disp_h = imgui.get_io().display_size
        center_x = (disp_w - menu_width) / 2
        center_y = (disp_h - menu_height) / 2

        imgui.set_next_window_position(center_x, center_y, imgui.ALWAYS)
        imgui.set_next_window_size(menu_width, menu_height, imgui.ALWAYS)

        expanded, _ = imgui.begin(
            "##menu", False,
            imgui.WINDOW_NO_TITLE_BAR | imgui.WINDOW_NO_MOVE | imgui.WINDOW_NO_RESIZE
        )

        if expanded:
            # Custom tab bar with horizontal scroll
            self._draw_custom_tab_bar()

            # Draw active tab content with elastic vertical scroll
            imgui.spacing()
            r, g, b, _ = Theme.BG_WINDOW
            imgui.push_style_color(imgui.COLOR_CHILD_BACKGROUND, r, g, b, 0.0)
            imgui.begin_child("##tab_content", 0, 0, border=False,
                              flags=imgui.WINDOW_NO_SCROLL_WITH_MOUSE)

            # Elastic vertical scroll (we handle wheel ourselves)
            if imgui.is_window_hovered(imgui.HOVERED_CHILD_WINDOWS):
                io = imgui.get_io()
                if io.mouse_wheel != 0:
                    self._content_scroll_y -= io.mouse_wheel * 100

            max_scroll_y = imgui.get_scroll_max_y()
            self._content_scroll_y = max(0.0, min(self._content_scroll_y, max_scroll_y))

            cur_y = imgui.get_scroll_y()
            diff_y = self._content_scroll_y - cur_y
            if abs(diff_y) > 0.5:
                t = 1.0 - math.exp(-self._dt / self.scroll_tau)
                imgui.set_scroll_y(cur_y + diff_y * t)
            else:
                imgui.set_scroll_y(self._content_scroll_y)

            if self._active_tab == 0:
                self._draw_connection_tab(session_state, callbacks, stats or {}, live_status or {})
            elif self._active_tab == 1:
                self._draw_parameters_tab(params, on_param_change)
            elif self._active_tab == 2:
                self._draw_video_tab(params, on_param_change, stats or {}, live_status or {})
            elif self._active_tab == 3:
                self._draw_recording_tab(params, on_param_change, callbacks)
            elif self._active_tab == 4:
                self._draw_diagnostics_tab(stats or {}, live_status or {})
            elif self._active_tab == 5:
                self._draw_control_settings_tab(params, on_param_change, callbacks)
            elif self._active_tab == 6:
                self._draw_debug_tab(params, on_param_change, stats or {}, live_status or {})
            elif self._active_tab == 7:
                self._draw_about_tab()

            # Compensate for outer window padding so content can scroll fully to bottom
            pad_bottom = imgui.get_style().window_padding[1]
            imgui.dummy(0, pad_bottom)

            imgui.end_child()
            imgui.pop_style_color()

            # Confirmation dialog renders on top of everything
            self._draw_confirm_dialog()

        imgui.end()
        imgui.pop_style_var()

    def _draw_custom_tab_bar(self):
        """Custom tab bar: wheel scrolls horizontally, click to switch.
        Style: plain text labels with accent underline on active tab."""
        tab_labels = ["CONNECTION", "PARAMETERS", "VIDEO", "RECORDING",
                      "DIAGNOSTICS", "CONTROL", "DEBUG", "ABOUT"]

        tab_bar_height = 34
        pad_x = 16
        gap = 6

        imgui.push_style_color(imgui.COLOR_CHILD_BACKGROUND, 0.0, 0.0, 0.0, 0.0)
        imgui.push_style_var(imgui.STYLE_ITEM_SPACING, (gap, 0))

        imgui.begin_child(
            "##tab_bar_scroll", 0, tab_bar_height, border=False,
            flags=imgui.WINDOW_HORIZONTAL_SCROLLING_BAR | imgui.WINDOW_NO_SCROLLBAR
        )

        # Mouse wheel -> update target scroll position
        if imgui.is_window_hovered():
            io = imgui.get_io()
            if io.mouse_wheel != 0:
                self._tab_scroll_x -= io.mouse_wheel * 100

        # Clamp target
        max_scroll = imgui.get_scroll_max_x()
        self._tab_scroll_x = max(0.0, min(self._tab_scroll_x, max_scroll))

        # Smooth elastic interpolation toward target (dt-based, frame-rate independent)
        cur = imgui.get_scroll_x()
        diff = self._tab_scroll_x - cur
        if abs(diff) > 0.5:
            t = 1.0 - math.exp(-self._dt / self.scroll_tau)
            imgui.set_scroll_x(cur + diff * t)
        else:
            imgui.set_scroll_x(self._tab_scroll_x)

        draw_list = imgui.get_window_draw_list()
        pushed_font = self._push_font(self.font_title)

        # Get tab bar window info for auto-scroll
        tab_bar_width = imgui.get_window_width()
        content_x = 0.0  # Track cumulative x position in content

        for i, label in enumerate(tab_labels):
            if i > 0:
                imgui.same_line()

            is_active = (i == self._active_tab)

            # Invisible button as click target
            text_size = imgui.calc_text_size(label)
            btn_w = text_size.x + pad_x * 2
            btn_h = tab_bar_height

            cursor_pos = imgui.get_cursor_screen_pos()

            imgui.push_id(str(i))
            clicked = imgui.invisible_button(f"##tab{i}", btn_w, btn_h)
            hovered = imgui.is_item_hovered()
            imgui.pop_id()

            if clicked:
                self._active_tab = i
                self._content_scroll_y = 0.0

                # Auto-scroll tab bar to show this tab fully
                scroll_x = imgui.get_scroll_x()
                tab_left = content_x
                tab_right = content_x + btn_w

                # If tab is left of visible area, scroll left
                if tab_left < scroll_x:
                    self._tab_scroll_x = tab_left - gap
                # If tab is right of visible area, scroll right
                elif tab_right > scroll_x + tab_bar_width:
                    self._tab_scroll_x = tab_right - tab_bar_width + gap

            # Track content position for next iteration
            content_x += btn_w + gap

            # Text color
            if is_active:
                text_color = imgui.get_color_u32_rgba(*Theme.ACCENT_PRIMARY)
            elif hovered:
                text_color = imgui.get_color_u32_rgba(0.8, 0.85, 0.95, 1.0)
            else:
                text_color = imgui.get_color_u32_rgba(*Theme.TEXT_SECONDARY)

            # Draw label centered in button area
            tx = cursor_pos[0] + (btn_w - text_size.x) * 0.5
            ty = cursor_pos[1] + (btn_h - text_size.y) * 0.5
            draw_list.add_text(tx, ty, text_color, label)

            # Active tab: accent underline
            if is_active:
                line_y = cursor_pos[1] + btn_h - 2
                line_color = imgui.get_color_u32_rgba(*Theme.ACCENT_PRIMARY)
                draw_list.add_rect_filled(
                    cursor_pos[0] + 4, line_y,
                    cursor_pos[0] + btn_w - 4, line_y + 2,
                    line_color
                )

        self._pop_font(pushed_font)

        imgui.end_child()
        imgui.pop_style_var()
        imgui.pop_style_color()

    def _tab(self, label: str, draw_fn: Callable):
        pushed = self._push_font(self.font_title)
        selected = imgui.begin_tab_item(label)[0]
        self._pop_font(pushed)
        if selected:
            imgui.spacing()
            r, g, b, _ = Theme.BG_WINDOW
            imgui.push_style_color(imgui.COLOR_CHILD_BACKGROUND, r, g, b, 0.0)
            imgui.begin_child(f"##tab_content_{label}", 0, 0, border=False)
            draw_fn()
            imgui.end_child()
            imgui.pop_style_color()
            imgui.end_tab_item()

    # -------------------------------------------------------------------------
    # CONNECTION tab
    # -------------------------------------------------------------------------

    def _draw_connection_tab(self, session_state: str, callbacks: Dict, stats: Dict, live_status: Dict = None) -> None:
        if live_status is None:
            live_status = {}
        self._draw_section_title("CONNECTION STATUS")

        # --- State indicator row ---
        self._draw_state_indicator(session_state)
        imgui.same_line(0, 10)
        pushed = self._push_font(self.font_mono)
        color = Theme.STATE_COLORS.get(session_state, Theme.STATE_COLORS["idle"])
        imgui.text_colored(session_state.upper(), *color)
        self._pop_font(pushed)

        imgui.spacing()

        # --- Server info ---
        server_ip = stats.get("server_ip", "")
        server_port = stats.get("server_port", 0)
        if server_ip:
            self._draw_kv_row("SERVER", f"{server_ip}:{server_port}", accent=True)
        else:
            self._draw_kv_row("SERVER", "N/A")

        # Uptime
        if self._connect_time and session_state == "connected":
            uptime_s = int(time.time() - self._connect_time)
            h, rem = divmod(uptime_s, 3600)
            m, s = divmod(rem, 60)
            self._draw_kv_row("UPTIME", f"{h:02d}:{m:02d}:{s:02d}", accent=True)
        else:
            self._draw_kv_row("UPTIME", "N/A")

        hb = stats.get("heartbeats_sent", 0)
        self._draw_kv_row("HEARTBEATS", f"{hb}")

        imgui.spacing()
        imgui.separator()
        imgui.spacing()

        # --- Network quality (shown when connected) ---
        pushed = self._push_font(self.font_body)
        imgui.text_colored("NETWORK QUALITY", *Theme.TEXT_SECONDARY)
        self._pop_font(pushed)
        imgui.separator()
        imgui.spacing()

        latency = live_status.get("latency_ms", 0.0)
        loss = live_status.get("packet_loss_rate", 0.0)
        bw = self._bandwidth_kbps

        if session_state == "connected":
            # Latency with color coding
            if latency < 30:
                lat_color = (0.0, 1.0, 0.5, 1.0)    # green: excellent
            elif latency < 80:
                lat_color = (1.0, 0.85, 0.0, 1.0)   # yellow: ok
            else:
                lat_color = (1.0, 0.3, 0.3, 1.0)    # red: bad

            pushed_body = self._push_font(self.font_body)
            imgui.text_colored("LATENCY", *Theme.TEXT_SECONDARY)
            self._pop_font(pushed_body)
            imgui.same_line(160)
            pushed_mono = self._push_font(self.font_mono)
            imgui.text_colored(f"{latency:.2f} ms", *lat_color)
            self._pop_font(pushed_mono)

            # Packet loss with color coding
            if loss < 0.01:
                loss_color = (0.0, 1.0, 0.5, 1.0)
            elif loss < 0.05:
                loss_color = (1.0, 0.85, 0.0, 1.0)
            else:
                loss_color = (1.0, 0.3, 0.3, 1.0)

            pushed_body = self._push_font(self.font_body)
            imgui.text_colored("PACKET LOSS", *Theme.TEXT_SECONDARY)
            self._pop_font(pushed_body)
            imgui.same_line(160)
            pushed_mono = self._push_font(self.font_mono)
            imgui.text_colored(f"{loss:.2%}", *loss_color)
            self._pop_font(pushed_mono)

            self._draw_kv_row("BANDWIDTH", f"{bw:.0f} kbps", accent=bw > 0)
        else:
            pushed = self._push_font(self.font_mono)
            imgui.text_colored("  -- not connected --", *Theme.TEXT_SECONDARY)
            self._pop_font(pushed)

        imgui.spacing()
        imgui.separator()
        imgui.spacing()

        # --- Available devices list ---
        pushed = self._push_font(self.font_body)
        imgui.text_colored("AVAILABLE DEVICES", *Theme.TEXT_SECONDARY)
        self._pop_font(pushed)
        imgui.separator()
        imgui.spacing()

        # Device list (mock data for UI, backend will populate this)
        devices = stats.get("discovered_devices", [])

        if devices:
            pushed = self._push_font(self.font_mono)
            text_h = imgui.get_text_line_height()
            row_pad_y = 6
            row_h = text_h + row_pad_y * 2
            btn_w = 90
            btn_h = text_h + 10
            list_pad = 6
            list_h = min(row_h * len(devices) + list_pad * 2, 220)

            # 手绘圆角边框（不用 begin_child border=True）
            list_origin = imgui.get_cursor_screen_pos()
            parent_dl = imgui.get_window_draw_list()
            content_w = imgui.get_content_region_available_width()

            # 圆角外框
            border_color = imgui.get_color_u32_rgba(0.25, 0.25, 0.3, 0.6)
            parent_dl.add_rect(
                list_origin[0], list_origin[1],
                list_origin[0] + content_w, list_origin[1] + list_h,
                border_color, rounding=6.0)

            imgui.begin_child("##device_list", content_w, list_h, border=False,
                              flags=imgui.WINDOW_NO_SCROLLBAR)
            imgui.set_cursor_pos((list_pad, list_pad))

            draw_list = imgui.get_window_draw_list()
            inner_w = content_w - list_pad * 2

            for idx, device in enumerate(devices):
                device_name = device.get("name", "Unknown")
                device_ip = device.get("ip", "0.0.0.0")
                device_port = device.get("port", 0)

                row_origin = imgui.get_cursor_screen_pos()

                # Hover 检测：用窄的 invisible_button（不覆盖按钮区域）
                row_interact_w = inner_w - btn_w - 12
                imgui.push_id(f"dev_{idx}")
                imgui.invisible_button(f"##devrow{idx}", row_interact_w, row_h)
                row_hovered = imgui.is_item_hovered()

                # Animate hover alpha
                hover_key = f"dev_{idx}"
                cur_alpha = self._device_hover_alpha.get(hover_key, 0.0)
                target_alpha = 1.0 if row_hovered else 0.0
                alpha_diff = target_alpha - cur_alpha
                if abs(alpha_diff) > 0.01:
                    ht = 1.0 - math.exp(-self._dt / 0.06)
                    cur_alpha += alpha_diff * ht
                else:
                    cur_alpha = target_alpha
                self._device_hover_alpha[hover_key] = cur_alpha

                # 行背景（圆角）
                if cur_alpha > 0.01:
                    bg_color = imgui.get_color_u32_rgba(0.15, 0.18, 0.22, cur_alpha * 0.6)
                    draw_list.add_rect_filled(
                        row_origin[0], row_origin[1],
                        row_origin[0] + inner_w, row_origin[1] + row_h,
                        bg_color, rounding=4.0)

                # 设备名（截断 mDNS 后缀）
                short_name = device_name.split("._")[0] if "._" in device_name else device_name
                text_y = row_origin[1] + (row_h - text_h) * 0.5
                name_color = imgui.get_color_u32_rgba(*Theme.TEXT_PRIMARY)
                draw_list.add_text(row_origin[0] + 8, text_y, name_color, short_name)

                # IP:Port
                addr_text = f"{device_ip}:{device_port}"
                addr_w = imgui.calc_text_size(addr_text).x
                addr_x = row_origin[0] + inner_w - btn_w - 16 - addr_w
                addr_color = imgui.get_color_u32_rgba(*Theme.TEXT_SECONDARY)
                draw_list.add_text(addr_x, text_y, addr_color, addr_text)

                # CONNECT 按钮（手绘，精确控制文字位置）
                imgui.same_line(0, 0)
                btn_x = row_origin[0] + inner_w - btn_w - 4
                btn_y_pos = row_origin[1] + (row_h - btn_h) * 0.5
                imgui.set_cursor_screen_pos((btn_x, btn_y_pos))

                # invisible_button 作为点击区域
                btn_clicked = imgui.invisible_button(f"##connect_btn{idx}", btn_w, btn_h)
                btn_hovered = imgui.is_item_hovered()

                # 手绘按钮背景 + 文字
                draw_list = imgui.get_window_draw_list()
                btn_color = (0.0, 0.7, 0.85, 0.9) if btn_hovered else (0.0, 0.55, 0.65, 0.8)
                btn_color_u32 = imgui.get_color_u32_rgba(*btn_color)
                draw_list.add_rect_filled(btn_x, btn_y_pos, btn_x + btn_w, btn_y_pos + btn_h,
                                          btn_color_u32, rounding=4.0)

                # 文字居中（上移 1px 补偿基线）
                text_h = imgui.get_text_line_height()
                text_x = btn_x + (btn_w - imgui.calc_text_size("CONNECT")[0]) * 0.5
                text_y = btn_y_pos + (btn_h - text_h) * 0.5
                text_color = imgui.get_color_u32_rgba(1.0, 1.0, 1.0, 1.0)
                draw_list.add_text(text_x, text_y, text_color, "CONNECT")

                if btn_clicked:
                    if "select_device" in callbacks:
                        callbacks["select_device"](idx)

                imgui.pop_id()

                # 下一行光标
                imgui.set_cursor_screen_pos((row_origin[0], row_origin[1] + row_h))

            imgui.end_child()
            self._pop_font(pushed)
        else:
            pushed = self._push_font(self.font_mono)
            imgui.text_colored("  No devices found. Click SCAN to discover.", *Theme.TEXT_SECONDARY)
            self._pop_font(pushed)

        imgui.spacing()

        # SCAN button
        pushed_btn = self._push_font(self.font_body)
        if imgui.button("SCAN", width=100, height=36):
            if "scan_devices" in callbacks:
                callbacks["scan_devices"]()
        self._pop_font(pushed_btn)

        imgui.spacing()
        imgui.separator()
        imgui.spacing()

        # --- Device name input (manual entry) ---
        pushed = self._push_font(self.font_body)
        imgui.text_colored("OR ENTER DEVICE NAME MANUALLY (e.g. air_unit_01)", *Theme.TEXT_SECONDARY)
        self._pop_font(pushed)
        imgui.spacing()

        pushed = self._push_font(self.font_mono)
        imgui.set_next_item_width(400)
        changed, new_name = imgui.input_text("##device_name", self._service_name_buf.rstrip(b'\x00').decode(), 128)
        if changed:
            encoded = new_name.encode()[:127]
            self._service_name_buf = bytearray(encoded + b'\x00' * (128 - len(encoded)))
        self._pop_font(pushed)

        imgui.spacing()
        imgui.separator()
        imgui.spacing()

        # --- Action buttons ---
        is_idle = session_state in ("idle", "disconnected")
        is_connected = session_state == "connected"
        is_busy = session_state in ("discovering", "connecting", "reconnecting")

        pushed = self._push_font(self.font_body)

        # CONNECT button - disabled when busy or already connected
        connect_disabled = not is_idle
        disconnect_disabled = not (is_connected or is_busy)

        if connect_disabled:
            imgui.push_style_var(imgui.STYLE_ALPHA, self.menu_alpha * 0.4)
        if imgui.button("CONNECT", width=150, height=36):
            if is_idle and "connect_by_name" in callbacks:
                svc = self._service_name_buf.rstrip(b'\x00').decode().strip()
                callbacks["connect_by_name"](svc)
        if connect_disabled:
            imgui.pop_style_var()

        imgui.same_line(0, 16)

        # DISCONNECT button - disabled when not connected/busy
        if disconnect_disabled:
            imgui.push_style_var(imgui.STYLE_ALPHA, self.menu_alpha * 0.4)
        if imgui.button("DISCONNECT", width=150, height=36):
            if (is_connected or is_busy) and "disconnect" in callbacks:
                callbacks["disconnect"]()
        if disconnect_disabled:
            imgui.pop_style_var()

        imgui.spacing()
        imgui.spacing()

        if imgui.button("QUIT", width=316, height=36):
            if "quit" in callbacks:
                callbacks["quit"]()
        self._pop_font(pushed)

    # -------------------------------------------------------------------------
    # PARAMETERS tab
    # -------------------------------------------------------------------------

    def _draw_parameters_tab(self, params: Dict, on_change: Optional[Callable]) -> None:
        self._draw_section_title("INPUT SETTINGS")

        pushed = self._push_font(self.font_body)

        # Mouse sensitivity with slider
        sensitivity = params.get("mouse_sensitivity", 1.0)
        changed, new_val = self._slider_float_with_hint(
            "Mouse Sensitivity##sens", sensitivity, 0.1, 5.0, "%.2f"
        )
        if changed and on_change:
            on_change("mouse_sensitivity", new_val)

        # FOV with slider
        fov = params.get("fov", 90.0)
        changed, new_val = self._slider_float_with_hint(
            "FOV##fov", fov, 30.0, 120.0, "%.0f deg"
        )
        if changed and on_change:
            on_change("fov", new_val)

        imgui.spacing()
        self._draw_subsection("CONTROL OPTIONS")

        invert_pitch = params.get("invert_pitch", False)
        changed, new_val = imgui.checkbox("Invert Pitch", invert_pitch)
        if changed and on_change:
            on_change("invert_pitch", new_val)

        imgui.spacing()
        self._draw_subsection("CURRENT VALUES")
        self._pop_font(pushed)

        self._draw_kv_row("Sensitivity", f"{params.get('mouse_sensitivity', 1.0):.2f}x")
        self._draw_kv_row("FOV", f"{params.get('fov', 90.0):.0f} deg")
        self._draw_kv_row("Invert Pitch", "ON" if params.get("invert_pitch") else "OFF")

    # -------------------------------------------------------------------------
    # VIDEO tab
    # -------------------------------------------------------------------------

    def _draw_video_tab(self, params: Dict, on_change: Optional[Callable],
                        stats: Dict, live_status: Dict) -> None:
        self._draw_section_title("VIDEO SETTINGS")

        pushed = self._push_font(self.font_body)

        quality_labels = ["LOW", "MEDIUM", "HIGH", "ULTRA"]
        quality = params.get("video_quality", 1)
        changed, new_val = self._animated_combo("Quality", quality, quality_labels)
        if changed and new_val != quality and on_change:
            self._request_confirm(
                param_key="video_quality",
                old_value=quality,
                new_value=new_val,
                label=f"Quality: {quality_labels[quality]} -> {quality_labels[new_val]}",
                on_confirm=on_change,
                on_revert=on_change,
            )

        resolution = params.get("resolution", 2)
        changed, new_val = self._animated_combo("Resolution", resolution, self._resolution_labels)
        if changed and new_val != resolution and on_change:
            old_label = self._resolution_labels[resolution].split(" ")[0]
            new_label = self._resolution_labels[new_val].split(" ")[0]
            self._request_confirm(
                param_key="resolution",
                old_value=resolution,
                new_value=new_val,
                label=f"Resolution: {old_label} -> {new_label}",
                on_confirm=on_change,
                on_revert=on_change,
            )

        wm_labels = ["WINDOWED", "FULLSCREEN"]
        window_mode = params.get("window_mode", 0)
        changed, new_val = self._animated_combo("Window Mode", window_mode, wm_labels)
        if changed and new_val != window_mode and on_change:
            self._request_confirm(
                param_key="window_mode",
                old_value=window_mode,
                new_value=new_val,
                label=f"Window Mode: {wm_labels[window_mode]} -> {wm_labels[new_val]}",
                on_confirm=on_change,
                on_revert=on_change,
            )

        # Display selector (only in fullscreen modes)
        if window_mode != 0:
            try:
                num_displays = pygame.display.get_num_displays()
                if num_displays > 1:
                    desk_sizes = pygame.display.get_desktop_sizes()
                    display_labels = ["AUTO (Current)"]
                    for i in range(num_displays):
                        if i < len(desk_sizes):
                            dw, dh = desk_sizes[i]
                            display_labels.append(f"Display {i+1}  ({dw}x{dh})")
                        else:
                            display_labels.append(f"Display {i+1}")
                    current_display = params.get("fullscreen_display", -1)
                    combo_idx = current_display + 1
                    changed_d, new_d = self._animated_combo("Output Display", combo_idx, display_labels)
                    if changed_d and new_d != combo_idx and on_change:
                        on_change("fullscreen_display", new_d - 1)
            except Exception:
                pass

        imgui.spacing()
        self._draw_subsection("LIVE STREAM STATS")
        self._pop_font(pushed)

        fps = live_status.get("fps", 0.0)
        frames = stats.get("frames_received", 0)
        bytes_rx = stats.get("bytes_received", 0)
        mb_rx = bytes_rx / (1024 * 1024)

        fps_accent = fps > 0
        self._draw_kv_row("Stream FPS", f"{fps:.1f}", accent=fps_accent)
        self._draw_kv_row("Frames Received", f"{frames}")
        self._draw_kv_row("Data Received", f"{mb_rx:.2f} MB")
        self._draw_kv_row("Bandwidth", f"{self._bandwidth_kbps:.0f} kbps",
                          accent=self._bandwidth_kbps > 0)

    # -------------------------------------------------------------------------
    # RECORDING tab
    # -------------------------------------------------------------------------

    def _draw_recording_tab(self, params: Dict, on_change: Optional[Callable],
                            callbacks: Optional[Dict] = None) -> None:
        self._draw_section_title("RECORDING")

        # --- Recording control ---
        pushed = self._push_font(self.font_body)
        self._draw_subsection("RECORD CONTROL")
        self._pop_font(pushed)

        pushed = self._push_font(self.font_body)

        if self._recording_active:
            # Recording duration timer
            elapsed = time.time() - (self._recording_start_time or time.time())
            h, rem = divmod(int(elapsed), 3600)
            m, s = divmod(rem, 60)
            timer_str = f"{h:02d}:{m:02d}:{s:02d}"

            # Pulsing red dot indicator
            draw_list = imgui.get_window_draw_list()
            cx, cy = imgui.get_cursor_screen_pos()
            line_h = imgui.get_text_line_height()
            pulse = 0.5 + 0.5 * math.sin(time.time() * 4.0)
            dot_color = imgui.get_color_u32_rgba(1.0, 0.15, 0.15, 0.6 + 0.4 * pulse)
            draw_list.add_circle_filled(cx + 8, cy + line_h / 2, 5, dot_color)
            imgui.dummy(20, line_h)
            imgui.same_line()

            pushed_mono = self._push_font(self.font_mono)
            imgui.text_colored(f"REC  {timer_str}", 1.0, 0.3, 0.3, 1.0)
            self._pop_font(pushed_mono)

            # File size estimate (based on bitrate * elapsed when no real data)
            size_bytes = self._recording_file_size_bytes
            if size_bytes == 0 and self._recording_start_time:
                bitrate_kbps = params.get("recording_bitrate", 5000)
                size_bytes = int(elapsed * bitrate_kbps * 1000 / 8)
            if size_bytes < 1024 * 1024:
                size_str = f"{size_bytes / 1024:.1f} KB"
            elif size_bytes < 1024 * 1024 * 1024:
                size_str = f"{size_bytes / (1024 * 1024):.1f} MB"
            else:
                size_str = f"{size_bytes / (1024 * 1024 * 1024):.2f} GB"
            self._draw_kv_row("File Size", size_str)

            imgui.spacing()

            # STOP button (red accent)
            imgui.push_style_color(imgui.COLOR_BUTTON, 0.6, 0.1, 0.1, 1.0)
            imgui.push_style_color(imgui.COLOR_BUTTON_HOVERED, 0.8, 0.15, 0.15, 1.0)
            imgui.push_style_color(imgui.COLOR_BUTTON_ACTIVE, 1.0, 0.2, 0.2, 1.0)
            if imgui.button("STOP RECORDING", 200, 36):
                self._recording_active = False
                self._recording_start_time = None
                self._recording_file_size_bytes = 0
                if on_change:
                    on_change("recording_enabled", False)
                if callbacks and "stop_recording" in callbacks:
                    callbacks["stop_recording"]()
            imgui.pop_style_color(3)
        else:
            pushed_mono = self._push_font(self.font_mono)
            imgui.text_colored("IDLE", *Theme.TEXT_SECONDARY)
            self._pop_font(pushed_mono)

            imgui.spacing()

            # START button (cyan accent)
            if imgui.button("START RECORDING", 200, 36):
                self._recording_active = True
                self._recording_start_time = time.time()
                self._recording_file_size_bytes = 0
                if on_change:
                    on_change("recording_enabled", True)
                if callbacks and "start_recording" in callbacks:
                    callbacks["start_recording"]()

        imgui.same_line(0, 16)

        # Screenshot button
        if imgui.button("SCREENSHOT (Ctrl+S)", 200, 36):
            if callbacks and "screenshot" in callbacks:
                callbacks["screenshot"]()

        imgui.spacing()

        # Open recording folder
        if imgui.button("OPEN RECORDINGS FOLDER", 200, 28):
            if callbacks and "open_recordings_folder" in callbacks:
                callbacks["open_recordings_folder"]()

        self._pop_font(pushed)

        imgui.spacing()
        imgui.separator()
        imgui.spacing()

        # --- Recording settings ---
        pushed = self._push_font(self.font_body)
        self._draw_subsection("SETTINGS")

        bitrate = params.get("recording_bitrate", 5000)
        changed, new_val = self._slider_int_with_hint(
            "Bitrate (kbps)##bitrate", bitrate, 1000, 20000
        )
        if changed and on_change:
            on_change("recording_bitrate", new_val)

        imgui.spacing()

        fmt = params.get("recording_format", 0)
        changed, new_val = self._animated_combo("Format##fmt", fmt, ["MP4 (H.264)", "MKV (H.264)", "AVI (RAW)"])
        if changed and on_change:
            on_change("recording_format", new_val)

        imgui.spacing()
        self._draw_subsection("CURRENT CONFIG")
        self._pop_font(pushed)

        self._draw_kv_row("Status", "RECORDING" if self._recording_active else "IDLE",
                          accent=self._recording_active)
        self._draw_kv_row("Bitrate", f"{params.get('recording_bitrate', 5000)} kbps")
        fmt_names = ["MP4", "MKV", "AVI"]
        self._draw_kv_row("Format", fmt_names[params.get("recording_format", 0)])

    # -------------------------------------------------------------------------
    # DIAGNOSTICS tab
    # -------------------------------------------------------------------------

    def _draw_diagnostics_tab(self, stats: Dict, live_status: Dict) -> None:
        self._draw_section_title("NETWORK DIAGNOSTICS")

        pushed = self._push_font(self.font_body)

        # --- Bandwidth section ---
        self._draw_subsection("BANDWIDTH")
        self._pop_font(pushed)

        uplink_kbps = stats.get("uplink_bandwidth_kbps", 0.0)
        downlink_kbps = stats.get("downlink_bandwidth_kbps", self._bandwidth_kbps)
        self._draw_kv_row("Uplink", f"{uplink_kbps:.1f} kbps")
        self._draw_kv_row("Downlink", f"{downlink_kbps:.1f} kbps")

        imgui.spacing()

        # --- Packet statistics ---
        pushed = self._push_font(self.font_body)
        self._draw_subsection("PACKET STATISTICS")
        self._pop_font(pushed)

        packets_sent = stats.get("packets_sent", 0)
        packets_received = stats.get("packets_received", 0)
        packets_lost = stats.get("packets_lost", 0)
        packets_retransmitted = stats.get("packets_retransmitted", 0)

        self._draw_kv_row("Packets Sent", f"{packets_sent}")
        self._draw_kv_row("Packets Received", f"{packets_received}")
        self._draw_kv_row("Packets Lost", f"{packets_lost}")
        self._draw_kv_row("Retransmitted", f"{packets_retransmitted}")

        imgui.spacing()

        # --- RTT statistics ---
        pushed = self._push_font(self.font_body)
        self._draw_subsection("LATENCY STATISTICS")
        self._pop_font(pushed)

        latency_min = stats.get("latency_min_ms", 0.0)
        latency_avg = live_status.get("latency_ms", 0.0)
        latency_max = stats.get("latency_max_ms", 0.0)

        self._draw_kv_row("Min RTT", f"{latency_min:.2f} ms")
        self._draw_kv_row("Avg RTT", f"{latency_avg:.2f} ms", accent=True)
        self._draw_kv_row("Max RTT", f"{latency_max:.2f} ms")

        imgui.spacing()

        # --- Codec statistics ---
        pushed = self._push_font(self.font_body)
        self._draw_subsection("CODEC STATISTICS")
        self._pop_font(pushed)

        encode_time_ms = stats.get("encode_time_ms", 0.0)
        decode_time_ms = stats.get("decode_time_ms", 0.0)
        buffer_frames = stats.get("buffer_frames", 0)
        keyframe_interval = stats.get("keyframe_interval", 0)

        self._draw_kv_row("Encode Time", f"{encode_time_ms:.1f} ms")
        self._draw_kv_row("Decode Time", f"{decode_time_ms:.1f} ms")
        self._draw_kv_row("Buffer Frames", f"{buffer_frames}")
        self._draw_kv_row("Keyframe Interval", f"{keyframe_interval}")

        imgui.spacing()

        # --- Error statistics ---
        pushed = self._push_font(self.font_body)
        self._draw_subsection("ERROR STATISTICS")
        self._pop_font(pushed)

        crc_errors = stats.get("crc_errors", 0)
        timeout_errors = stats.get("timeout_errors", 0)
        decode_errors = stats.get("decode_errors", 0)

        self._draw_kv_row("CRC Errors", f"{crc_errors}")
        self._draw_kv_row("Timeout Errors", f"{timeout_errors}")
        self._draw_kv_row("Decode Errors", f"{decode_errors}")

    # -------------------------------------------------------------------------
    # CONTROL SETTINGS tab
    # -------------------------------------------------------------------------

    def _draw_control_settings_tab(self, params: Dict, on_change: Optional[Callable],
                                    callbacks: Optional[Dict] = None) -> None:
        self._draw_section_title("CONTROL SETTINGS")

        pushed = self._push_font(self.font_body)

        # --- Keyboard mapping section ---
        self._draw_subsection("KEY BINDINGS")
        self._pop_font(pushed)

        binding_labels = {
            "toggle_ready": "Toggle Ready",
            "toggle_hud": "Toggle HUD",
            "toggle_console": "Console",
            "toggle_menu": "Menu",
        }

        for action, display_name in binding_labels.items():
            pushed_body = self._push_font(self.font_body)
            imgui.text_colored(display_name, *Theme.TEXT_SECONDARY)
            self._pop_font(pushed_body)
            imgui.same_line(160)

            is_rebinding = (self._rebinding_action == action)
            key_label = self._key_bindings.get(action, "?")

            if is_rebinding:
                btn_text = "Press any key..."
                imgui.push_style_color(imgui.COLOR_BUTTON, 0.3, 0.15, 0.0, 1.0)
                imgui.push_style_color(imgui.COLOR_BUTTON_HOVERED, 0.4, 0.2, 0.0, 1.0)
                imgui.push_style_color(imgui.COLOR_BUTTON_ACTIVE, 0.5, 0.25, 0.0, 1.0)
            else:
                btn_text = f"[{key_label.upper()}]"

            pushed_mono = self._push_font(self.font_mono)
            if imgui.button(f"{btn_text}##bind_{action}", 140, 28):
                if not is_rebinding:
                    self._rebinding_action = action
                    if callbacks and "start_key_capture" in callbacks:
                        callbacks["start_key_capture"]()
            self._pop_font(pushed_mono)

            if is_rebinding:
                imgui.pop_style_color(3)

        imgui.spacing()
        pushed = self._push_font(self.font_body)
        if imgui.button("Reset to Default", 150, 32):
            self._key_bindings = {
                "toggle_ready": "F5",
                "toggle_hud": "Tab",
                "toggle_console": "`",
                "toggle_menu": "Esc",
            }
            self._rebinding_action = None
        imgui.same_line()
        if imgui.button("Save Preset", 150, 32):
            if on_change:
                on_change("key_bindings", dict(self._key_bindings))
        self._pop_font(pushed)

        imgui.spacing()
        imgui.separator()
        imgui.spacing()

        # --- Gamepad section ---
        pushed = self._push_font(self.font_body)
        self._draw_subsection("GAMEPAD CONFIGURATION")
        self._pop_font(pushed)

        # Gamepad status
        gamepad_connected = False  # Would come from stats
        status_text = "Connected: Xbox One Controller" if gamepad_connected else "No Gamepad Connected"
        status_color = (0.0, 1.0, 0.5, 1.0) if gamepad_connected else (0.5, 0.5, 0.5, 1.0)

        pushed = self._push_font(self.font_body)
        imgui.text_colored("STATUS", *Theme.TEXT_SECONDARY)
        self._pop_font(pushed)
        imgui.same_line(160)
        pushed = self._push_font(self.font_mono)
        imgui.text_colored(status_text, *status_color)
        self._pop_font(pushed)

        imgui.spacing()

        # Stick mapping
        pushed = self._push_font(self.font_body)
        imgui.text_colored("STICK MAPPING", *Theme.TEXT_SECONDARY)
        self._pop_font(pushed)
        imgui.separator()
        imgui.spacing()

        self._draw_kv_row("Left Stick", "Movement")
        self._draw_kv_row("Right Stick", "Camera")

        imgui.spacing()

        # Button mapping
        pushed = self._push_font(self.font_body)
        imgui.text_colored("BUTTON MAPPING", *Theme.TEXT_SECONDARY)
        self._pop_font(pushed)
        imgui.separator()
        imgui.spacing()

        self._draw_kv_row("A Button", "Jump")
        self._draw_kv_row("B Button", "Crouch")
        self._draw_kv_row("X Button", "Action 1")
        self._draw_kv_row("Y Button", "Action 2")
        self._draw_kv_row("LT Trigger", "Brake")
        self._draw_kv_row("RT Trigger", "Accelerate")

        imgui.spacing()

        # Deadzone slider
        pushed = self._push_font(self.font_body)
        imgui.text_colored("DEADZONE", *Theme.TEXT_SECONDARY)
        self._pop_font(pushed)
        imgui.separator()
        imgui.spacing()

        deadzone = params.get("gamepad_deadzone", 0.15)
        imgui.set_next_item_width(300)
        changed, new_deadzone = imgui.slider_float("Deadzone##gamepad", deadzone, 0.0, 0.5, "%.2f")
        if changed and on_change:
            on_change("gamepad_deadzone", new_deadzone)

        imgui.spacing()

        # Vibration toggle
        vibration_enabled = params.get("gamepad_vibration", True)
        changed, new_vibration = imgui.checkbox("Enable Vibration Feedback", vibration_enabled)
        if changed and on_change:
            on_change("gamepad_vibration", new_vibration)

    # -------------------------------------------------------------------------
    # DEBUG tab
    # -------------------------------------------------------------------------

    def _draw_debug_tab(self, params: Dict, on_change: Optional[Callable],
                        stats: Dict, live_status: Dict) -> None:
        self._draw_section_title("DEBUG")

        pushed = self._push_font(self.font_body)

        show_perf = params.get("show_performance_graph", False)
        changed, new_val = imgui.checkbox("Show Performance Graph", show_perf)
        if changed and on_change:
            on_change("show_performance_graph", new_val)

        show_debug = params.get("show_debug_info", False)
        changed, new_val = imgui.checkbox("Show Debug Info", show_debug)
        if changed and on_change:
            on_change("show_debug_info", new_val)

        # --- Performance graphs ---
        if show_perf:
            imgui.spacing()
            self._draw_subsection("PERFORMANCE GRAPHS")
            self._pop_font(pushed)

            # Reorder ring buffer so index 0 = oldest sample
            n = self._perf_history_size
            wi = self._perf_write_idx % n

            fps_ordered = self._fps_history[wi:] + self._fps_history[:wi]
            lat_ordered = self._latency_history[wi:] + self._latency_history[:wi]

            fps_arr = array('f', fps_ordered)
            lat_arr = array('f', lat_ordered)

            graph_w = imgui.get_content_region_available_width()
            graph_h = 80

            # FPS graph
            current_fps = live_status.get("fps", 0.0)
            imgui.push_style_color(imgui.COLOR_PLOT_LINES, *Theme.ACCENT_PRIMARY)
            imgui.plot_lines(
                f"##fps_graph",
                fps_arr,
                overlay_text=f"FPS: {current_fps:.1f}",
                scale_min=0.0,
                scale_max=max(max(fps_arr) * 1.2, 1.0),
                graph_size=(graph_w, graph_h),
            )
            imgui.pop_style_color()

            imgui.spacing()

            # Latency graph
            current_lat = live_status.get("latency_ms", 0.0)
            imgui.push_style_color(imgui.COLOR_PLOT_LINES, 1.0, 0.65, 0.0, 1.0)
            imgui.plot_lines(
                f"##latency_graph",
                lat_arr,
                overlay_text=f"Latency: {current_lat:.2f} ms",
                scale_min=0.0,
                scale_max=max(max(lat_arr) * 1.2, 1.0),
                graph_size=(graph_w, graph_h),
            )
            imgui.pop_style_color()

            pushed = self._push_font(self.font_body)

        imgui.spacing()
        self._draw_subsection("NETWORK")
        self._pop_font(pushed)

        latency = live_status.get("latency_ms", 0.0)
        loss = live_status.get("packet_loss_rate", 0.0)
        cmds = stats.get("commands_sent", 0)
        hb = stats.get("heartbeats_sent", 0)
        bytes_rx = stats.get("bytes_received", 0)

        latency_accent = latency > 0 and latency < 50
        loss_bad = loss > 0.01
        self._draw_kv_row("Latency", f"{latency:.2f} ms", accent=latency_accent)
        self._draw_kv_row("Packet Loss",
                          f"{loss:.2%}",
                          accent=loss_bad)
        self._draw_kv_row("Commands Sent", f"{cmds}")
        self._draw_kv_row("Heartbeats Sent", f"{hb}")
        self._draw_kv_row("Bytes Received", f"{bytes_rx / 1024:.1f} KB")

        pushed = self._push_font(self.font_body)
        imgui.spacing()
        self._draw_subsection("RENDER")
        self._pop_font(pushed)

        fps = live_status.get("fps", 0.0)
        frames = live_status.get("frames_received", 0)
        self._draw_kv_row("Render FPS", f"{fps:.1f}", accent=fps > 0)
        self._draw_kv_row("Frames Total", f"{frames}")

        # --- System resources ---
        pushed = self._push_font(self.font_body)
        imgui.spacing()
        self._draw_subsection("SYSTEM RESOURCES")
        self._pop_font(pushed)

        now = time.time()
        if now - self._sys_res_last_update > 1.0:
            self._sys_res_last_update = now
            self._sys_res_cpu = psutil.cpu_percent(interval=0)
            mem = psutil.virtual_memory()
            self._sys_res_mem_percent = mem.percent
            self._sys_res_mem_used = mem.used
            self._sys_res_mem_total = mem.total
            proc = psutil.Process(os.getpid())
            self._sys_res_proc_mem = proc.memory_info().rss

        self._draw_kv_row("CPU Usage", f"{self._sys_res_cpu:.1f}%",
                          accent=self._sys_res_cpu > 80)
        self._draw_kv_row("Memory",
                          f"{self._sys_res_mem_used / (1024**3):.1f} / "
                          f"{self._sys_res_mem_total / (1024**3):.1f} GB "
                          f"({self._sys_res_mem_percent:.0f}%)",
                          accent=self._sys_res_mem_percent > 80)
        self._draw_kv_row("Process Memory",
                          f"{self._sys_res_proc_mem / (1024**2):.1f} MB")

    # -------------------------------------------------------------------------
    # ABOUT tab
    # -------------------------------------------------------------------------

    def _draw_about_tab(self) -> None:
        self._draw_section_title("ABOUT")

        pushed = self._push_font(self.font_body)

        imgui.spacing()
        self._pop_font(pushed)

        # Project name in title font
        pushed_t = self._push_font(self.font_title)
        imgui.text_colored("PIP-Link", *Theme.ACCENT_PRIMARY)
        self._pop_font(pushed_t)

        pushed = self._push_font(self.font_body)
        imgui.text_colored("Remote Flight Control Client", *Theme.TEXT_SECONDARY)
        imgui.spacing()
        imgui.spacing()

        self._draw_subsection("PROJECT INFO")
        self._pop_font(pushed)

        self._draw_kv_row("Version", "0.1.0-dev")
        self._draw_kv_row("License", "MIT")
        self._draw_kv_row("Platform", "Windows / Linux")

        pushed = self._push_font(self.font_body)
        imgui.spacing()
        self._draw_subsection("TECHNOLOGY STACK")
        self._pop_font(pushed)

        self._draw_kv_row("UI Framework", "Dear ImGui (pyimgui)")
        self._draw_kv_row("Rendering", "Pygame + OpenGL")
        self._draw_kv_row("Networking", "UDP / mDNS (zeroconf)")
        self._draw_kv_row("Input", "pynput + pygame")
        self._draw_kv_row("Target FPS", f"{Config.TARGET_FPS}")

        pushed = self._push_font(self.font_body)
        imgui.spacing()
        self._draw_subsection("RUNTIME")
        self._pop_font(pushed)

        import sys
        self._draw_kv_row("Python", sys.version.split()[0])
        self._draw_kv_row("ImGui", imgui.get_version())
        self._draw_kv_row("OS", f"{os.name} ({os.sys.platform})")

    # -------------------------------------------------------------------------
    # No signal screen (shown when no video frame)
    # -------------------------------------------------------------------------

    def draw_no_signal(self):
        """Draw NO SIGNAL overlay with grey background and live clock."""
        disp = imgui.get_io().display_size
        w = float(disp[0])
        h = float(disp[1])
        draw_list = imgui.get_background_draw_list()

        # Dark grey background
        bg = imgui.get_color_u32_rgba(0.12, 0.12, 0.14, 1.0)
        draw_list.add_rect_filled(0, 0, w, h, bg)

        # Subtle scan-line effect: thin horizontal lines
        line_col = imgui.get_color_u32_rgba(0.15, 0.15, 0.17, 0.4)
        y = 0.0
        while y < h:
            draw_list.add_line(0, y, w, y, line_col, 1.0)
            y += 4.0

        # "NO SIGNAL" — large, centered
        pushed_title = self._push_font(self.font_title)
        ns_text = "NO SIGNAL"
        ns_size = imgui.calc_text_size(ns_text)
        ns_x = (w - ns_size.x) / 2
        ns_y = h * 0.42 - ns_size.y / 2

        # Pulsing alpha (slow breathe, period ~3s)
        pulse = 0.45 + 0.55 * (0.5 + 0.5 * math.sin(time.time() * 2.1))
        ns_col = imgui.get_color_u32_rgba(0.55, 0.57, 0.62, pulse)
        draw_list.add_text(ns_x, ns_y, ns_col, ns_text)
        self._pop_font(pushed_title)

        # Current time — below NO SIGNAL, mono font
        pushed_mono = self._push_font(self.font_mono)
        now = time.localtime()
        time_str = time.strftime("%H:%M:%S", now)
        t_size = imgui.calc_text_size(time_str)
        t_x = (w - t_size.x) / 2
        t_y = ns_y + ns_size.y + 18
        t_col = imgui.get_color_u32_rgba(0.40, 0.42, 0.48, 0.85)
        draw_list.add_text(t_x, t_y, t_col, time_str)

        # Date line
        date_str = time.strftime("%Y-%m-%d", now)
        d_size = imgui.calc_text_size(date_str)
        d_x = (w - d_size.x) / 2
        d_y = t_y + t_size.y + 6
        d_col = imgui.get_color_u32_rgba(0.32, 0.34, 0.38, 0.7)
        draw_list.add_text(d_x, d_y, d_col, date_str)
        self._pop_font(pushed_mono)

    # -------------------------------------------------------------------------
    # Status bar (shown when menu is closed)
    # -------------------------------------------------------------------------

    def draw_status_bar(self, status: Dict) -> None:
        # HUD alpha 控制（与 input_hud 共享 _hud_alpha）
        if self._hud_alpha < 0.01:
            return

        bar_w = 260
        bar_h = 145
        disp_w, disp_h = imgui.get_io().display_size
        # 右侧滑出：alpha=1 时正常位置，alpha→0 时滑出右侧
        offset_x = (1.0 - self._hud_alpha) * (bar_w + 16)
        imgui.set_next_window_position(
            disp_w - bar_w - 16 + offset_x,
            disp_h - bar_h - 16,
            imgui.ALWAYS
        )
        imgui.set_next_window_size(bar_w, bar_h, imgui.ALWAYS)

        a = self._hud_alpha
        imgui.push_style_color(imgui.COLOR_WINDOW_BACKGROUND, 0.06, 0.06, 0.09, 0.85 * a)
        expanded, _ = imgui.begin(
            "##status", False,
            imgui.WINDOW_NO_TITLE_BAR | imgui.WINDOW_NO_MOVE |
            imgui.WINDOW_NO_RESIZE | imgui.WINDOW_NO_SCROLLBAR
        )

        if expanded:
            fps = status.get("fps", 0.0)
            latency = status.get("latency_ms", 0.0)
            loss = status.get("packet_loss_rate", 0.0)
            frames = status.get("frames_received", 0)

            self._draw_label_value("FPS", f"{fps:.1f}", accent=True, right_align=True)
            self._draw_label_value("LATENCY", f"{latency:.2f} ms", right_align=True)
            self._draw_label_value("LOSS", f"{loss:.2%}",
                                   accent=(loss > 0.01), right_align=True)
            self._draw_label_value("FRAMES", f"{frames}", right_align=True)

        imgui.end()
        imgui.pop_style_color()

    # -------------------------------------------------------------------------
    # READY indicator (bottom-left, always visible)
    # -------------------------------------------------------------------------

    def draw_ready_indicator(self):
        """左下角 READY/NOT READY 指示器 — 阻尼弹簧缩放 + 呼吸灯"""
        now = time.time()

        # 检测状态切换，记录切换时刻
        if self.is_ready != self._ready_anim_dir:
            self._ready_anim_dir = self.is_ready
            self._ready_toggle_time = now

        # 阻尼弹簧: scale = 1 + A * exp(-zeta * t) * sin(omega * t)
        t = now - self._ready_toggle_time
        amp = 0.7
        zeta = 5.0
        omega = 14.0
        if t < 2.0 and self._ready_toggle_time > 0:
            spring = amp * math.exp(-zeta * t) * math.sin(omega * t)
        else:
            spring = 0.0
        scale = 1.0 + spring

        disp_w, disp_h = imgui.get_io().display_size
        draw_list = imgui.get_foreground_draw_list()

        # 文字高度用于垂直居中
        text_h = imgui.get_text_line_height()

        # 基础参数
        base_radius = 8.0
        radius = base_radius * scale
        margin_x = 20.0
        margin_y = 16.0  # 底边距
        cx = margin_x + base_radius
        cy = disp_h - margin_y - text_h / 2  # 圆点垂直居中于文字行

        if self.is_ready:
            glow_alpha = 0.15 + 0.1 * (0.5 + 0.5 * math.sin(now * 2.0))
            glow_col = imgui.get_color_u32_rgba(0.1, 0.95, 0.4, glow_alpha)
            draw_list.add_circle_filled(cx, cy, radius * 2.2, glow_col)

            dot_col = imgui.get_color_u32_rgba(0.1, 0.95, 0.4, 1.0)
            draw_list.add_circle_filled(cx, cy, radius, dot_col)

            text = "READY"
            text_col = imgui.get_color_u32_rgba(0.1, 0.95, 0.4, 0.95)
        else:
            pulse = 0.3 + 0.7 * (0.5 + 0.5 * math.sin(now * 4.0))

            glow_col = imgui.get_color_u32_rgba(0.8, 0.3, 0.2, pulse * 0.2)
            draw_list.add_circle_filled(cx, cy, radius * 2.0, glow_col)

            dot_col = imgui.get_color_u32_rgba(0.8, 0.3, 0.2, pulse)
            draw_list.add_circle_filled(cx, cy, radius, dot_col)

            text = "NOT READY  [F5]"
            text_col = imgui.get_color_u32_rgba(0.7, 0.35, 0.25, 0.4 + pulse * 0.5)

        # 文字：圆点右侧，垂直居中
        text_x = cx + base_radius + 12
        text_y = cy - text_h / 2
        draw_list.add_text(text_x, text_y, text_col, text)

    # ------------------------------------------------------------------
    # Input HUD
    # ------------------------------------------------------------------

    _BIT_NAMES = {
        0: "ESC", 1: "F1", 2: "F2", 3: "F3", 4: "F4", 6: "F6", 7: "F7",
        8: "F8", 9: "F9", 10: "F10", 11: "F11", 12: "F12",
        13: "`", 14: "1", 15: "2", 16: "3", 17: "4", 18: "5",
        19: "6", 20: "7", 21: "8", 22: "9", 23: "0",
        24: "-", 25: "=", 26: "BS", 27: "TAB",
        28: "Q", 29: "W", 30: "E", 31: "R", 32: "T", 33: "Y",
        34: "U", 35: "I", 36: "O", 37: "P", 38: "[", 39: "]",
        40: "\\", 41: "CAPS", 42: "A", 43: "S", 44: "D", 45: "F",
        46: "G", 47: "H", 48: "J", 49: "K", 50: "L", 51: ";",
        52: "'", 53: "ENT", 54: "LSHF", 55: "Z", 56: "X", 57: "C",
        58: "V", 59: "B", 60: "N", 61: "M", 62: ",", 63: ".",
        64: "/", 65: "RSHF", 66: "LCTL", 67: "LALT",
        68: "SPC", 69: "RALT", 70: "RCTL",
    }

    def draw_input_hud(self, keyboard_state: bytes, mouse_dx: int, mouse_dy: int,
                       mouse_buttons: tuple, scroll_delta: int = 0,
                       side_buttons: tuple = (False, False)):
        """左侧居中 输入可视化 HUD（TAB 切换显隐）"""
        now = time.time()
        dt = min(now - getattr(self, '_hud_last_t', now), 0.1)
        self._hud_last_t = now

        # HUD alpha 动画（指数衰减）
        target = 1.0 if self.show_hud else 0.0
        diff = target - self._hud_alpha
        if abs(diff) > 0.01:
            t = 1.0 - math.exp(-dt / 0.12)
            self._hud_alpha += diff * t
        else:
            self._hud_alpha = target
        if self._hud_alpha < 0.01:
            return

        disp_w, disp_h = imgui.get_io().display_size
        draw_list = imgui.get_foreground_draw_list()

        # --- 面板布局 ---
        panel_w = 320.0
        margin = 16.0
        pad = 16.0

        title_h = 40.0
        stick_size = 120.0
        mouse_shape_h = stick_size
        mouse_row_h = stick_size + 28.0
        sep_h = 20.0
        kbd_label_h = 24.0
        kbd_tag_h = 24.0
        kbd_gap_y = 5.0
        kbd_max_rows = 3
        kbd_area_h = kbd_tag_h * kbd_max_rows + kbd_gap_y * (kbd_max_rows - 1)

        panel_h = title_h + mouse_row_h + sep_h + kbd_label_h + kbd_area_h + pad

        # 水平滑出动画：alpha=1 时在 margin，alpha=0 时滑出屏幕左侧
        px = margin - (1.0 - self._hud_alpha) * (panel_w + margin)
        py = (disp_h - panel_h) / 2.0

        # --- 背景 ---
        a = self._hud_alpha
        bg = imgui.get_color_u32_rgba(0.06, 0.06, 0.09, 0.78 * a)
        draw_list.add_rect_filled(px, py, px + panel_w, py + panel_h, bg, 6.0)
        border_col = imgui.get_color_u32_rgba(0.18, 0.20, 0.25, 0.5 * a)
        draw_list.add_rect(px, py, px + panel_w, py + panel_h, border_col, 6.0)

        # --- 标题 ---
        title_col = imgui.get_color_u32_rgba(0.50, 0.52, 0.58, 1.0)
        draw_list.add_text(px + pad, py + 10, title_col, "INPUT")

        # --- 鼠标区域 ---
        row_y = py + title_h

        stick_x = px + pad
        stick_y = row_y
        self._draw_mouse_stick(draw_list, stick_x, stick_y, stick_size,
                               mouse_dx, mouse_dy, dt)

        btn_area_x = stick_x + stick_size + 80
        btn_area_y = row_y
        self._draw_mouse_buttons(draw_list, btn_area_x, btn_area_y,
                                 mouse_buttons, mouse_shape_h,
                                 scroll_delta, side_buttons)

        # --- 分隔线 ---
        sep_y = row_y + mouse_row_h + sep_h / 2
        sep_col = imgui.get_color_u32_rgba(0.22, 0.24, 0.30, 0.4)
        draw_list.add_line(px + pad, sep_y, px + panel_w - pad, sep_y, sep_col)

        # --- 键盘区域 ---
        kbd_y = sep_y + sep_h / 2 - 2
        kbd_label_col = imgui.get_color_u32_rgba(0.50, 0.52, 0.58, 1.0)
        draw_list.add_text(px + pad, kbd_y, kbd_label_col, "KEYBOARD")

        tags_y = kbd_y + kbd_label_h + 2
        tags_x = px + pad
        tags_max_x = px + panel_w - pad
        tags_max_y = py + panel_h

        draw_list.push_clip_rect(tags_x, tags_y, tags_max_x, tags_max_y, True)
        self._draw_keyboard_tags(draw_list, tags_x, tags_y, tags_max_x,
                                 keyboard_state, kbd_max_rows)
        draw_list.pop_clip_rect()

    def _draw_mouse_stick(self, draw_list, x, y, size, dx, dy, dt):
        """鼠标摇杆 — 圆角矩形 + 虚线十字 + 圆点"""
        half = size / 2.0
        cx = x + half
        cy = y + half

        bg = imgui.get_color_u32_rgba(0.04, 0.04, 0.06, 0.9)
        draw_list.add_rect_filled(x, y, x + size, y + size, bg, 5.0)
        border = imgui.get_color_u32_rgba(0.22, 0.24, 0.30, 0.8)
        draw_list.add_rect(x, y, x + size, y + size, border, 5.0)

        dash_col = imgui.get_color_u32_rgba(0.35, 0.38, 0.45, 0.35)
        self._draw_dashed_line(draw_list, x + 6, cy, x + size - 6, cy, dash_col)
        self._draw_dashed_line(draw_list, cx, y + 6, cx, y + size - 6, dash_col)

        max_speed = 25.0
        target_x = max(-1.0, min(1.0, dx / max_speed))
        target_y = max(-1.0, min(1.0, dy / max_speed))

        if dx != 0 or dy != 0:
            t = 1.0 - math.exp(-dt / 0.04)
            self._stick_x += (target_x - self._stick_x) * t
            self._stick_y += (target_y - self._stick_y) * t
        else:
            decay = math.exp(-dt / 0.06)
            self._stick_x *= decay
            self._stick_y *= decay

        if abs(self._stick_x) < 0.01:
            self._stick_x = 0.0
        if abs(self._stick_y) < 0.01:
            self._stick_y = 0.0

        dot_x = cx + self._stick_x * (half - 8)
        dot_y = cy + self._stick_y * (half - 8)

        if abs(self._stick_x) > 0.02 or abs(self._stick_y) > 0.02:
            trail_col = imgui.get_color_u32_rgba(0.0, 0.85, 1.0, 0.12)
            draw_list.add_line(cx, cy, dot_x, dot_y, trail_col, 2.0)

        intensity = min(1.0, math.sqrt(self._stick_x**2 + self._stick_y**2))
        if intensity > 0.05:
            glow = imgui.get_color_u32_rgba(0.0, 0.85, 1.0, intensity * 0.18)
            draw_list.add_circle_filled(dot_x, dot_y, 9.0, glow)

        dot_col = imgui.get_color_u32_rgba(0.0, 0.85, 1.0, 0.9)
        draw_list.add_circle_filled(dot_x, dot_y, 5.5, dot_col)

        # 速度数值 — 摇杆正下方
        val_y = y + size + 6
        val_text = f"{self._stick_x:+.2f} / {self._stick_y:+.2f}"
        dim = imgui.get_color_u32_rgba(0.40, 0.42, 0.48, 0.7)
        tw = imgui.calc_text_size(val_text).x
        draw_list.add_text(x + (size - tw) / 2, val_y, dim, val_text)

    def _draw_mouse_buttons(self, draw_list, x, y, buttons, area_h,
                            scroll_delta=0, side_buttons=(False, False)):
        """鼠标按键 — 模拟鼠标外形 + 滚轮方向 + 侧键"""
        mouse_w = 72.0
        mouse_h = area_h - 22.0
        r = 10.0

        outline_col = imgui.get_color_u32_rgba(0.22, 0.24, 0.30, 0.6)
        inner_bg = imgui.get_color_u32_rgba(0.05, 0.05, 0.07, 0.8)
        draw_list.add_rect_filled(x, y, x + mouse_w, y + mouse_h, inner_bg, r)
        draw_list.add_rect(x, y, x + mouse_w, y + mouse_h, outline_col, r)

        # LMB(左半) | RMB(右半) — 上部 40%
        btn_h = mouse_h * 0.40
        mid_x = x + mouse_w / 2

        div_col = imgui.get_color_u32_rgba(0.25, 0.28, 0.35, 0.5)
        draw_list.add_line(mid_x, y + 6, mid_x, y + btn_h, div_col)

        # LMB
        lmb = buttons[0] if len(buttons) > 0 else False
        if lmb:
            fill = imgui.get_color_u32_rgba(0.0, 0.85, 1.0, 0.7)
            draw_list.add_rect_filled(x + 3, y + 3, mid_x - 1, y + btn_h, fill, 7.0)
        lc = imgui.get_color_u32_rgba(0.9, 0.95, 1.0, 0.9) if lmb \
            else imgui.get_color_u32_rgba(0.45, 0.48, 0.55, 0.8)
        ltw = imgui.calc_text_size("L").x
        draw_list.add_text(x + (mouse_w / 2 - ltw) / 2, y + btn_h / 2 - 8, lc, "L")

        # RMB
        rmb = buttons[2] if len(buttons) > 2 else False
        if rmb:
            fill = imgui.get_color_u32_rgba(0.0, 0.85, 1.0, 0.7)
            draw_list.add_rect_filled(mid_x + 1, y + 3, x + mouse_w - 3,
                                      y + btn_h, fill, 7.0)
        rc = imgui.get_color_u32_rgba(0.9, 0.95, 1.0, 0.9) if rmb \
            else imgui.get_color_u32_rgba(0.45, 0.48, 0.55, 0.8)
        rtw = imgui.calc_text_size("R").x
        draw_list.add_text(mid_x + (mouse_w / 2 - rtw) / 2, y + btn_h / 2 - 8, rc, "R")

        # MMB 滚轮 + 方向指示
        wheel_y = y + btn_h + 6
        wheel_h = 22.0
        wheel_w = 14.0
        wheel_x = mid_x - wheel_w / 2
        mmb = buttons[1] if len(buttons) > 1 else False
        if mmb:
            wfill = imgui.get_color_u32_rgba(0.0, 0.85, 1.0, 0.7)
        else:
            wfill = imgui.get_color_u32_rgba(0.20, 0.22, 0.28, 0.8)
        draw_list.add_rect_filled(wheel_x, wheel_y, wheel_x + wheel_w,
                                  wheel_y + wheel_h, wfill, 4.0)
        wborder = imgui.get_color_u32_rgba(0.30, 0.33, 0.40, 0.6)
        draw_list.add_rect(wheel_x, wheel_y, wheel_x + wheel_w,
                           wheel_y + wheel_h, wborder, 4.0)

        # 滚轮方向箭头
        arrow_col = imgui.get_color_u32_rgba(0.0, 0.85, 1.0, 0.9)
        if scroll_delta > 0:
            # 上箭头
            ay = wheel_y - 6
            draw_list.add_triangle_filled(
                mid_x, ay - 4, mid_x - 5, ay + 3, mid_x + 5, ay + 3, arrow_col)
        elif scroll_delta < 0:
            # 下箭头
            ay = wheel_y + wheel_h + 6
            draw_list.add_triangle_filled(
                mid_x, ay + 4, mid_x - 5, ay - 3, mid_x + 5, ay - 3, arrow_col)

        # 侧键 — 鼠标左侧两个小按钮
        side_w = 10.0
        side_h = 16.0
        side_gap = 4.0
        side_x = x - side_w - 4
        side_y0 = y + mouse_h * 0.35
        for i, pressed in enumerate(side_buttons):
            sy = side_y0 + i * (side_h + side_gap)
            if pressed:
                sf = imgui.get_color_u32_rgba(0.0, 0.85, 1.0, 0.7)
            else:
                sf = imgui.get_color_u32_rgba(0.15, 0.16, 0.20, 0.8)
            draw_list.add_rect_filled(side_x, sy, side_x + side_w, sy + side_h, sf, 3.0)
            sb = imgui.get_color_u32_rgba(0.25, 0.28, 0.35, 0.6)
            draw_list.add_rect(side_x, sy, side_x + side_w, sy + side_h, sb, 3.0)
            # 标签 M4/M5 — 垂直居中对齐
            label = f"M{i + 4}"
            lc2 = imgui.get_color_u32_rgba(0.40, 0.42, 0.48, 0.7)
            lw2 = imgui.calc_text_size(label).x
            lh2 = imgui.calc_text_size(label).y
            draw_list.add_text(side_x - lw2 - 3, sy + (side_h - lh2) / 2, lc2, label)

        # MOUSE 标签 — 鼠标外形下方
        label_col = imgui.get_color_u32_rgba(0.40, 0.42, 0.48, 0.7)
        label = "MOUSE"
        lw = imgui.calc_text_size(label).x
        draw_list.add_text(x + (mouse_w - lw) / 2, y + mouse_h + 4, label_col, label)

    def _draw_keyboard_tags(self, draw_list, start_x, start_y, max_x,
                            keyboard_state: bytes, max_rows: int = 2):
        """键盘按键 tags — 流式布局，限制行数，小号字体"""
        pressed = []
        for byte_idx, byte_val in enumerate(keyboard_state):
            if byte_val == 0:
                continue
            for bit in range(8):
                if byte_val & (1 << bit):
                    bit_index = byte_idx * 8 + bit
                    name = self._BIT_NAMES.get(bit_index)
                    if name:
                        pressed.append(name)

        pushed = self._push_font(self.font_body)

        if not pressed:
            dim = imgui.get_color_u32_rgba(0.35, 0.38, 0.42, 0.5)
            draw_list.add_text(start_x, start_y + 2, dim, "---")
            self._pop_font(pushed)
            return

        tag_h = 22.0
        pad_x = 7.0
        gap_x = 4.0
        gap_y = 4.0
        x_cursor = start_x
        y_cursor = start_y
        row = 0

        for name in pressed:
            tw = imgui.calc_text_size(name).x
            tag_w = tw + pad_x * 2

            if x_cursor + tag_w > max_x and x_cursor > start_x:
                row += 1
                if row >= max_rows:
                    more = imgui.get_color_u32_rgba(0.50, 0.52, 0.58, 0.8)
                    draw_list.add_text(x_cursor, y_cursor + 3, more, "...")
                    self._pop_font(pushed)
                    return
                x_cursor = start_x
                y_cursor += tag_h + gap_y

            fill = imgui.get_color_u32_rgba(0.0, 0.75, 0.95, 0.85)
            draw_list.add_rect_filled(x_cursor, y_cursor,
                                      x_cursor + tag_w, y_cursor + tag_h,
                                      fill, 3.0)
            text_col = imgui.get_color_u32_rgba(0.02, 0.05, 0.08, 1.0)
            th = imgui.calc_text_size(name).y
            draw_list.add_text(x_cursor + pad_x, y_cursor + (tag_h - th) / 2,
                               text_col, name)

            x_cursor += tag_w + gap_x

        self._pop_font(pushed)

    @staticmethod
    def _draw_dashed_line(draw_list, x0, y0, x1, y1, color, dash=4.0, gap=3.0):
        """绘制虚线"""
        dx = x1 - x0
        dy = y1 - y0
        length = math.sqrt(dx * dx + dy * dy)
        if length < 1.0:
            return
        nx = dx / length
        ny = dy / length
        pos = 0.0
        while pos < length:
            end = min(pos + dash, length)
            draw_list.add_line(
                x0 + nx * pos, y0 + ny * pos,
                x0 + nx * end, y0 + ny * end,
                color, 1.0
            )
            pos = end + gap

    def on_key_captured(self, pygame_key: int, key_name: str):
        """Called by input handler when a key is captured during rebinding."""
        if self._rebinding_action and self._rebinding_action in self._key_bindings:
            # Capitalize display name
            display = key_name.capitalize() if len(key_name) > 1 else key_name.upper()
            self._key_bindings[self._rebinding_action] = display
            self._rebinding_action = None

    def update_perf_history(self, fps: float, latency: float):
        """Push new samples into ring buffers for performance graphs."""
        idx = self._perf_write_idx % self._perf_history_size
        self._fps_history[idx] = fps
        self._latency_history[idx] = latency
        self._perf_write_idx += 1

    def _draw_state_indicator(self, state: str) -> None:
        color = Theme.STATE_COLORS.get(state, Theme.STATE_COLORS["idle"])
        r, g, b, a = color
        draw_list = imgui.get_window_draw_list()
        cx, cy = imgui.get_cursor_screen_pos()
        radius = 6.0
        # center vertically on current line height
        line_h = imgui.get_text_line_height()
        cy += line_h / 2
        cx += radius
        packed = imgui.get_color_u32_rgba(r, g, b, a)
        draw_list.add_circle_filled(cx, cy, radius, packed)
        # advance cursor by the circle width + small gap
        imgui.dummy(radius * 2 + 2, line_h)
