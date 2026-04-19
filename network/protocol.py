"""
协议编解码 - 消息序列化/反序列化和 CRC 校验
"""

import struct
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


@dataclass
class ControlCommand:
    """控制指令"""
    seq: int
    t1: float
    forward: float = 0.0
    turn: float = 0.0
    action: int = 0
    sprint: float = 0.0

    def to_bytes(self) -> bytes:
        """序列化为字节"""
        return struct.pack(
            '=fffi',
            self.forward,
            self.turn,
            self.sprint,
            self.action
        )

    @staticmethod
    def from_bytes(data: bytes) -> 'ControlCommand':
        """从字节反序列化"""
        forward, turn, sprint, action = struct.unpack('=fffi', data)
        return ControlCommand(
            seq=0,
            t1=0.0,
            forward=forward,
            turn=turn,
            action=action,
            sprint=sprint
        )


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
        forward: float = 0.0,
        turn: float = 0.0,
        action: int = 0,
        sprint: float = 0.0
    ) -> bytes:
        """
        构建控制指令消息

        格式：
        [Magic:2][Version:1][MsgType:1][Reserved:1][Seq:4][t1:8][Payload:var][CRC32:4]
        """
        # 构建 payload
        payload = struct.pack(
            '=fffi',
            forward,
            turn,
            sprint,
            action
        )

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
        message_without_crc = header + timestamp + payload

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
