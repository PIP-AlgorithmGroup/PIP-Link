"""
高精度延迟计算 - 四时间戳方案
"""

import time
from typing import Optional, Dict, Tuple
from collections import deque
from dataclasses import dataclass
import statistics


@dataclass
class LatencyResult:
    """延迟计算结果"""
    rtt: float  # 往返延迟（秒）
    offset: float  # 时钟偏移（秒）
    delay_up: float  # 上行延迟（秒）
    delay_down: float  # 下行延迟（秒）


class LatencyCalculator:
    """
    高精度延迟计算器 - 四时间戳方案

    原理：
    1. 客户端发送消息，记录 t1（发送时间）
    2. 机载端接收消息，记录 t2（接收时间）
    3. 机载端发送 ACK，记录 t3（发送时间）
    4. 客户端接收 ACK，记录 t4（接收时间）

    计算公式：
    - RTT = t4 - t1
    - offset = ((t2 - t1) + (t3 - t4)) / 2
    - delay_up = (t2 - t1) - offset
    - delay_down = (t4 - t3) - offset
    """

    def __init__(self, max_history: int = 100, timeout: float = 5.0):
        """
        初始化延迟计算器

        Args:
            max_history: 保留的历史记录数
            timeout: 超时时间（秒），超过此时间未收到 ACK 则清理
        """
        self.max_history = max_history
        self.timeout = timeout

        # 待处理的发送记录：{seq: (t1, send_time)}
        self.pending_sends: Dict[int, Tuple[float, float]] = {}

        # 延迟历史记录
        self.rtt_history = deque(maxlen=max_history)
        self.offset_history = deque(maxlen=max_history)
        self.delay_up_history = deque(maxlen=max_history)
        self.delay_down_history = deque(maxlen=max_history)

        # 统计数据
        self.total_measurements = 0
        self.filtered_outliers = 0

    def record_send(self, seq: int, t1: float) -> int:
        """
        记录发送时间

        Args:
            seq: 序列号
            t1: 发送时间戳（time.perf_counter()）

        Returns:
            序列号
        """
        current_time = time.perf_counter()
        self.pending_sends[seq] = (t1, current_time)

        # 清理超时的记录
        self._cleanup_timeout()

        return seq

    def record_ack(
        self,
        seq: int,
        t2: float,
        t3: float,
        t4: Optional[float] = None
    ) -> Optional[LatencyResult]:
        """
        记录 ACK 接收时间并计算延迟

        Args:
            seq: 序列号
            t2: 机载端接收时间戳
            t3: 机载端发送时间戳
            t4: 客户端接收时间戳（如果为 None，使用当前时间）

        Returns:
            LatencyResult 或 None（如果序列号不存在或异常）
        """
        if t4 is None:
            t4 = time.perf_counter()

        # 检查序列号是否存在
        if seq not in self.pending_sends:
            return None

        t1, send_time = self.pending_sends.pop(seq)

        # 计算延迟
        rtt = t4 - t1
        offset = ((t2 - t1) + (t3 - t4)) / 2
        delay_up = (t2 - t1) - offset
        delay_down = (t4 - t3) - offset

        # 异常值过滤（3σ 规则）
        if not self._is_outlier(rtt, delay_up, delay_down):
            self.rtt_history.append(rtt)
            self.offset_history.append(offset)
            self.delay_up_history.append(delay_up)
            self.delay_down_history.append(delay_down)
            self.total_measurements += 1
        else:
            self.filtered_outliers += 1

        return LatencyResult(
            rtt=rtt,
            offset=offset,
            delay_up=delay_up,
            delay_down=delay_down
        )

    def get_average_rtt(self) -> Optional[float]:
        """获取平均 RTT（秒）"""
        if not self.rtt_history:
            return None
        return statistics.mean(self.rtt_history)

    def get_average_delay_up(self) -> Optional[float]:
        """获取平均上行延迟（秒）"""
        if not self.delay_up_history:
            return None
        return statistics.mean(self.delay_up_history)

    def get_average_delay_down(self) -> Optional[float]:
        """获取平均下行延迟（秒）"""
        if not self.delay_down_history:
            return None
        return statistics.mean(self.delay_down_history)

    def get_average_offset(self) -> Optional[float]:
        """获取平均时钟偏移（秒）"""
        if not self.offset_history:
            return None
        return statistics.mean(self.offset_history)

    def get_min_rtt(self) -> Optional[float]:
        """获取最小 RTT（秒）"""
        if not self.rtt_history:
            return None
        return min(self.rtt_history)

    def get_max_rtt(self) -> Optional[float]:
        """获取最大 RTT（秒）"""
        if not self.rtt_history:
            return None
        return max(self.rtt_history)

    def get_stats(self) -> Dict[str, float]:
        """获取完整统计数据"""
        stats = {
            'total_measurements': self.total_measurements,
            'filtered_outliers': self.filtered_outliers,
            'pending_acks': len(self.pending_sends),
        }

        if self.rtt_history:
            stats['rtt_avg'] = statistics.mean(self.rtt_history)
            stats['rtt_min'] = min(self.rtt_history)
            stats['rtt_max'] = max(self.rtt_history)
            if len(self.rtt_history) > 1:
                stats['rtt_stdev'] = statistics.stdev(self.rtt_history)

        if self.delay_up_history:
            stats['delay_up_avg'] = statistics.mean(self.delay_up_history)
            stats['delay_up_min'] = min(self.delay_up_history)
            stats['delay_up_max'] = max(self.delay_up_history)

        if self.delay_down_history:
            stats['delay_down_avg'] = statistics.mean(self.delay_down_history)
            stats['delay_down_min'] = min(self.delay_down_history)
            stats['delay_down_max'] = max(self.delay_down_history)

        if self.offset_history:
            stats['offset_avg'] = statistics.mean(self.offset_history)

        return stats

    def _is_outlier(self, rtt: float, delay_up: float, delay_down: float) -> bool:
        """
        使用 3σ 规则检测异常值

        Args:
            rtt: 往返延迟
            delay_up: 上行延迟
            delay_down: 下行延迟

        Returns:
            True 如果是异常值
        """
        # 需要至少 2 个数据点才能计算标准差
        if len(self.rtt_history) < 2:
            return False

        # 检查 RTT
        rtt_mean = statistics.mean(self.rtt_history)
        rtt_stdev = statistics.stdev(self.rtt_history)
        if abs(rtt - rtt_mean) > 3 * rtt_stdev:
            return True

        # 检查上行延迟
        if len(self.delay_up_history) >= 2:
            delay_up_mean = statistics.mean(self.delay_up_history)
            delay_up_stdev = statistics.stdev(self.delay_up_history)
            if abs(delay_up - delay_up_mean) > 3 * delay_up_stdev:
                return True

        # 检查下行延迟
        if len(self.delay_down_history) >= 2:
            delay_down_mean = statistics.mean(self.delay_down_history)
            delay_down_stdev = statistics.stdev(self.delay_down_history)
            if abs(delay_down - delay_down_mean) > 3 * delay_down_stdev:
                return True

        return False

    def _cleanup_timeout(self):
        """清理超时的待处理记录"""
        current_time = time.perf_counter()
        expired_seqs = []

        for seq, (t1, send_time) in self.pending_sends.items():
            if current_time - send_time > self.timeout:
                expired_seqs.append(seq)

        for seq in expired_seqs:
            del self.pending_sends[seq]

    def reset(self):
        """重置所有统计数据"""
        self.pending_sends.clear()
        self.rtt_history.clear()
        self.offset_history.clear()
        self.delay_up_history.clear()
        self.delay_down_history.clear()
        self.total_measurements = 0
        self.filtered_outliers = 0
