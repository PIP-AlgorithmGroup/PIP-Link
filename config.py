"""Configuration"""

class Config:
    # UI config
    RENDER_WIDTH = 1600
    RENDER_HEIGHT = 900
    TARGET_FPS = 120
    FULLSCREEN = False

    # Font config
    FONT_SIZE = 22
    FONT_SIZE_TITLE = 28
    FONT_SIZE_BODY = 20
    FONT_SIZE_MONO = 22
    FONT_PATH = "C:\\Windows\\Fonts\\segoeui.ttf"
    FONT_PATH_BOLD = "C:\\Windows\\Fonts\\segoeuib.ttf"
    FONT_PATH_MONO = "C:\\Windows\\Fonts\\consola.ttf"

    # Network config
    MDNS_SERVICE_NAME = "_pip-link._udp"
    MDNS_TIMEOUT = 5.0

    # UDP config
    UDP_BUFFER_SIZE = 65536
    VIDEO_PORT_OFFSET = 1000
    CONTROL_PORT_OFFSET = 2000

    # 控制指令配置
    TX_SEND_RATE = 50  # Hz
    TX_TIMEOUT = 0.1

    # 心跳配置
    HEARTBEAT_INTERVAL = 0.1
    HEARTBEAT_TIMEOUT = 0.3

    # 视频解码配置
    RENDER_QUEUE_MAX_SIZE = 3

    # FEC 配置
    FEC_ENABLED = True
    FEC_REDUNDANCY = 0.2  # 20% 冗余
