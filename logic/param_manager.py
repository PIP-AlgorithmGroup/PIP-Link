"""Parameter management"""


class ParamManager:
    """Parameter manager"""

    def __init__(self):
        self.params = {
            # Input
            "mouse_sensitivity": 1.0,
            "fov": 90.0,
            "invert_pitch": False,

            # Video
            "video_quality": 1,  # 0: Low, 1: Medium, 2: High, 3: Ultra
            "resolution": 4,  # index into resolutions dict, default 1920x1080
            "window_mode": 0,  # 0: Windowed, 1: Fullscreen (borderless)
            "fullscreen_display": -1,  # -1: current display, 0+: specific display index

            # Recording
            "recording_enabled": False,
            "recording_bitrate": 5000,  # kbps
            "recording_format": 0,  # 0: MP4, 1: MKV, 2: AVI
            "save_dir": "",  # 空字符串 = 项目根目录

            # Debug
            "show_performance_graph": False,
            "show_debug_info": False,

            # Stream (remote — synced to air unit)
            "stream_encoder": 1,           # 0=JPEG, 1=H.264
            "stream_bitrate": 2000,        # kbps
            "stream_fps": 30,              # target fps
            "stream_fec_enabled": False,
            "stream_fec_redundancy": 0.20,

            # Image enhancement (remote — synced to air unit)
            "brightness": 0,   # -100~100
            "contrast": 0,     # -100~100
            "sharpness": 0,    # 0~100
            "denoise": 0,      # 0~100
        }

        # Resolution mapping: (width, height, aspect_ratio_label)
        self.resolutions = {
            0:  (960,  540,  "16:9"),
            1:  (1024, 576,  "16:9"),
            2:  (1280, 720,  "16:9"),
            3:  (1366, 768,  "16:9"),
            4:  (1600, 900,  "16:9"),
            5:  (1920, 1080, "16:9"),
            6:  (2560, 1440, "16:9"),
            7:  (3840, 2160, "16:9"),
            8:  (1280, 800,  "16:10"),
            9:  (1440, 900,  "16:10"),
            10: (1680, 1050, "16:10"),
            11: (1920, 1200, "16:10"),
            12: (2560, 1600, "16:10"),
            13: (1024, 768,  "4:3"),
            14: (1280, 960,  "4:3"),
            15: (1600, 1200, "4:3"),
            16: (1280, 1024, "5:4"),
            17: (1600, 1280, "5:4"),
        }

    def get_param(self, key: str):
        """Get parameter"""
        return self.params.get(key)

    def set_param(self, key: str, value):
        """Set parameter"""
        self.params[key] = value

    def get_all_params(self) -> dict:
        """Get all parameters"""
        return self.params.copy()

    def get_resolution_string(self, index: int) -> str:
        """Get resolution as string format"""
        if index in self.resolutions:
            w, h, ratio = self.resolutions[index]
            return f"{w}x{h} ({ratio})"
        return "Unknown"

    def get_resolution_list(self) -> list:
        """Get all resolutions as display strings"""
        return [
            f"{w}x{h} ({ratio})"
            for w, h, ratio in self.resolutions.values()
        ]


