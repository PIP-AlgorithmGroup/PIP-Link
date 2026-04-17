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
            "resolution": 1,  # 0: 720p, 1: 1080p, 2: 1440p
            "window_mode": 0,  # 0: Windowed, 1: Fullscreen

            # Recording
            "recording_enabled": False,
            "recording_bitrate": 5000,  # kbps

            # Debug
            "show_performance_graph": False,
            "show_debug_info": False,
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

