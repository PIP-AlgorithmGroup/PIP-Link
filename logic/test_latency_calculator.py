"""
LatencyCalculator 单元测试
"""

import pytest
import time
from logic.latency_calculator import LatencyCalculator, LatencyResult


class TestLatencyCalculatorBasic:
    """基础功能测试"""

    def test_record_send_and_ack(self):
        """测试记录发送和 ACK"""
        calc = LatencyCalculator()

        # 模拟发送
        t1 = time.perf_counter()
        seq = calc.record_send(1, t1)
        assert seq == 1

        # 模拟接收 ACK
        time.sleep(0.01)  # 模拟网络延迟
        t2 = t1 + 0.005
        t3 = t1 + 0.010
        t4 = time.perf_counter()

        result = calc.record_ack(seq, t2, t3, t4)

        assert result is not None
        assert result.rtt > 0
        assert result.delay_up > 0
        assert result.delay_down > 0

    def test_multiple_measurements(self):
        """测试多次测量"""
        calc = LatencyCalculator()

        for i in range(10):
            t1 = time.perf_counter()
            seq = calc.record_send(i, t1)

            time.sleep(0.001)
            t2 = t1 + 0.0005
            t3 = t1 + 0.0010
            t4 = time.perf_counter()

            result = calc.record_ack(seq, t2, t3, t4)
            assert result is not None

        # 验证统计数据
        assert calc.total_measurements == 10
        assert calc.get_average_rtt() is not None
        assert calc.get_average_delay_up() is not None
        assert calc.get_average_delay_down() is not None

    def test_nonexistent_seq(self):
        """测试不存在的序列号"""
        calc = LatencyCalculator()

        t1 = time.perf_counter()
        t2 = t1 + 0.005
        t3 = t1 + 0.010
        t4 = time.perf_counter()

        # 尝试记录不存在的序列号
        result = calc.record_ack(999, t2, t3, t4)
        assert result is None


class TestLatencyCalculatorOutlierFiltering:
    """异常值过滤测试"""

    def test_outlier_detection(self):
        """测试异常值检测"""
        calc = LatencyCalculator()

        # 记录 10 个正常的测量
        for i in range(10):
            t1 = time.perf_counter()
            seq = calc.record_send(i, t1)

            t2 = t1 + 0.010
            t3 = t1 + 0.015
            t4 = time.perf_counter()

            calc.record_ack(seq, t2, t3, t4)

        normal_count = calc.total_measurements

        # 记录一个异常值（极大的 RTT）
        t1 = time.perf_counter()
        seq = calc.record_send(100, t1)

        t2 = t1 + 1.0  # 极大的延迟
        t3 = t1 + 1.5
        t4 = time.perf_counter()

        calc.record_ack(seq, t2, t3, t4)

        # 异常值应该被过滤
        assert calc.total_measurements == normal_count
        assert calc.filtered_outliers == 1

    def test_no_filtering_with_few_samples(self):
        """测试样本少时不过滤"""
        calc = LatencyCalculator()

        # 第一个测量不应该被过滤（没有参考数据）
        t1 = time.perf_counter()
        seq = calc.record_send(1, t1)

        t2 = t1 + 0.010
        t3 = t1 + 0.015
        t4 = time.perf_counter()

        calc.record_ack(seq, t2, t3, t4)

        assert calc.total_measurements == 1
        assert calc.filtered_outliers == 0


class TestLatencyCalculatorTimeout:
    """超时清理测试"""

    def test_timeout_cleanup(self):
        """测试超时记录清理"""
        calc = LatencyCalculator(timeout=0.1)

        # 记录发送
        t1 = time.perf_counter()
        seq = calc.record_send(1, t1)

        # 等待超时
        time.sleep(0.15)

        # 再记录一个发送（触发清理）
        t1_new = time.perf_counter()
        seq_new = calc.record_send(2, t1_new)

        # 第一个序列号应该被清理
        assert 1 not in calc.pending_sends
        assert 2 in calc.pending_sends


