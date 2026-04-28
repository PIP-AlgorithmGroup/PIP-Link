"""Reed-Solomon FEC 编解码器（基于 cm256 Cauchy Matrix GF(2^8)）

libcm256 编译安装：
    git clone https://github.com/catid/cm256 && cd cm256
    mkdir build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make && sudo make install
    sudo ldconfig
"""

import ctypes
import ctypes.util
import math
import logging
from typing import Dict, List, Optional

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# cm256 ctypes 绑定
# ---------------------------------------------------------------------------

class _CM256Params(ctypes.Structure):
    _fields_ = [
        ("OriginalCount", ctypes.c_int),
        ("RecoveryCount", ctypes.c_int),
        ("BlockBytes",    ctypes.c_int),
    ]


class _CM256Block(ctypes.Structure):
    """
    对应 C 结构体：
        struct cm256_block { void* Block; uint8_t Index; };
    64-bit 下 sizeof = 16（7 字节尾填充），ctypes 默认对齐与编译器一致。
    """
    _fields_ = [
        ("Block", ctypes.c_void_p),
        ("Index", ctypes.c_uint8),
    ]


_CM256_VERSION = 2  # cm256.h: #define CM256_VERSION 2


def _load_cm256() -> Optional[ctypes.CDLL]:
    candidates = [
        "libcm256.so",
        "libcm256.so.1",
        ctypes.util.find_library("cm256"),
    ]
    for name in candidates:
        if name is None:
            continue
        try:
            lib = ctypes.CDLL(name)
            # cm256_init 是宏：#define cm256_init() cm256_init_(CM256_VERSION)
            # 实际导出符号是 cm256_init_，返回 bool（true = 失败）
            lib.cm256_init_.restype  = ctypes.c_bool
            lib.cm256_init_.argtypes = [ctypes.c_int]
            lib.cm256_encode.restype  = ctypes.c_int
            lib.cm256_encode.argtypes = [
                _CM256Params,
                ctypes.POINTER(_CM256Block),
                ctypes.c_void_p,
            ]
            lib.cm256_decode.restype  = ctypes.c_int
            lib.cm256_decode.argtypes = [
                _CM256Params,
                ctypes.POINTER(_CM256Block),
            ]
            if lib.cm256_init_(_CM256_VERSION):   # true = 失败
                logger.warning("cm256_init_ failed for %s (SSSE3/NEON not supported?)", name)
                continue
            logger.info("cm256 loaded: %s", name)
            return lib
        except OSError:
            continue
    return None


_cm256: Optional[ctypes.CDLL] = _load_cm256()
CM256_AVAILABLE: bool = _cm256 is not None
FEC_AVAILABLE: bool = CM256_AVAILABLE   # 向后兼容别名

if not CM256_AVAILABLE:
    logger.warning(
        "libcm256.so not found — FEC disabled. "
        "Build: https://github.com/catid/cm256"
    )


# ---------------------------------------------------------------------------
# 内部：构造可写 block 数组
# ---------------------------------------------------------------------------

def _alloc_block_array(
    chunks: List[bytes],
    indices: List[int],
    block_size: int,
) -> tuple:
    """
    为每个 chunk 分配大小为 block_size 的可写 C buffer（零填充），
    返回 (CM256Block_array, buffer_list)。
    调用方必须持有 buffer_list 引用，防止 GC 回收。
    """
    n = len(chunks)
    arr = (_CM256Block * n)()
    bufs: List[ctypes.Array] = []
    for i, chunk in enumerate(chunks):
        buf = ctypes.create_string_buffer(block_size)
        buf.raw = chunk[:block_size].ljust(block_size, b'\x00')
        bufs.append(buf)
        arr[i].Block = ctypes.cast(buf, ctypes.c_void_p)
        arr[i].Index = indices[i]
    return arr, bufs


# ---------------------------------------------------------------------------
# FECEncoder
# ---------------------------------------------------------------------------

