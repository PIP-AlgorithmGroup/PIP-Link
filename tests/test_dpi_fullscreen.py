"""Test fullscreen on secondary display with DPI awareness"""
import ctypes
import ctypes.wintypes

try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
    print("DPI: per-monitor v1")
except Exception:
    pass

import pygame
from pygame.locals import *
from OpenGL.GL import *
import imgui
from imgui.integrations.pygame import PygameRenderer
import time
import sys

MONITORINFOF_PRIMARY = 0x00000001

class MONITORINFOEX(ctypes.Structure):
    _fields_ = [
        ("cbSize", ctypes.wintypes.DWORD),
        ("rcMonitor", ctypes.wintypes.RECT),
        ("rcWork", ctypes.wintypes.RECT),
        ("dwFlags", ctypes.wintypes.DWORD),
        ("szDevice", ctypes.c_wchar * 32),
    ]

def enum_monitors():
    results = []
    def _cb(hMon, _hdc, _lprc, _data):
        info = MONITORINFOEX()
        info.cbSize = ctypes.sizeof(MONITORINFOEX)
        ctypes.windll.user32.GetMonitorInfoW(hMon, ctypes.byref(info))
        m = info.rcMonitor
        results.append({
            "x": m.left, "y": m.top,
            "w": m.right - m.left, "h": m.bottom - m.top,
            "primary": bool(info.dwFlags & MONITORINFOF_PRIMARY),
        })
        return True
    ENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p,
                                   ctypes.POINTER(ctypes.wintypes.RECT), ctypes.c_long)
    ctypes.windll.user32.EnumDisplayMonitors(None, None, ENUMPROC(_cb), 0)
    results.sort(key=lambda m: (not m["primary"], m["x"], m["y"]))
    return results

pygame.init()
pygame.display.set_mode((1280, 720), DOUBLEBUF | OPENGL)
pygame.display.set_caption("DPI Test")

glClearColor(0.1, 0.1, 0.1, 1.0)
glEnable(GL_TEXTURE_2D)

imgui.create_context()
io = imgui.get_io()
io.display_size = (1280, 720)
renderer = PygameRenderer()

monitors = enum_monitors()
print(f"\nMonitors: {len(monitors)}")
for i, m in enumerate(monitors):
    print(f"  [{i}] {m['w']}x{m['h']} at ({m['x']},{m['y']}) {'PRIMARY' if m['primary'] else ''}")

print(f"\npygame desktop sizes: {pygame.display.get_desktop_sizes()}")

if len(monitors) < 2:
    print("Need 2 monitors for this test")
    pygame.quit()
    sys.exit(0)

target = 1  # secondary display
mon = monitors[target]
print(f"\nFullscreen on monitor {target}: {mon['w']}x{mon['h']} at ({mon['x']},{mon['y']})")

# Fullscreen: set_mode + SetWindowPos
flags = DOUBLEBUF | OPENGL | NOFRAME
pygame.display.set_mode((mon["w"], mon["h"]), flags)
pygame.event.pump()

hwnd = pygame.display.get_wm_info()["window"]
SWP_NOZORDER = 0x0004
SWP_FRAMECHANGED = 0x0020
ctypes.windll.user32.SetWindowPos(hwnd, 0, mon["x"], mon["y"], mon["w"], mon["h"],
                                   SWP_NOZORDER | SWP_FRAMECHANGED)
pygame.event.pump()
pygame.event.get()

# Reinit GL
glClearColor(0.0, 0.2, 0.0, 1.0)
w, h = pygame.display.get_window_size()
glViewport(0, 0, w, h)
glMatrixMode(GL_PROJECTION)
glLoadIdentity()
glOrtho(-1, 1, -1, 1, -1, 1)
glMatrixMode(GL_MODELVIEW)
glLoadIdentity()
io.display_size = (w, h)

try:
    if hasattr(renderer, 'shutdown'):
        renderer.shutdown()
except Exception:
    pass
renderer = PygameRenderer()

print(f"Window size after fullscreen: {w}x{h}")

# Verify with Win32
rect = ctypes.wintypes.RECT()
ctypes.windll.user32.GetWindowRect(hwnd, ctypes.byref(rect))
print(f"Win32 rect: ({rect.left},{rect.top}) -> ({rect.right},{rect.bottom})")
print(f"Win32 size: {rect.right-rect.left}x{rect.bottom-rect.top}")

# Render a few frames
for i in range(60):
    for e in pygame.event.get():
        if e.type == pygame.QUIT:
            break
        if e.type != pygame.VIDEORESIZE:
            renderer.process_event(e)
    glClear(GL_COLOR_BUFFER_BIT)
    imgui.new_frame()
    imgui.set_next_window_position(50, 50)
    imgui.begin("Test", flags=imgui.WINDOW_ALWAYS_AUTO_RESIZE)
    imgui.text(f"Monitor {target}: {mon['w']}x{mon['h']}")
    imgui.text(f"Window: {w}x{h}")
    imgui.text(f"Frame {i}/60")
    imgui.end()
    imgui.render()
    renderer.render(imgui.get_draw_data())
    pygame.display.flip()
    time.sleep(0.016)

# Back to windowed
pygame.display.set_mode((1280, 720), DOUBLEBUF | OPENGL)
pygame.display.set_caption("DPI Test - Windowed")
pygame.event.pump()
pygame.event.get()
w, h = pygame.display.get_window_size()
print(f"\nBack to windowed: {w}x{h}")

pygame.quit()
print("DONE")
