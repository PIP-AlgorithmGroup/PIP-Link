"""CS2 风格开发者控制台"""

import time
import math
import imgui


# 日志级别颜色
_LEVEL_COLORS = {
    "debug":  (0.45, 0.45, 0.50, 1.0),
    "info":   (0.78, 0.80, 0.85, 1.0),
    "warn":   (1.0, 0.85, 0.0, 1.0),
    "error":  (1.0, 0.3, 0.3, 1.0),
    "system": (0.0, 0.85, 1.0, 1.0),
}

# 自动检测关键词 → 级别
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
        self._lines: list[tuple[str, str]] = []  # (text, level)
        self._max_lines = 500
        self._anim_h = 0.0          # 当前动画高度
        self._scroll_y = 0.0        # 当前滚动位置
        self._scroll_target = 0.0   # 目标滚动位置
        self._last_frame_time = time.time()
        self._input_buf = ""
        self.font_mono = font_mono
        self.font_body = font_body

        # 动画参数
        self._slide_tau = 0.08      # 展开/收起时间常数
        self._scroll_tau = 0.08     # 滚动时间常数

        # 拖动调节高度
        self._user_height_ratio = 0.55
        self._dragging = False

    # ------------------------------------------------------------------
    # 公共 API
    # ------------------------------------------------------------------

    def toggle(self):
        self.visible = not self.visible

    def log(self, text: str, level: str | None = None):
        """添加一行日志。level 为 None 时自动检测。"""
        if level is None:
            level = _detect_level(text)
        self._lines.append((text, level))
        if len(self._lines) > self._max_lines:
            self._lines = self._lines[-self._max_lines:]
        self._scroll_target = float("inf")

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

        # 指数衰减动画
        diff = target_h - self._anim_h
        if abs(diff) > 0.5:
            t = 1.0 - math.exp(-dt / self._slide_tau)
            self._anim_h += diff * t
        else:
            self._anim_h = target_h

        # 完全收起时不渲染
        if self._anim_h < 1.0:
            self._anim_h = 0.0
            return

        w = disp_w
        h = self._anim_h
        line_h = 22.0       # 每行高度
        pad_x = 14.0
        pad_y = 8.0
        input_h = 36.0      # 底部输入框预留高度

        draw_list = imgui.get_overlay_draw_list()

        # --- 不可见窗口吃掉控制台区域的输入，防止穿透到下层 UI ---
        # 仅当鼠标在控制台区域内时才抢焦点，否则让 ##menu 正常响应
        mx, my = imgui.get_io().mouse_pos
        if 0 <= mx <= w and 0 <= my <= h:
            imgui.set_next_window_focus()
        imgui.set_next_window_position(0, 0)
        imgui.set_next_window_size(w, h)
        imgui.push_style_var(imgui.STYLE_WINDOW_PADDING, (0, 0))
        imgui.push_style_var(imgui.STYLE_WINDOW_BORDERSIZE, 0.0)
        imgui.push_style_color(imgui.COLOR_WINDOW_BACKGROUND, 0, 0, 0, 0)
        imgui.begin(
            "##console_input_blocker",
            flags=(imgui.WINDOW_NO_TITLE_BAR | imgui.WINDOW_NO_RESIZE
                   | imgui.WINDOW_NO_MOVE | imgui.WINDOW_NO_SCROLLBAR
                   | imgui.WINDOW_NO_SAVED_SETTINGS),
        )
        imgui.end()
        imgui.pop_style_color(1)
        imgui.pop_style_var(2)

        # --- 背景 ---
        bg_col = imgui.get_color_u32_rgba(0.02, 0.02, 0.04, 0.92)
        # 底部两角圆角 6px（用 draw_list flags）
        draw_list.add_rect_filled(0, 0, w, h, bg_col, rounding=6.0,
                                  flags=imgui.DRAW_ROUND_CORNERS_BOTTOM)

        # --- 底部 accent 线 + 拖动手柄 ---
        mx, my = io.mouse_pos
        drag_zone = (0 <= mx <= w and h - 5 <= my <= h + 5)
        if drag_zone and io.mouse_down[0] and not self._dragging:
            self._dragging = True
        if self._dragging:
            if io.mouse_down[0]:
                new_h = max(100, min(my, disp_h * 0.85))
                self._user_height_ratio = new_h / disp_h
            else:
                self._dragging = False

        line_thick = 3.0 if self._dragging else (2.0 if drag_zone else 1.0)
        line_color = imgui.get_color_u32_rgba(0.0, 0.85, 1.0, 1.0 if (self._dragging or drag_zone) else 0.7)
        draw_list.add_line(0, h, w, h, line_color, line_thick)

        # --- 日志区域 ---
        content_h = h - pad_y - input_h
        if content_h < line_h:
            return

        visible_lines = int(content_h / line_h)
        total_lines = len(self._lines)
        max_scroll = max(0.0, (total_lines * line_h) - content_h)

        # 处理滚轮（仅当鼠标在控制台区域内）
        if 0 <= mx <= w and 0 <= my <= h:
            wheel = io.mouse_wheel
            if wheel != 0:
                self._scroll_target -= wheel * 100.0

        # clamp 目标
        self._scroll_target = max(0.0, min(self._scroll_target, max_scroll))

        # 弹性滚动插值
        sdiff = self._scroll_target - self._scroll_y
        if abs(sdiff) > 0.5:
            st = 1.0 - math.exp(-dt / self._scroll_tau)
            self._scroll_y += sdiff * st
        else:
            self._scroll_y = self._scroll_target

        # --- 用 mono 字体绘制日志 ---
        use_mono = self.font_mono is not None
        if use_mono:
            imgui.push_font(self.font_mono)

        # 计算可见行范围
        first_line = int(self._scroll_y / line_h)
        first_line = max(0, min(first_line, total_lines - 1)) if total_lines > 0 else 0
        y_offset = pad_y - (self._scroll_y % line_h)

        for i in range(first_line, total_lines):
            y = y_offset + (i - first_line) * line_h
            if y > content_h:
                break
            if y + line_h < 0:
                continue

            text, level = self._lines[i]
            color = _LEVEL_COLORS.get(level, _LEVEL_COLORS["info"])
            col32 = imgui.get_color_u32_rgba(*color)
            draw_list.add_text(pad_x, y, col32, text)

        if use_mono:
            imgui.pop_font()

        # --- 底部输入框（预留，仅绘制外观） ---
        input_y = h - input_h
        input_bg = imgui.get_color_u32_rgba(0.06, 0.06, 0.09, 1.0)
        draw_list.add_rect_filled(0, input_y, w, h, input_bg,
                                  rounding=6.0, flags=imgui.DRAW_ROUND_CORNERS_BOTTOM)
        # 分隔线
        sep_col = imgui.get_color_u32_rgba(0.15, 0.15, 0.22, 1.0)
        draw_list.add_line(0, input_y, w, input_y, sep_col, 1.0)
        # 提示符
        prompt_col = imgui.get_color_u32_rgba(0.0, 0.85, 1.0, 0.7)
        if self.font_mono is not None:
            imgui.push_font(self.font_mono)
        draw_list.add_text(pad_x, input_y + (input_h - 18) / 2, prompt_col, "> _")
        if self.font_mono is not None:
            imgui.pop_font()