class FECEncoder:
    """FEC 编码器：对 N 个 data chunks 生成 K 个 parity chunks（cm256）"""

    def __init__(self, redundancy: float = 0.2):
        self.redundancy = redundancy

    def encode(self, chunks: List[bytes]) -> List[bytes]:
        """
        输入 N 个 data chunks，输出 N+K 个 chunks（原始 + parity）。
        K = max(1, ceil(N * redundancy))，约束 N+K ≤ 256。
        cm256 不可用时降级为直接返回原始 chunks（FEC 关闭）。
        """
        if not CM256_AVAILABLE or not chunks:
            return chunks

        n = len(chunks)
        k = max(1, math.ceil(n * self.redundancy))
        k = min(k, 256 - n)   # N+K ≤ 256
        if k <= 0:
            return chunks

        block_size = max(len(c) for c in chunks)

        orig_arr, orig_bufs = _alloc_block_array(chunks, list(range(n)), block_size)

        # K 个 recovery block 连续存放
        recovery_buf = ctypes.create_string_buffer(k * block_size)

        params = _CM256Params(n, k, block_size)
        ret = _cm256.cm256_encode(
            params,
            orig_arr,
            ctypes.cast(recovery_buf, ctypes.c_void_p),
        )
        if ret != 0:
            logger.error("cm256_encode failed: %d", ret)
            return chunks

        # data chunks 保留原始长度（不零填充），parity 固定为 block_size
        result: List[bytes] = list(chunks)
        for i in range(k):
            offset = i * block_size
            result.append(bytes(recovery_buf[offset : offset + block_size]))

        return result


# ---------------------------------------------------------------------------
# FECDecoder
# ---------------------------------------------------------------------------

class FECDecoder:
    """FEC 解码器：从任意 N 个 chunks（含 parity）恢复原始数据（cm256）"""

    def __init__(self, redundancy: float = 0.2):
        self.redundancy = redundancy

    def decode(
        self,
        received: Dict[int, bytes],
        n_data: int,
        n_total: int,
        chunk_sizes: Optional[Dict[int, int]] = None,
    ) -> Optional[List[bytes]]:
        """
        received    : {chunk_idx: chunk_bytes}（data + parity 混合）
        n_data      : 原始 data chunk 数（wire 上的 orig_chunks 字段）
        n_total     : 含 parity 的总 chunk 数（wire 上的 total_chunks 字段）
        chunk_sizes : {chunk_idx: 实际字节数}，用于裁去 data chunk 的尾部零填充

        返回按序排列的 n_data 个 data chunks，无法恢复时返回 None。
        """
        if not CM256_AVAILABLE:
            return None
        if len(received) < n_data:
            return None

        # 快速路径：已拥有全部 data chunks，跳过 cm256
        if all(i in received for i in range(n_data)):
            result = []
            for i in range(n_data):
                data = received[i]
                if chunk_sizes and i in chunk_sizes:
                    data = data[:chunk_sizes[i]]
                result.append(data)
            return result

        k = n_total - n_data
        if k <= 0:
            return None

        # block_size：优先从 parity block 推断（parity 永远是完整 block_size）
        block_size = 0
        for idx in range(n_data, n_total):
            if idx in received:
                block_size = len(received[idx])
                break
        if block_size == 0:
            # 退化：所有收到的都是 data block（不应走到这里，防御）
            block_size = max(len(v) for v in received.values())

        # 取任意 n_data 个 block（cm256 要求恰好 n_data 个）
        selected = list(received.items())[:n_data]

        block_arr, bufs = _alloc_block_array(
            [data for _, data in selected],
            [idx  for idx, _ in selected],
            block_size,
        )

        params = _CM256Params(n_data, k, block_size)
        ret = _cm256.cm256_decode(params, block_arr)
        if ret != 0:
            logger.debug("cm256_decode failed: %d", ret)
            return None

        # decode 后：block_arr[slot].Index = 该 slot 恢复的原始 block 编号
        #            bufs[slot].raw         = 对应恢复数据
        result_map: Dict[int, bytes] = {}
        for slot in range(n_data):
            orig_idx = block_arr[slot].Index
            data = bytes(bufs[slot].raw)
            if chunk_sizes and orig_idx in chunk_sizes:
                data = data[:chunk_sizes[orig_idx]]
            result_map[orig_idx] = data

        if len(result_map) < n_data or not all(i in result_map for i in range(n_data)):
            return None

        return [result_map[i] for i in range(n_data)]