class TestLatencyCalculatorStatistics:
    """统计数据测试"""

    def test_statistics(self):
        """测试统计数据"""
        calc = LatencyCalculator()

        # 记录 5 个测量
        for i in range(5):
            t1 = time.perf_counter()
            seq = calc.record_send(i, t1)

            t2 = t1 + 0.010
            t3 = t1 + 0.015
            t4 = time.perf_counter()

            calc.record_ack(seq, t2, t3, t4)

        # 获取统计数据
        stats = calc.get_stats()

        assert stats['total_measurements'] == 5
        assert 'rtt_avg' in stats
        assert 'rtt_min' in stats
        assert 'rtt_max' in stats
        assert 'delay_up_avg' in stats
        assert 'delay_down_avg' in stats

    def test_empty_statistics(self):
        """测试空统计数据"""
        calc = LatencyCalculator()

        assert calc.get_average_rtt() is None
        assert calc.get_average_delay_up() is None
        assert calc.get_average_delay_down() is None

        stats = calc.get_stats()
        assert stats['total_measurements'] == 0


class TestLatencyCalculatorReset:
    """重置测试"""

    def test_reset(self):
        """测试重置"""
        calc = LatencyCalculator()

        # 记录一些测量
        for i in range(5):
            t1 = time.perf_counter()
            seq = calc.record_send(i, t1)

            t2 = t1 + 0.010
            t3 = t1 + 0.015
            t4 = time.perf_counter()

            calc.record_ack(seq, t2, t3, t4)

        assert calc.total_measurements == 5

        # 重置
        calc.reset()

        assert calc.total_measurements == 0
        assert calc.get_average_rtt() is None
        assert len(calc.pending_sends) == 0


class TestLatencyCalculatorClockOffset:
    """时钟偏移测试"""

    def test_clock_offset_calculation(self):
        """测试时钟偏移计算"""
        calc = LatencyCalculator()

        # 先记录几个正常的测量建立基线
        for i in range(3):
            t1 = time.perf_counter()
            seq = calc.record_send(i, t1)
            t2 = t1 + 0.010
            t3 = t1 + 0.015
            t4 = t1 + 0.020
            calc.record_ack(seq, t2, t3, t4)

        # 记录一个测量并验证时钟偏移计算
        t1 = time.perf_counter()
        seq = calc.record_send(100, t1)
        t2 = t1 + 0.010
        t3 = t1 + 0.015
        t4 = t1 + 0.020

        result = calc.record_ack(seq, t2, t3, t4)

        # 验证时钟偏移计算
        # offset = ((t2 - t1) + (t3 - t4)) / 2
        # offset = ((0.010) + (-0.005)) / 2 = 0.0025
        assert result is not None
        expected_offset = ((0.010) + (-0.005)) / 2
        assert abs(result.offset - expected_offset) < 1e-6


class TestLatencyCalculatorAsymmetricNetwork:
    """非对称网络测试"""

    def test_asymmetric_delay(self):
        """测试非对称延迟检测"""
        calc = LatencyCalculator()

        # 上行延迟 5ms，下行延迟 20ms
        t1 = 1000.0
        t2 = 1000.005  # 上行 5ms
        t3 = 1000.010
        t4 = 1000.030  # 下行 20ms

        seq = calc.record_send(1, t1)
        result = calc.record_ack(seq, t2, t3, t4)

        assert result is not None
        assert result.delay_up < result.delay_down


class TestLatencyCalculatorMaxHistory:
    """历史记录限制测试"""

    def test_max_history_limit(self):
        """测试历史记录最大数量限制"""
        calc = LatencyCalculator(max_history=5)

        # 记录 10 个测量（使用一致的延迟避免异常值过滤）
        for i in range(10):
            t1 = 1000.0 + i * 0.1
            seq = calc.record_send(i, t1)

            t2 = t1 + 0.010
            t3 = t1 + 0.015
            t4 = t1 + 0.020

            calc.record_ack(seq, t2, t3, t4)

        # 历史记录应该只保留最后 5 个
        assert len(calc.rtt_history) == 5
        # 总测量数应该是 10（即使历史记录只保留 5 个）
        assert calc.total_measurements == 10


if __name__ == '__main__':
    pytest.main([__file__, '-v'])
