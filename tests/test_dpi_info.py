"""Dump display info: pygame vs Win32 — diagnose DPI scaling mismatch"""
import pygame
import ctypes
import ctypes.wintypes

# Enable per-monitor DPI awareness BEFORE pygame init
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)  # PROCESS_PER_MONITOR_DPI_AWARE
    print("DPI awareness: per-monitor v1")
except Exception:
    try:
        ctypes.windll.user32.SetProcessDPIAware()
        print("DPI awareness: system-level")
    except Exception:
        print("DPI awareness: NONE")

pygame.init()
pygame.display.set_mode((640, 480))

print(f"\npygame.display.get_num_displays() = {pygame.display.get_num_displays()}")
print(f"pygame.display.get_desktop_sizes() = {pygame.display.get_desktop_sizes()}")

# Win32: EnumDisplayMonitors
MONITORINFOF_PRIMARY = 0x00000001

class MONITORINFOEX(ctypes.Structure):
    _fields_ = [
        ("cbSize", ctypes.wintypes.DWORD),
        ("rcMonitor", ctypes.wintypes.RECT),
        ("rcWork", ctypes.wintypes.RECT),
        ("dwFlags", ctypes.wintypes.DWORD),
        ("szDevice", ctypes.c_wchar * 32),
    ]

monitors = []
def _enum_cb(hMonitor, hdcMonitor, lprcMonitor, dwData):
    info = MONITORINFOEX()
    info.cbSize = ctypes.sizeof(MONITORINFOEX)
    ctypes.windll.user32.GetMonitorInfoW(hMonitor, ctypes.byref(info))

    # Get DPI for this monitor
    dpiX = ctypes.c_uint()
    dpiY = ctypes.c_uint()
    try:
        ctypes.windll.shcore.GetDpiForMonitor(hMonitor, 0, ctypes.byref(dpiX), ctypes.byref(dpiY))
        dpi = dpiX.value
    except Exception:
        dpi = 96

    m = info.rcMonitor
    w = info.rcWork
    primary = bool(info.dwFlags & MONITORINFOF_PRIMARY)
    monitors.append({
        "device": info.szDevice,
        "monitor": (m.left, m.top, m.right, m.bottom),
        "work": (w.left, w.top, w.right, w.bottom),
        "primary": primary,
        "dpi": dpi,
        "scale": dpi / 96.0,
    })
    return True

MONITORENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(ctypes.wintypes.RECT), ctypes.c_long)
ctypes.windll.user32.EnumDisplayMonitors(None, None, MONITORENUMPROC(_enum_cb), 0)

print(f"\nWin32 monitors ({len(monitors)}):")
for i, m in enumerate(monitors):
    mon = m["monitor"]
    mw = mon[2] - mon[0]
    mh = mon[3] - mon[1]
    print(f"  [{i}] {m['device']}  {'PRIMARY' if m['primary'] else '       '}")
    print(f"      rect: {mon}  ({mw}x{mh})")
    print(f"      work: {m['work']}")
    print(f"      DPI: {m['dpi']}  scale: {m['scale']:.2f}x")

pygame.quit()
