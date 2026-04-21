"""键盘编码器 - 使用 Win32 GetAsyncKeyState 直接轮询硬件状态"""

import ctypes
from typing import Optional


class KeyboardEncoder:
    """键盘状态编码器 - 轮询式，无事件丢失"""

    _user32 = ctypes.windll.user32

    # bit_index -> Windows Virtual Key Code
    _VK_MAP = {
        # Byte 0: 功能键 (0-7)
        0: 0x1B,    # ESC
        1: 0x70,    # F1
        2: 0x71,    # F2
        3: 0x72,    # F3
        4: 0x73,    # F4
        # bit 5: F5 保留给 READY 切换，不进入位图
        6: 0x75,    # F6
        7: 0x76,    # F7

        # Byte 1: 功能键 + 数字 (8-15)
        8: 0x77,    # F8
        9: 0x78,    # F9
        10: 0x79,   # F10
        11: 0x7A,   # F11
        12: 0x7B,   # F12
        13: 0xC0,   # `
        14: 0x31,   # 1
        15: 0x32,   # 2

        # Byte 2: 数字键 (16-23)
        16: 0x33,   # 3
        17: 0x34,   # 4
        18: 0x35,   # 5
        19: 0x36,   # 6
        20: 0x37,   # 7
        21: 0x38,   # 8
        22: 0x39,   # 9
        23: 0x30,   # 0

        # Byte 3: 符号 + 字母 (24-31)
        24: 0xBD,   # -
        25: 0xBB,   # =
        26: 0x08,   # Backspace
        27: 0x09,   # Tab
        28: 0x51,   # Q
        29: 0x57,   # W
        30: 0x45,   # E
        31: 0x52,   # R

        # Byte 4: 字母 (32-39)
        32: 0x54,   # T
        33: 0x59,   # Y
        34: 0x55,   # U
        35: 0x49,   # I
        36: 0x4F,   # O
        37: 0x50,   # P
        38: 0xDB,   # [
        39: 0xDD,   # ]

        # Byte 5: 符号 + 字母 (40-47)
        40: 0xDC,   # \
        41: 0x14,   # Caps Lock
        42: 0x41,   # A
        43: 0x53,   # S
        44: 0x44,   # D
        45: 0x46,   # F
        46: 0x47,   # G
        47: 0x48,   # H

        # Byte 6: 字母 (48-55)
        48: 0x4A,   # J
        49: 0x4B,   # K
        50: 0x4C,   # L
        51: 0xBA,   # ;
        52: 0xDE,   # '
        53: 0x0D,   # Enter
        54: 0xA0,   # Left Shift
        55: 0x5A,   # Z

        # Byte 7: 字母 (56-63)
        56: 0x58,   # X
        57: 0x43,   # C
        58: 0x56,   # V
        59: 0x42,   # B
        60: 0x4E,   # N
        61: 0x4D,   # M
        62: 0xBC,   # ,
        63: 0xBE,   # .

        # Byte 8: 符号 + 修饰键 (64-70)
        64: 0xBF,   # /
        65: 0xA1,   # Right Shift
        66: 0xA2,   # Left Ctrl
        67: 0xA4,   # Left Alt
        68: 0x20,   # Space
        69: 0xA5,   # Right Alt
        70: 0xA3,   # Right Ctrl
    }

    _VK_F5 = 0x74

    BIT_NAMES = {
        0: "ESC", 1: "F1", 2: "F2", 3: "F3", 4: "F4", 6: "F6", 7: "F7",
        8: "F8", 9: "F9", 10: "F10", 11: "F11", 12: "F12",
        13: "`", 14: "1", 15: "2", 16: "3", 17: "4", 18: "5",
        19: "6", 20: "7", 21: "8", 22: "9", 23: "0",
        24: "-", 25: "=", 26: "BS", 27: "TAB",
        28: "Q", 29: "W", 30: "E", 31: "R", 32: "T", 33: "Y",
        34: "U", 35: "I", 36: "O", 37: "P", 38: "[", 39: "]",
        40: "\\", 41: "CAPS", 42: "A", 43: "S", 44: "D", 45: "F",
        46: "G", 47: "H", 48: "J", 49: "K", 50: "L", 51: ";",
        52: "'", 53: "ENT", 54: "LSHF", 55: "Z", 56: "X", 57: "C",
        58: "V", 59: "B", 60: "N", 61: "M", 62: ",", 63: ".",
        64: "/", 65: "RSHF", 66: "LCTL", 67: "LALT",
        68: "SPC", 69: "RALT", 70: "RCTL",
    }

    def __init__(self):
        self.on_f5_pressed: Optional[callable] = None
        self._f5_was_down = False

    def start(self):
        pass

    def stop(self):
        self._f5_was_down = False

    def get_state(self) -> bytes:
        """轮询所有映射键，返回 10 字节位图"""
        f5_down = bool(self._user32.GetAsyncKeyState(self._VK_F5) & 0x8000)
        if f5_down and not self._f5_was_down:
            if self.on_f5_pressed:
                self.on_f5_pressed()
        self._f5_was_down = f5_down

        state = bytearray(10)
        for bit_index, vk in self._VK_MAP.items():
            if self._user32.GetAsyncKeyState(vk) & 0x8000:
                state[bit_index // 8] |= (1 << (bit_index % 8))
        return bytes(state)

    def get_pressed_count(self) -> int:
        count = 0
        for vk in self._VK_MAP.values():
            if self._user32.GetAsyncKeyState(vk) & 0x8000:
                count += 1
        return count