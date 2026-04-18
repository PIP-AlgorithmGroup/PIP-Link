"""ImGui UI components - CS2 inspired flight control aesthetic"""

import imgui
import time
from typing import Optional, Callable, Dict
from ui.theme import Theme
from config import Config


class ImGuiUI:
    """ImGui UI manager with multi-font rendering"""

    def __init__(self, font_title=None, font_body=None, font_mono=None):
        self.show_menu = False
        self.menu_alpha = 0.0
        self.menu_scale = 0.95
        self.menu_open_time = 0.0
        self.animation_duration = 0.3

        self.font_title = font_title
        self.font_body = font_body
        self.font_mono = font_mono

        # Track connect time for uptime display
        self._connect_time: Optional[float] = None
        self._last_state: str = "idle"

        # Bandwidth tracking
        self._last_bytes: int = 0
        self._last_bw_time: float = time.time()
        self._bandwidth_kbps: float = 0.0

        # Connection tab state
        self._service_name_buf: bytearray = bytearray(Config.MDNS_SERVICE_NAME.encode() + b'\x00' * (128 - len(Config.MDNS_SERVICE_NAME)))

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
    # Animation
    # -------------------------------------------------------------------------

    def _update_menu_animation(self):
        current_time = time.time()
        elapsed = current_time - self.menu_open_time
        progress = min(elapsed / self.animation_duration, 1.0)

        if self.show_menu:
            self.menu_alpha = progress
            self.menu_scale = 0.95 + (progress * 0.05)
        else:
            self.menu_alpha = 1.0 - progress
            self.menu_scale = 1.0 - (progress * 0.05)

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
            dt = t_now - self._last_bw_time
            if dt >= 0.5:
                self._bandwidth_kbps = (bytes_now - self._last_bytes) * 8 / dt / 1000
                self._last_bytes = bytes_now
                self._last_bw_time = t_now

        self._update_menu_animation()
        imgui.push_style_var(imgui.STYLE_ALPHA, self.menu_alpha)

        menu_width = 800
        menu_height = 650
        center_x = (Config.RENDER_WIDTH - menu_width) / 2
        center_y = (Config.RENDER_HEIGHT - menu_height) / 2

        imgui.set_next_window_position(center_x, center_y, imgui.ALWAYS)
        imgui.set_next_window_size(menu_width, menu_height, imgui.ALWAYS)

        expanded, _ = imgui.begin(
            "##menu", False,
            imgui.WINDOW_NO_TITLE_BAR | imgui.WINDOW_NO_MOVE | imgui.WINDOW_NO_RESIZE
        )

        if expanded:
            pushed = self._push_font(self.font_title)
            tab_opened = imgui.begin_tab_bar("MenuTabs", imgui.TAB_BAR_FITTING_POLICY_SCROLL)
            self._pop_font(pushed)

            if tab_opened:
                self._tab("CONNECTION", lambda: self._draw_connection_tab(
                    session_state, callbacks, stats or {}))
                self._tab("PARAMETERS", lambda: self._draw_parameters_tab(
                    params, on_param_change))
                self._tab("VIDEO", lambda: self._draw_video_tab(
                    params, on_param_change, stats or {}, live_status or {}))
                self._tab("RECORDING", lambda: self._draw_recording_tab(
                    params, on_param_change))
                self._tab("DIAGNOSTICS", lambda: self._draw_diagnostics_tab(
                    stats or {}, live_status or {}))
                self._tab("DEBUG", lambda: self._draw_debug_tab(
                    params, on_param_change, stats or {}, live_status or {}))
                imgui.end_tab_bar()

        imgui.end()
        imgui.pop_style_var()

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

    def _draw_connection_tab(self, session_state: str, callbacks: Dict, stats: Dict) -> None:
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

        latency = stats.get("latency_ms", 0.0)
        loss = stats.get("packet_loss_rate", 0.0)
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
            imgui.text_colored(f"{latency:.1f} ms", *lat_color)
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
            imgui.begin_child("##device_list", 0, 120, border=True)

            for idx, device in enumerate(devices):
                device_name = device.get("name", "Unknown")
                device_ip = device.get("ip", "0.0.0.0")
                device_port = device.get("port", 0)

                # Selectable device row
                is_selected = device.get("selected", False)
                clicked, _ = imgui.selectable(
                    f"{device_name}##dev{idx}",
                    is_selected,
                    imgui.SELECTABLE_SPAN_ALL_COLUMNS
                )
                if clicked and "select_device" in callbacks:
                    callbacks["select_device"](idx)

                # Show IP:Port on same line
                imgui.same_line(250)
                imgui.text_colored(f"{device_ip}:{device_port}", *Theme.TEXT_SECONDARY)

            imgui.end_child()
            self._pop_font(pushed)
        else:
            pushed = self._push_font(self.font_mono)
            imgui.text_colored("  No devices found. Click SCAN to discover.", *Theme.TEXT_SECONDARY)
            self._pop_font(pushed)

        imgui.spacing()

        # SCAN button
        pushed_btn = self._push_font(self.font_body)
        if imgui.button("SCAN", width=100, height=28):
            if "scan_devices" in callbacks:
                callbacks["scan_devices"]()
        self._pop_font(pushed_btn)

        imgui.spacing()
        imgui.separator()
        imgui.spacing()

        # --- Service name input (manual entry) ---
        pushed = self._push_font(self.font_body)
        imgui.text_colored("OR ENTER SERVICE NAME MANUALLY", *Theme.TEXT_SECONDARY)
        self._pop_font(pushed)
        imgui.spacing()

        pushed = self._push_font(self.font_mono)
        imgui.set_next_item_width(400)
        changed, new_name = imgui.input_text("##svcname", self._service_name_buf.rstrip(b'\x00').decode(), 128)
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
            if is_idle and "connect" in callbacks:
                svc = self._service_name_buf.rstrip(b'\x00').decode().strip()
                callbacks["connect"](svc or Config.MDNS_SERVICE_NAME)
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

        quality = params.get("video_quality", 1)
        imgui.set_next_item_width(220)
        changed, new_val = imgui.combo("Quality", quality, ["LOW", "MEDIUM", "HIGH", "ULTRA"])
        if changed and on_change:
            on_change("video_quality", new_val)

        resolution = params.get("resolution", 5)
        imgui.set_next_item_width(220)
        changed, new_val = imgui.combo(
            "Resolution", resolution,
            self._resolution_labels
        )
        if changed and on_change:
            on_change("resolution", new_val)

        window_mode = params.get("window_mode", 0)
        imgui.set_next_item_width(220)
        changed, new_val = imgui.combo("Window Mode", window_mode, ["WINDOWED", "FULLSCREEN"])
        if changed and on_change:
            on_change("window_mode", new_val)

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

    def _draw_recording_tab(self, params: Dict, on_change: Optional[Callable]) -> None:
        self._draw_section_title("RECORDING SETTINGS")

        pushed = self._push_font(self.font_body)

        recording_enabled = params.get("recording_enabled", False)
        changed, new_val = imgui.checkbox("Enable Recording", recording_enabled)
        if changed and on_change:
            on_change("recording_enabled", new_val)

        imgui.spacing()

        bitrate = params.get("recording_bitrate", 5000)
        changed, new_val = self._slider_int_with_hint(
            "Bitrate (kbps)##bitrate", bitrate, 1000, 20000
        )
        if changed and on_change:
            on_change("recording_bitrate", new_val)

        imgui.spacing()
        self._draw_subsection("FORMAT")

        fmt = params.get("recording_format", 0)
        imgui.set_next_item_width(220)
        changed, new_val = imgui.combo("Format##fmt", fmt, ["MP4 (H.264)", "MKV (H.264)", "AVI (RAW)"])
        if changed and on_change:
            on_change("recording_format", new_val)

        imgui.spacing()
        self._draw_subsection("CURRENT CONFIG")
        self._pop_font(pushed)

        self._draw_kv_row("Status", "RECORDING" if params.get("recording_enabled") else "IDLE",
                          accent=params.get("recording_enabled", False))
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

        self._draw_kv_row("Min RTT", f"{latency_min:.1f} ms")
        self._draw_kv_row("Avg RTT", f"{latency_avg:.1f} ms", accent=True)
        self._draw_kv_row("Max RTT", f"{latency_max:.1f} ms")

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
        self._draw_kv_row("Latency", f"{latency:.1f} ms", accent=latency_accent)
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

    # -------------------------------------------------------------------------
    # Status bar (shown when menu is closed)
    # -------------------------------------------------------------------------

    def draw_status_bar(self, status: Dict) -> None:
        bar_w = 220
        bar_h = 120
        imgui.set_next_window_position(
            Config.RENDER_WIDTH - bar_w - 16,
            Config.RENDER_HEIGHT - bar_h - 16,
            imgui.ALWAYS
        )
        imgui.set_next_window_size(bar_w, bar_h, imgui.ALWAYS)

        imgui.push_style_color(imgui.COLOR_WINDOW_BACKGROUND, 0.06, 0.06, 0.09, 0.85)
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
            self._draw_label_value("LATENCY", f"{latency:.1f} ms", right_align=True)
            self._draw_label_value("LOSS", f"{loss:.2%}",
                                   accent=(loss > 0.01), right_align=True)
            self._draw_label_value("FRAMES", f"{frames}", right_align=True)

        imgui.end()
        imgui.pop_style_color()

    # -------------------------------------------------------------------------
    # Helpers
    # -------------------------------------------------------------------------

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
