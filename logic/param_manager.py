"""参数管理"""


class ParamManager:
    """参数管理"""

    def __init__(self):
        self.params = {
            "mouse_sensitivity": 1.0,
            "fov": 90.0,
        }

    def get_param(self, key: str):
        """获取参数"""
        return self.params.get(key)

    def set_param(self, key: str, value):
        """设置参数"""
        self.params[key] = value

    def get_all_params(self) -> dict:
        """获取所有参数"""
        return self.params.copy()
