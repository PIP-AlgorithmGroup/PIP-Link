"""
协议编解码 - 消息序列化/反序列化和 CRC 校验
"""

import struct
import json
import zlib
from typing import Tuple, Optional
from dataclasses import dataclass


# 协议常量
MAGIC = 0xABCD
VERSION = 0x01

# 消息类型
MSG_TYPE_CONTROL_COMMAND = 0x01
MSG_TYPE_PARAM_UPDATE = 0x02
MSG_TYPE_PARAM_QUERY = 0x03
MSG_TYPE_HEARTBEAT = 0x04
MSG_TYPE_ACK = 0x05
MSG_TYPE_VIDEO_ACK = 0x06
MSG_TYPE_VIDEO_NACK = 0x07


KEYBOARD_STATE_SIZE = 10
MOUSE_DATA_SIZE = 6  # int16 dx + int16 dy + uint8 buttons + int8 scroll


@dataclass
class ControlCommand:
    """控制指令 — 10 字节键盘位图 + 6 字节鼠标数据"""
    seq: int
    t1: float
    keyboard_state: bytes = b'\x00' * KEYBOARD_STATE_SIZE
    mouse_dx: int = 0
    mouse_dy: int = 0
    mouse_buttons: int = 0
    scroll_delta: int = 0

    def to_bytes(self) -> bytes:
        return bytes(self.keyboard_state[:KEYBOARD_STATE_SIZE]).ljust(KEYBOARD_STATE_SIZE, b'\x00')

    @staticmethod
    def from_bytes(data: bytes) -> 'ControlCommand':
        kb = data[:KEYBOARD_STATE_SIZE] if len(data) >= KEYBOARD_STATE_SIZE else data.ljust(KEYBOARD_STATE_SIZE, b'\x00')
        mouse_dx = mouse_dy = mouse_buttons = scroll_delta = 0
        if len(data) >= KEYBOARD_STATE_SIZE + MOUSE_DATA_SIZE:
            mouse_dx, mouse_dy, mouse_buttons, scroll_delta = struct.unpack(
                '=hhBb', data[KEYBOARD_STATE_SIZE:KEYBOARD_STATE_SIZE + MOUSE_DATA_SIZE])
        return ControlCommand(seq=0, t1=0.0, keyboard_state=kb,
                              mouse_dx=mouse_dx, mouse_dy=mouse_dy,
                              mouse_buttons=mouse_buttons, scroll_delta=scroll_delta)


@dataclass
class ACKMessage:
    """ACK 消息"""
    seq: int
    t2: float
    t3: float


