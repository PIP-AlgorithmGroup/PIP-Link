"""H.264 解码器 - 使用 PyAV 实现实时解码"""

import logging
from typing import List, Optional
import numpy as np

logger = logging.getLogger(__name__)

try:
    import av
    H264_AVAILABLE = True
except ImportError:
    H264_AVAILABLE = False
    logger.warning("PyAV not installed, H.264 decoding unavailable")


class H264Decoder:
    """H.264 实时解码器"""

    def __init__(self):
        if not H264_AVAILABLE:
            raise RuntimeError("PyAV not installed")

        self.codec_ctx = av.CodecContext.create('h264', 'r')
        self.codec_ctx.open()
        logger.info("H264Decoder initialized")

    def decode(self, nal_data: bytes) -> List[np.ndarray]:
        """解码 NAL 数据，返回 BGR 帧列表"""
        try:
            packet = av.Packet(nal_data)
            frames = self.codec_ctx.decode(packet)
            return [frame.to_ndarray(format='bgr24') for frame in frames]
        except Exception as e:
            logger.debug(f"H264 decode error: {e}")
            return []

    def close(self):
        if self.codec_ctx:
            self.codec_ctx = None
