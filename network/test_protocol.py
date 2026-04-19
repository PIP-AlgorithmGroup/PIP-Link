"""
Protocol 类单元测试
"""

import pytest
import time
from network.protocol import (
    Protocol,
    MSG_TYPE_CONTROL_COMMAND,
    MSG_TYPE_ACK,
    MSG_TYPE_HEARTBEAT,
    MAGIC,
    VERSION
)


class TestProtocolControlCommand:
    """控制指令消息测试"""

    def test_build_and_parse_control_command(self):
        """测试控制指令的编码和解码"""
        seq = 1
        t1 = time.perf_counter()
        forward = 1.0
        turn = 0.5
        action = 1
        sprint = 0.8

        # 构建消息
        message = Protocol.build_control_command(
            seq=seq,
            t1=t1,
            forward=forward,
            turn=turn,
            action=action,
            sprint=sprint
        )

        # 验证消息长度
        assert len(message) == 37  # header(9) + t1(8) + payload(16) + crc(4)

        # 解析消息
        msg_type, seq_recv, t1_recv, payload = Protocol.parse_message(message)

        # 验证消息类型和序列号
        assert msg_type == MSG_TYPE_CONTROL_COMMAND
        assert seq_recv == seq

        # 验证时间戳精度（浮点数）
        assert abs(t1_recv - t1) < 1e-6

        # 验证 payload
        assert payload is not None
        assert len(payload) == 16

    def test_control_command_with_zero_values(self):
        """测试零值控制指令"""
        seq = 2
        t1 = time.perf_counter()

        message = Protocol.build_control_command(seq=seq, t1=t1)
        msg_type, seq_recv, t1_recv, payload = Protocol.parse_message(message)

        assert msg_type == MSG_TYPE_CONTROL_COMMAND
        assert seq_recv == seq

    def test_control_command_with_negative_values(self):
        """测试负值控制指令"""
        seq = 3
        t1 = time.perf_counter()
        forward = -1.0
        turn = -0.5

        message = Protocol.build_control_command(
            seq=seq,
            t1=t1,
            forward=forward,
            turn=turn
        )
        msg_type, seq_recv, t1_recv, payload = Protocol.parse_message(message)

        assert msg_type == MSG_TYPE_CONTROL_COMMAND
        assert seq_recv == seq


class TestProtocolACK:
    """ACK 消息测试"""

    def test_build_and_parse_ack(self):
        """测试 ACK 消息的编码和解码"""
        seq = 1
        t2 = time.perf_counter()
        time.sleep(0.01)  # 模拟网络延迟
        t3 = time.perf_counter()

        # 构建 ACK
        message = Protocol.build_ack(seq=seq, t2=t2, t3=t3)

        # 验证消息长度
        assert len(message) == 29  # header(9) + t2(8) + t3(8) + crc(4)

        # 解析 ACK
        seq_recv, t2_recv, t3_recv = Protocol.parse_ack(message)

        # 验证序列号
        assert seq_recv == seq

        # 验证时间戳精度
        assert abs(t2_recv - t2) < 1e-6
        assert abs(t3_recv - t3) < 1e-6

        # 验证时间顺序
        assert t3_recv > t2_recv

    def test_ack_with_large_seq(self):
        """测试大序列号 ACK"""
        seq = 0xFFFFFFFF  # 最大 32 位无符号整数
        t2 = time.perf_counter()
        t3 = time.perf_counter()

        message = Protocol.build_ack(seq=seq, t2=t2, t3=t3)
        seq_recv, t2_recv, t3_recv = Protocol.parse_ack(message)

        assert seq_recv == seq


class TestProtocolHeartbeat:
    """心跳消息测试"""

    def test_build_and_parse_heartbeat(self):
        """测试心跳消息的编码和解码"""
        seq = 1
        t1 = time.perf_counter()

        # 构建心跳
        message = Protocol.build_heartbeat(seq=seq, t1=t1)

        # 验证消息长度
        assert len(message) == 21  # header(9) + t1(8) + crc(4)

        # 解析心跳
        seq_recv, t1_recv = Protocol.parse_heartbeat(message)

        # 验证序列号和时间戳
        assert seq_recv == seq
        assert abs(t1_recv - t1) < 1e-6


