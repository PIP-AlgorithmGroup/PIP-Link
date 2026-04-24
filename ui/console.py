"""CS2 风格开发者控制台"""

import time
import math
import imgui

from core.command import CommandRegistry, CommandResult


# 日志级别颜色
_LEVEL_COLORS = {
    "debug":  (0.45, 0.45, 0.50, 1.0),
    "info":   (0.78, 0.80, 0.85, 1.0),
    "warn":   (1.0,  0.85, 0.0,  1.0),
    "error":  (1.0,  0.3,  0.3,  1.0),
    "system": (0.0,  0.85, 1.0,  1.0),
}

_LEVEL_RULES = [
    (("error", "Error", "ERROR", "Traceback", "Exception"), "error"),
    (("warn", "Warn", "WARN", "Warning"), "warn"),
    (("[App]", "[Session", "[ServiceDiscovery]", "[VideoReceiver]",
      "[ControlSender]", "[HeartbeatManager]", "[StatusMonitor]",
      "[KeyboardEncoder]",
      "Started", "Stopped", "Starting", "Stopping", "Loaded",
      "Connected", "Disconnected", "Service found"), "system"),
]


def _detect_level(text: str) -> str:
    for keywords, level in _LEVEL_RULES:
        for kw in keywords:
            if kw in text:
                return level
    return "info"