class Protocol:
    """协议编解码器"""

    @staticmethod
    def build_control_command(
        seq: int,
        t1: float,
        keyboard_state: bytes = b'\x00' * KEYBOARD_STATE_SIZE,
        mouse_dx: int = 0,
        mouse_dy: int = 0,
        mouse_buttons: int = 0,
        scroll_delta: int = 0,
    ) -> bytes:
        """
        构建控制指令消息

        格式：
        [Magic:2][Version:1][MsgType:1][Reserved:1][Seq:4][t1:8][KeyboardState:10]
        [MouseDX:2][MouseDY:2][MouseButtons:1][ScrollDelta:1][CRC32:4]
        总长度：37 字节
        """
        kb_payload = bytes(keyboard_state[:KEYBOARD_STATE_SIZE]).ljust(KEYBOARD_STATE_SIZE, b'\x00')
        mouse_payload = struct.pack('=hhBb',
                                    max(-32768, min(32767, mouse_dx)),
                                    max(-32768, min(32767, mouse_dy)),
                                    mouse_buttons & 0xFF,
                                    max(-128, min(127, scroll_delta)))

        # 构建消息头（不含 CRC）
        header = struct.pack(
            '=HBBBI',
            MAGIC,
            VERSION,
            MSG_TYPE_CONTROL_COMMAND,
            0,
            seq
        )

        # 构建时间戳部分
        timestamp = struct.pack('=d', t1)

        # 组合消息（不含 CRC）
        message_without_crc = header + timestamp + kb_payload + mouse_payload

        # 计算 CRC32
        crc = zlib.crc32(message_without_crc) & 0xffffffff

        # 添加 CRC
        message = message_without_crc + struct.pack('=I', crc)

        return message

    @staticmethod
    def build_ack(seq: int, t2: float, t3: float) -> bytes:
        """
        构建 ACK 消息

        格式：
        [Magic:2][Version:1][MsgType:1][Reserved:1][Seq:4][t2:8][t3:8][CRC32:4]
        """
        # 构建消息头（不含 CRC）
        header = struct.pack(
            '=HBBBI',
            MAGIC,
            VERSION,
            MSG_TYPE_ACK,
            0,
            seq
        )

        # 构建时间戳部分
        timestamps = struct.pack('=dd', t2, t3)

        # 组合消息（不含 CRC）
        message_without_crc = header + timestamps

        # 计算 CRC32
        crc = zlib.crc32(message_without_crc) & 0xffffffff

        # 添加 CRC
        message = message_without_crc + struct.pack('=I', crc)

        return message

    @staticmethod
    def build_heartbeat(seq: int, t1: float) -> bytes:
        """
        构建心跳消息

        格式：
        [Magic:2][Version:1][MsgType:1][Reserved:1][Seq:4][t1:8][CRC32:4]
        """
        # 构建消息头（不含 CRC）
        header = struct.pack(
            '=HBBBI',
            MAGIC,
            VERSION,
            MSG_TYPE_HEARTBEAT,
            0,
            seq
        )

        # 构建时间戳部分
        timestamp = struct.pack('=d', t1)

        # 组合消息（不含 CRC）
        message_without_crc = header + timestamp

        # 计算 CRC32
        crc = zlib.crc32(message_without_crc) & 0xffffffff

        # 添加 CRC
        message = message_without_crc + struct.pack('=I', crc)

        return message

    @staticmethod
    def parse_message(data: bytes) -> Tuple[int, int, float, Optional[bytes]]:
        """
        解析消息

        返回：(msg_type, seq, t1, payload)
        """
        if len(data) < 18:
            raise ValueError(f"消息太短: {len(data)} bytes")

        # 解析消息头
        magic, version, msg_type, reserved, seq = struct.unpack(
            '=HBBBI',
            data[:9]
        )

        # 验证 Magic 和 Version
        if magic != MAGIC:
            raise ValueError(f"Magic 错误: {hex(magic)}")
        if version != VERSION:
            raise ValueError(f"Version 错误: {version}")

        # 验证 CRC
        crc_received = struct.unpack('=I', data[-4:])[0]
        crc_calculated = zlib.crc32(data[:-4]) & 0xffffffff
        if crc_received != crc_calculated:
            raise ValueError(f"CRC 校验失败: {hex(crc_received)} != {hex(crc_calculated)}")

        # 解析时间戳
        t1 = struct.unpack('=d', data[9:17])[0]

        # 解析 payload
        payload = data[17:-4] if len(data) > 21 else None

        return msg_type, seq, t1, payload

    @staticmethod
    def parse_ack(data: bytes) -> Tuple[int, float, float]:
        """
        解析 ACK 消息

        返回：(seq, t2, t3)
        """
        if len(data) < 25:
            raise ValueError(f"ACK 消息太短: {len(data)} bytes")

        # 解析消息头
        magic, version, msg_type, reserved, seq = struct.unpack(
            '=HBBBI',
            data[:9]
        )

        # 验证 Magic 和 Version
        if magic != MAGIC:
            raise ValueError(f"Magic 错误: {hex(magic)}")
        if version != VERSION:
            raise ValueError(f"Version 错误: {version}")
        if msg_type != MSG_TYPE_ACK:
            raise ValueError(f"消息类型错误: {msg_type}")

        # 验证 CRC
        crc_received = struct.unpack('=I', data[-4:])[0]
        crc_calculated = zlib.crc32(data[:-4]) & 0xffffffff
        if crc_received != crc_calculated:
            raise ValueError(f"CRC 校验失败: {hex(crc_received)} != {hex(crc_calculated)}")

        # 解析时间戳
        t2, t3 = struct.unpack('=dd', data[9:25])

        return seq, t2, t3

    @staticmethod
    def parse_heartbeat(data: bytes) -> Tuple[int, float]:
        """
        解析心跳消息

        返回：(seq, t1)
        """
        if len(data) < 21:
            raise ValueError(f"心跳消息太短: {len(data)} bytes")

        # 解析消息头
        magic, version, msg_type, reserved, seq = struct.unpack(
            '=HBBBI',
            data[:9]
        )

        # 验证 Magic 和 Version
        if magic != MAGIC:
            raise ValueError(f"Magic 错误: {hex(magic)}")
        if version != VERSION:
            raise ValueError(f"Version 错误: {version}")
        if msg_type != MSG_TYPE_HEARTBEAT:
            raise ValueError(f"消息类型错误: {msg_type}")

        # 验证 CRC
        crc_received = struct.unpack('=I', data[-4:])[0]
        crc_calculated = zlib.crc32(data[:-4]) & 0xffffffff
        if crc_received != crc_calculated:
            raise ValueError(f"CRC 校验失败: {hex(crc_received)} != {hex(crc_calculated)}")

        # 解析时间戳
        t1 = struct.unpack('=d', data[9:17])[0]

        return seq, t1

    @staticmethod
    def build_video_ack(frame_id: int) -> bytes:
        """
        构建视频帧 ACK

        格式：[Magic:2][Version:1][MsgType:1][Reserved:1][FrameID:4][CRC32:4]
        """
        msg = struct.pack('=HBBBI', MAGIC, VERSION, MSG_TYPE_VIDEO_ACK, 0, frame_id)
        crc = zlib.crc32(msg) & 0xffffffff
        return msg + struct.pack('=I', crc)

    @staticmethod
    def parse_video_ack(data: bytes) -> int:
        """解析视频帧 ACK，返回 frame_id"""
        if len(data) < 13:
            raise ValueError(f"Video ACK 太短: {len(data)} bytes")
        magic, version, msg_type, reserved, frame_id = struct.unpack('=HBBBI', data[:9])
        if magic != MAGIC or msg_type != MSG_TYPE_VIDEO_ACK:
            raise ValueError("不是 Video ACK")
        crc_received = struct.unpack('=I', data[-4:])[0]
        crc_calculated = zlib.crc32(data[:-4]) & 0xffffffff
        if crc_received != crc_calculated:
            raise ValueError("CRC 校验失败")
        return frame_id

    @staticmethod
    def build_video_nack(frame_id: int, missing_chunks: list) -> bytes:
        """
        构建视频帧 NACK

        格式：[Magic:2][Version:1][MsgType:1][Reserved:1][FrameID:4][NumChunks:2][ChunkIdx:2*N][CRC32:4]
        """
        header = struct.pack('=HBBBI', MAGIC, VERSION, MSG_TYPE_VIDEO_NACK, 0, frame_id)
        chunks_data = struct.pack('=H', len(missing_chunks))
        for idx in missing_chunks:
            chunks_data += struct.pack('=H', idx)
        msg = header + chunks_data
        crc = zlib.crc32(msg) & 0xffffffff
        return msg + struct.pack('=I', crc)

    @staticmethod
    def parse_video_nack(data: bytes) -> tuple:
        """解析视频帧 NACK，返回 (frame_id, [missing_chunk_indices])"""
        if len(data) < 15:
            raise ValueError(f"Video NACK 太短: {len(data)} bytes")
        magic, version, msg_type, reserved, frame_id = struct.unpack('=HBBBI', data[:9])
        if magic != MAGIC or msg_type != MSG_TYPE_VIDEO_NACK:
            raise ValueError("不是 Video NACK")
        crc_received = struct.unpack('=I', data[-4:])[0]
        crc_calculated = zlib.crc32(data[:-4]) & 0xffffffff
        if crc_received != crc_calculated:
            raise ValueError("CRC 校验失败")
        num_chunks = struct.unpack('=H', data[9:11])[0]
        missing = []
        for i in range(num_chunks):
            offset = 11 + i * 2
            missing.append(struct.unpack('=H', data[offset:offset + 2])[0])
        return frame_id, missing

    @staticmethod
    def build_param_update(seq: int, t1: float, params: dict) -> bytes:
        """构建参数修改消息 — payload 为 JSON"""
        payload = json.dumps(params).encode('utf-8')
        header = struct.pack('=HBBBI', MAGIC, VERSION, MSG_TYPE_PARAM_UPDATE, 0, seq)
        timestamp = struct.pack('=d', t1)
        msg = header + timestamp + payload
        crc = zlib.crc32(msg) & 0xffffffff
        return msg + struct.pack('=I', crc)

    @staticmethod
    def build_param_query(seq: int, t1: float) -> bytes:
        """构建参数查询消息"""
        header = struct.pack('=HBBBI', MAGIC, VERSION, MSG_TYPE_PARAM_QUERY, 0, seq)
        timestamp = struct.pack('=d', t1)
        msg = header + timestamp
        crc = zlib.crc32(msg) & 0xffffffff
        return msg + struct.pack('=I', crc)
