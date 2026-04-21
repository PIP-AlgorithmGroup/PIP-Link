"""H.264 编码器 - 使用 PyAV 实现低延迟实时编码"""

import fractions
import logging
from typing import List, Optional
import numpy as np

logger = logging.getLogger(__name__)

try:
    import av
    H264_AVAILABLE = True
except ImportError:
    H264_AVAILABLE = False
    logger.warning("PyAV not installed, H.264 encoding unavailable")


class H264Encoder:
    """H.264 实时编码器 - ultrafast + zerolatency"""

    def __init__(self, width: int, height: int, fps: int = 30,
                 bitrate: int = 2_000_000, keyframe_interval: int = 30):
        if not H264_AVAILABLE:
            raise RuntimeError("PyAV not installed")

        self.width = width
        self.height = height
        self.fps = fps
        self.pts = 0

        self.codec_ctx = av.CodecContext.create('libx264', 'w')
        self.codec_ctx.width = width
        self.codec_ctx.height = height
        self.codec_ctx.time_base = fractions.Fraction(1, fps)
        self.codec_ctx.bit_rate = bitrate
        self.codec_ctx.pix_fmt = 'yuv420p'
        self.codec_ctx.gop_size = keyframe_interval
        self.codec_ctx.max_b_frames = 0
        self.codec_ctx.options = {
            'preset': 'ultrafast',
            'tune': 'zerolatency',
            'profile': 'baseline',
        }
        self.codec_ctx.open()
        logger.info(f"H264Encoder initialized: {width}x{height}@{fps}fps, "
                    f"{bitrate // 1000}kbps")

    def encode(self, frame_bgr: np.ndarray, force_keyframe: bool = False) -> List[bytes]:
        """编码一帧 BGR，返回编码后的 packet 列表"""
        frame = av.VideoFrame.from_ndarray(frame_bgr, format='bgr24')
        frame.pts = self.pts
        self.pts += 1

        if force_keyframe:
            frame.pict_type = av.video.frame.PictureType.I

        packets = self.codec_ctx.encode(frame)
        return [bytes(pkt) for pkt in packets]

    def flush(self) -> List[bytes]:
        """刷新编码器缓冲"""
        packets = self.codec_ctx.encode()
        return [bytes(pkt) for pkt in packets]

    def close(self):
        if self.codec_ctx:
            self.codec_ctx = None
