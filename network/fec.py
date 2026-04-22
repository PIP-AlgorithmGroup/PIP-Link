"""Reed-Solomon FEC 编解码器"""

import math
import logging
import numpy as np
from typing import List, Dict, Optional

logger = logging.getLogger(__name__)

try:
    from reedsolo import RSCodec, ReedSolomonError
    FEC_AVAILABLE = True
except ImportError:
    FEC_AVAILABLE = False
    logger.warning("reedsolo not installed, FEC disabled")


def _xor_chunks(chunks: List[bytes], size: int) -> bytes:
    """XOR all chunks together (numpy-accelerated)."""
    result = np.zeros(size, dtype=np.uint8)
    for c in chunks:
        arr = np.frombuffer(c, dtype=np.uint8)
        result[:len(arr)] ^= arr
    return result.tobytes()


class FECEncoder:
    """FEC 编码器 - 对一组 data chunks 生成 parity chunks"""

    def __init__(self, redundancy: float = 0.2):
        self.redundancy = redundancy

    def encode(self, chunks: List[bytes]) -> List[bytes]:
        """
        输入 N 个 data chunks，输出 N+K 个 chunks（原始 + parity）。
        K = ceil(N * redundancy), 至少 1。
        """
        if not FEC_AVAILABLE or not chunks:
            return chunks

        n = len(chunks)
        k = max(1, math.ceil(n * self.redundancy))
        max_size = max(len(c) for c in chunks)

        # k=1: fast XOR parity (single pass, no RS overhead)
        if k == 1:
            parity = _xor_chunks(chunks, max_size)
            return chunks + [parity]

        # k>1: per-column RS encoding (slow but correct)
        padded = [c.ljust(max_size, b'\x00') for c in chunks]
        rs = RSCodec(k)
        parity_chunks = [bytearray(max_size) for _ in range(k)]

        for col in range(max_size):
            column_data = bytes(padded[row][col] for row in range(n))
            encoded = rs.encode(column_data)
            parity_bytes = encoded[n:]
            for p_idx in range(k):
                parity_chunks[p_idx][col] = parity_bytes[p_idx]

        return chunks + [bytes(p) for p in parity_chunks]


class FECDecoder:
    """FEC 解码器 - 从部分 chunks 恢复完整数据"""

    def __init__(self, redundancy: float = 0.2):
        self.redundancy = redundancy

    def decode(self, received: Dict[int, bytes], n_data: int, n_total: int,
               chunk_sizes: Optional[Dict[int, int]] = None) -> Optional[List[bytes]]:
        """
        尝试从收到的 chunks 恢复原始 N 个 data chunks。
        """
        if not FEC_AVAILABLE:
            return None

        k = n_total - n_data
        if len(received) < n_data:
            return None

        # 如果已有所有 data chunks，直接返回
        data_chunks = {}
        for idx in range(n_data):
            if idx in received:
                data_chunks[idx] = received[idx]
        if len(data_chunks) == n_data:
            return [data_chunks[i] for i in range(n_data)]

        # k=1 XOR fast path: recover the single missing data chunk
        if k == 1:
            missing = [i for i in range(n_data) if i not in received]
            parity_idx = n_data  # parity is at index n_data
            if len(missing) == 1 and parity_idx in received:
                max_size = max(len(v) for v in received.values())
                present = [received[i].ljust(max_size, b'\x00')
                           for i in range(n_data) if i in received]
                parity = received[parity_idx].ljust(max_size, b'\x00')
                recovered = _xor_chunks(present + [parity], max_size)
                orig_size = chunk_sizes.get(missing[0], max_size) if chunk_sizes else max_size
                result = []
                for i in range(n_data):
                    if i == missing[0]:
                        result.append(recovered[:orig_size])
                    elif chunk_sizes and i in chunk_sizes:
                        result.append(received[i][:chunk_sizes[i]])
                    else:
                        result.append(received[i])
                return result
            return None

        # k>1: per-column RS decoding
        max_size = max(len(v) for v in received.values())
        padded_received = {}
        for idx, chunk in received.items():
            padded_received[idx] = chunk.ljust(max_size, b'\x00')

        rs = RSCodec(k)
        recovered = [bytearray(max_size) for _ in range(n_data)]

        try:
            for col in range(max_size):
                full_column = bytearray(n_total)
                erase_pos = []
                for idx in range(n_total):
                    if idx in padded_received:
                        full_column[idx] = padded_received[idx][col]
                    else:
                        erase_pos.append(idx)

                if len(erase_pos) > k:
                    return None

                decoded = rs.decode(bytes(full_column), erase_pos=erase_pos)
                for row in range(n_data):
                    recovered[row][col] = decoded[0][row]

            result = []
            for i in range(n_data):
                if chunk_sizes and i in chunk_sizes:
                    result.append(bytes(recovered[i][:chunk_sizes[i]]))
                else:
                    result.append(bytes(recovered[i]))
            return result

        except (ReedSolomonError, Exception) as e:
            logger.debug(f"FEC decode failed: {e}")
            return None