class GameConsole:
    """CS2 风格开发者控制台 — 顶部滑下的 overlay"""

    def __init__(self, font_mono, font_body):
        self.visible = False
        self._lines: list[tuple[str, str]] = []   # (text, level)
        self._max_lines = 500
        self._anim_h = 0.0
        self._last_frame_time = time.time()

        # 输入框
        self._input_buf = ""
        self._cursor_pos = 0                # 追踪 ImGui 内部光标位置
        self._last_cursor_pos = 0           # 上一帧光标位置，用于检测移动
        self._cursor_moved_time = 0.0       # 最后一次移动时间
        self._history: list[str] = []
        self._history_idx = -1
        self._input_focus_next = False

        self._scroll_to_bottom = False
        self._log_scroll_y = 0.0        # 日志区手动滚动位置

        self.font_mono = font_mono
        self.font_body = font_body

        # 动画参数
        self._slide_tau = 0.08

        # 拖动调节高度
        self._user_height_ratio = 0.55
        self._dragging = False

        # 指令注册表
        self.commands = CommandRegistry()

        # 预绑定 callback
        self._cmd_cb = self._on_cmd_cb

    # ------------------------------------------------------------------
    # Callback：追踪光标位置 + 历史导航
    # ------------------------------------------------------------------

    def _on_cmd_cb(self, data):
        # 每帧同步光标位置
        self._cursor_pos = data.cursor_pos

        # 检测光标移动
        if self._cursor_pos != self._last_cursor_pos:
            self._cursor_moved_time = time.time()
        self._last_cursor_pos = self._cursor_pos

        key = data.event_key
        if key == imgui.KEY_UP_ARROW and self._history:
            self._history_idx = min(
                self._history_idx + 1, len(self._history) - 1)
            hist = self._history[-(self._history_idx + 1)]
            data.delete_chars(0, data.buffer_text_length)
            data.insert_chars(0, hist)
            data.cursor_pos = len(hist)
        elif key == imgui.KEY_DOWN_ARROW:
            if self._history_idx > 0:
                self._history_idx -= 1
                hist = self._history[-(self._history_idx + 1)]
                data.delete_chars(0, data.buffer_text_length)
                data.insert_chars(0, hist)
                data.cursor_pos = len(hist)
            else:
                self._history_idx = -1
                data.delete_chars(0, data.buffer_text_length)
        return 0

    # ------------------------------------------------------------------
    # 公共 API
    # ------------------------------------------------------------------

    def toggle(self):
        self.visible = not self.visible
        if self.visible:
            self._input_focus_next = True

    def log(self, text: str, level: str | None = None):
        if level is None:
            level = _detect_level(text)
        self._lines.append((text, level))
        if len(self._lines) > self._max_lines:
            self._lines = self._lines[-self._max_lines:]
        self._scroll_to_bottom = True

    # ------------------------------------------------------------------
    # 渲染
    # ------------------------------------------------------------------

    def draw(self):
        now = time.time()
        dt = min(now - self._last_frame_time, 0.1)
        self._last_frame_time = now

        io = imgui.get_io()
        disp_w, disp_h = io.display_size
        target_h = disp_h * self._user_height_ratio if self.visible else 0.0

        diff = target_h - self._anim_h
        if abs(diff) > 0.5:
            t = 1.0 - math.exp(-dt / self._slide_tau)
            self._anim_h += diff * t
        else:
            self._anim_h = target_h

        if self._anim_h < 1.0:
            self._anim_h = 0.0
            return

        w = disp_w
        h = self._anim_h
        pad_x = 14.0
        pad_y = 6.0
        input_h = 36.0
        sep_y = h - pad_y - input_h

        mx, my = io.mouse_pos

        # --- 拖动手柄 ---
        drag_zone = (0 <= mx <= w and h - 5 <= my <= h + 5)
        if drag_zone and io.mouse_down[0] and not self._dragging:
            self._dragging = True
        if self._dragging:
            if io.mouse_down[0]:
                new_h = max(100, min(my, disp_h * 0.85))
                self._user_height_ratio = new_h / disp_h
            else:
                self._dragging = False

        # === ImGui 窗口：仅用于输入捕获（透明，不绘制任何内容）===
        imgui.set_next_window_position(0, 0)
        imgui.set_next_window_size(w, h)
        imgui.push_style_var(imgui.STYLE_WINDOW_PADDING, (0, 0))
        imgui.push_style_var(imgui.STYLE_WINDOW_BORDERSIZE, 0.0)
        imgui.push_style_color(imgui.COLOR_WINDOW_BACKGROUND, 0, 0, 0, 0)
        imgui.begin(
            "##console_root",
            flags=(imgui.WINDOW_NO_TITLE_BAR | imgui.WINDOW_NO_RESIZE
                   | imgui.WINDOW_NO_MOVE | imgui.WINDOW_NO_SCROLLBAR
                   | imgui.WINDOW_NO_SAVED_SETTINGS
                   | imgui.WINDOW_NO_SCROLL_WITH_MOUSE
                   | imgui.WINDOW_NO_NAV),
        )

        # --- 输入捕获（不可见 input_text）---
        self._draw_input_widget(pad_x, sep_y, input_h, w, now)

        imgui.end()
        imgui.pop_style_color(1)
        imgui.pop_style_var(2)

        # === foreground draw list：覆盖主 UI，在所有 ImGui 窗口之上 ===
        fdl = imgui.get_foreground_draw_list()

        # 背景（覆盖主 UI）
        bg_col = imgui.get_color_u32_rgba(0.02, 0.02, 0.04, 0.96)
        fdl.add_rect_filled(0, 0, w, h, bg_col, rounding=6.0,
                            flags=imgui.DRAW_ROUND_CORNERS_BOTTOM)

        # 分隔线
        sep_col = imgui.get_color_u32_rgba(0.15, 0.15, 0.22, 1.0)
        fdl.add_line(0, sep_y, w, sep_y, sep_col, 1.0)

        # 输入区背景
        input_bg = imgui.get_color_u32_rgba(0.06, 0.06, 0.09, 1.0)
        fdl.add_rect_filled(0, sep_y, w, h, input_bg, rounding=6.0,
                            flags=imgui.DRAW_ROUND_CORNERS_BOTTOM)

        # --- 日志区 ---
        self._draw_log_widget(pad_x, pad_y, w, sep_y, fdl)

        # 提示符 ">"
        prompt_col = imgui.get_color_u32_rgba(0.0, 0.85, 1.0, 0.9)
        prompt_y = sep_y + (input_h - 18) / 2
        if self.font_mono:
            imgui.push_font(self.font_mono)
        fdl.add_text(pad_x, prompt_y, prompt_col, ">")
        if self.font_mono:
            imgui.pop_font()

        # 输入文字
        text_col = imgui.get_color_u32_rgba(0.9, 0.92, 0.95, 1.0)
        if self.font_mono:
            imgui.push_font(self.font_mono)
        fdl.add_text(pad_x + 18, prompt_y, text_col, self._input_buf)

        # 光标（移动后0.5秒内常亮，之后闪烁）
        time_since_move = now - self._cursor_moved_time
        should_blink = time_since_move > 0.5
        cursor_visible = not should_blink or (now % 1.0) < 0.53

        if self.visible and cursor_visible:
            prefix = self._input_buf[:self._cursor_pos]
            cursor_x = pad_x + 18 + imgui.calc_text_size(prefix)[0]
            cursor_col = imgui.get_color_u32_rgba(0.9, 0.92, 0.95, 0.9)
            fdl.add_line(cursor_x, sep_y + 8, cursor_x, h - 8, cursor_col, 1.5)
        if self.font_mono:
            imgui.pop_font()

        # 底部 accent 线
        line_thick = 3.0 if self._dragging else (2.0 if drag_zone else 1.0)
        line_color = imgui.get_color_u32_rgba(
            0.0, 0.85, 1.0, 1.0 if (self._dragging or drag_zone) else 0.7)
        fdl.add_line(0, h, w, h, line_color, line_thick)

    # ------------------------------------------------------------------
    # 日志区：input_text_multiline 只读
    # ------------------------------------------------------------------

    def _draw_log_widget(self, pad_x, pad_y, w, sep_y, fdl):
        log_w = w - pad_x * 2
        log_h = sep_y - pad_y * 2

        if log_h <= 0 or not self._lines:
            return

        if self.font_mono:
            imgui.push_font(self.font_mono)

        line_h = imgui.get_text_line_height() + 2  # 行高 + 间距

        # 总内容高度
        total_h = len(self._lines) * line_h

        # 滚动目标
        if self._scroll_to_bottom:
            self._scroll_to_bottom = False
            self._log_scroll_y = max(0.0, total_h - log_h)

        max_scroll = max(0.0, total_h - log_h)
        self._log_scroll_y = max(0.0, min(self._log_scroll_y, max_scroll))

        # 鼠标滚轮（只在日志区内响应）
        io = imgui.get_io()
        mx, my = io.mouse_pos
        if pad_x <= mx <= pad_x + log_w and pad_y <= my <= pad_y + log_h:
            wheel = io.mouse_wheel
            if wheel != 0:
                self._log_scroll_y -= wheel * 40
                self._log_scroll_y = max(0.0, min(self._log_scroll_y, max_scroll))

        # 计算可见行范围
        first_line = int(self._log_scroll_y // line_h)
        last_line = min(len(self._lines), first_line + int(log_h // line_h) + 2)

        # 裁剪矩形（用 foreground draw list）
        fdl.push_clip_rect(pad_x, pad_y, pad_x + log_w, pad_y + log_h, True)

        y_offset = pad_y + first_line * line_h - self._log_scroll_y

        for i in range(first_line, last_line):
            text, level = self._lines[i]
            col = _LEVEL_COLORS.get(level, _LEVEL_COLORS["info"])
            col_u32 = imgui.get_color_u32_rgba(*col)
            fdl.add_text(pad_x + 4, y_offset, col_u32, text)
            y_offset += line_h

        fdl.pop_clip_rect()

        if self.font_mono:
            imgui.pop_font()

    # ------------------------------------------------------------------
    # 输入捕获（不可见 input_text）
    # ------------------------------------------------------------------

    def _draw_input_widget(self, pad_x, sep_y, input_h, w, now):
        # 点击输入区重新聚焦
        mx, my = imgui.get_mouse_pos()
        if imgui.is_mouse_clicked(0) and 0 <= mx <= w and sep_y <= my <= sep_y + input_h:
            self._input_focus_next = True

        imgui.push_item_width(w - pad_x - 18 - pad_x)
        imgui.set_cursor_screen_pos((pad_x + 18, sep_y + (input_h - 18) / 2))

        imgui.push_style_color(imgui.COLOR_FRAME_BACKGROUND, 0, 0, 0, 0)
        imgui.push_style_color(imgui.COLOR_FRAME_BACKGROUND_HOVERED, 0, 0, 0, 0)
        imgui.push_style_color(imgui.COLOR_FRAME_BACKGROUND_ACTIVE, 0, 0, 0, 0)
        imgui.push_style_color(imgui.COLOR_BORDER, 0, 0, 0, 0)
        imgui.push_style_color(imgui.COLOR_TEXT, 0, 0, 0, 0)
        imgui.push_style_var(imgui.STYLE_FRAME_PADDING, (0, 0))

        if self.font_mono:
            imgui.push_font(self.font_mono)

        if self._input_focus_next:
            imgui.set_keyboard_focus_here()
            self._input_focus_next = False

        changed, new_val = imgui.input_text(
            "##cmd_input",
            self._input_buf,
            256,
            flags=(imgui.INPUT_TEXT_ENTER_RETURNS_TRUE
                   | imgui.INPUT_TEXT_CALLBACK_HISTORY
                   | imgui.INPUT_TEXT_CALLBACK_ALWAYS),
            callback=self._cmd_cb,
        )

        if changed:
            cmd_str = new_val.strip()
            if cmd_str:
                self.log(f"> {cmd_str}", level="system")
                result = self.commands.dispatch(cmd_str)
                if result.message:
                    lvl = "info" if result.success else "error"
                    self.log(result.message, level=lvl)
                self._history.append(cmd_str)
                if len(self._history) > 100:
                    self._history = self._history[-100:]
            self._history_idx = -1
            self._input_buf = ""
            self._cursor_pos = 0
            self._input_focus_next = True
        else:
            self._input_buf = new_val

        if self.font_mono:
            imgui.pop_font()
        imgui.pop_style_var(1)
        imgui.pop_item_width()
        imgui.pop_style_color(5)