class TestProtocolCRC:
    """CRC 校验测试"""

    def test_crc_validation_control_command(self):
        """测试控制指令 CRC 校验"""
        seq = 1
        t1 = time.perf_counter()

        message = Protocol.build_control_command(seq=seq, t1=t1)

        # 正常解析应该成功
        msg_type, seq_recv, t1_recv, payload = Protocol.parse_message(message)
        assert msg_type == MSG_TYPE_CONTROL_COMMAND

        # 修改消息内容，CRC 应该失败
        corrupted_message = bytearray(message)
        corrupted_message[15] ^= 0xFF  # 翻转一个字节

        with pytest.raises(ValueError, match="CRC 校验失败"):
            Protocol.parse_message(bytes(corrupted_message))

    def test_crc_validation_ack(self):
        """测试 ACK CRC 校验"""
        seq = 1
        t2 = time.perf_counter()
        t3 = time.perf_counter()

        message = Protocol.build_ack(seq=seq, t2=t2, t3=t3)

        # 正常解析应该成功
        seq_recv, t2_recv, t3_recv = Protocol.parse_ack(message)
        assert seq_recv == seq

        # 修改消息内容，CRC 应该失败
        corrupted_message = bytearray(message)
        corrupted_message[12] ^= 0xFF  # 翻转一个字节

        with pytest.raises(ValueError, match="CRC 校验失败"):
            Protocol.parse_ack(bytes(corrupted_message))


class TestProtocolErrors:
    """错误处理测试"""

    def test_parse_message_too_short(self):
        """测试消息过短"""
        with pytest.raises(ValueError, match="消息太短"):
            Protocol.parse_message(b"short")

    def test_parse_message_invalid_magic(self):
        """测试无效的 Magic"""
        # 构建一个有效的消息，然后修改 Magic
        seq = 1
        t1 = time.perf_counter()
        message = Protocol.build_control_command(seq=seq, t1=t1)

        # 修改 Magic
        corrupted_message = bytearray(message)
        corrupted_message[0] = 0xFF
        corrupted_message[1] = 0xFF

        with pytest.raises(ValueError, match="Magic 错误"):
            Protocol.parse_message(bytes(corrupted_message))

    def test_parse_ack_too_short(self):
        """测试 ACK 消息过短"""
        with pytest.raises(ValueError, match="ACK 消息太短"):
            Protocol.parse_ack(b"short")

    def test_parse_heartbeat_too_short(self):
        """测试心跳消息过短"""
        with pytest.raises(ValueError, match="心跳消息太短"):
            Protocol.parse_heartbeat(b"short")


class TestProtocolTimestampPrecision:
    """时间戳精度测试"""

    def test_timestamp_precision_microseconds(self):
        """测试时间戳微秒级精度"""
        seq = 1
        t1 = time.perf_counter()

        message = Protocol.build_control_command(seq=seq, t1=t1)
        msg_type, seq_recv, t1_recv, payload = Protocol.parse_message(message)

        # 验证精度在 1 微秒以内
        precision_error = abs(t1_recv - t1)
        assert precision_error < 1e-6, f"精度误差: {precision_error} 秒"

    def test_timestamp_precision_ack(self):
        """测试 ACK 时间戳精度"""
        seq = 1
        t2 = time.perf_counter()
        t3 = time.perf_counter()

        message = Protocol.build_ack(seq=seq, t2=t2, t3=t3)
        seq_recv, t2_recv, t3_recv = Protocol.parse_ack(message)

        # 验证精度在 1 微秒以内
        assert abs(t2_recv - t2) < 1e-6
        assert abs(t3_recv - t3) < 1e-6


class TestProtocolSequenceNumbers:
    """序列号测试"""

    def test_sequence_number_range(self):
        """测试序列号范围"""
        t1 = time.perf_counter()

        # 测试最小值
        message = Protocol.build_control_command(seq=0, t1=t1)
        msg_type, seq_recv, t1_recv, payload = Protocol.parse_message(message)
        assert seq_recv == 0

        # 测试最大值
        message = Protocol.build_control_command(seq=0xFFFFFFFF, t1=t1)
        msg_type, seq_recv, t1_recv, payload = Protocol.parse_message(message)
        assert seq_recv == 0xFFFFFFFF

        # 测试中间值
        message = Protocol.build_control_command(seq=0x12345678, t1=t1)
        msg_type, seq_recv, t1_recv, payload = Protocol.parse_message(message)
        assert seq_recv == 0x12345678


if __name__ == '__main__':
    pytest.main([__file__, '-v'])
