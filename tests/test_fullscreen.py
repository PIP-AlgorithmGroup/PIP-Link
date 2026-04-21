"""Debug: simulate exact app flow with timing"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import faulthandler
faulthandler.enable()

import threading
import time
import pygame
from pygame.locals import *
from OpenGL.GL import *
import imgui
from imgui.integrations.pygame import PygameRenderer
from logic.input_mapper import InputMapper

pygame.init()
pygame.display.set_mode((1280, 720), DOUBLEBUF | OPENGL)
glClearColor(0, 0, 0, 1)
glEnable(GL_TEXTURE_2D)
glMatrixMode(GL_PROJECTION)
glOrtho(-1, 1, -1, 1, -1, 1)
glMatrixMode(GL_MODELVIEW)

imgui.create_context()
io = imgui.get_io()
io.display_size = (1280, 720)

# Load fonts like real app
try:
    font_title = io.fonts.add_font_from_file_ttf("C:\\Windows\\Fonts\\segoeuib.ttf", 22)
    font_body = io.fonts.add_font_from_file_ttf("C:\\Windows\\Fonts\\segoeui.ttf", 16)
    font_mono = io.fonts.add_font_from_file_ttf("C:\\Windows\\Fonts\\consola.ttf", 18)
except:
    pass

renderer = PygameRenderer()
mapper = InputMapper()

# Disable IME like real app
pygame.key.stop_text_input()

print("Init OK, running frames...", flush=True)

clock = pygame.time.Clock()
for i in range(60):
    for event in pygame.event.get():
        renderer.process_event(event)
    glClear(GL_COLOR_BUFFER_BIT)
    imgui.new_frame()
    imgui.text(f"Frame {i}")
    imgui.render()
    renderer.render(imgui.get_draw_data())
    pygame.display.flip()
    clock.tick(60)

# Now switch to borderless - exactly like _apply_window_mode
print(f"Threads before stop: {threading.active_count()}", flush=True)
for t in threading.enumerate():
    print(f"  {t.name} daemon={t.daemon} alive={t.is_alive()}", flush=True)

print("Stopping mapper.keyboard...", flush=True)
mapper.keyboard.stop()

print(f"Threads after stop: {threading.active_count()}", flush=True)
for t in threading.enumerate():
    print(f"  {t.name} daemon={t.daemon} alive={t.is_alive()}", flush=True)

from pygame._sdl2.video import Window
win = Window.from_display_module()
desk = pygame.display.get_desktop_sizes()
dw, dh = desk[0]

print("Switching to borderless...", flush=True)
win.borderless = True
win.size = (dw, dh)
win.position = (0, 0)
pygame.event.pump()

w, h = pygame.display.get_window_size()
glViewport(0, 0, w, h)
io.display_size = (w, h)
print(f"Borderless: {w}x{h}", flush=True)

mapper.keyboard.start()
print("KB restarted", flush=True)

for i in range(120):
    for event in pygame.event.get():
        renderer.process_event(event)
    glClear(GL_COLOR_BUFFER_BIT)
    imgui.new_frame()
    imgui.text(f"Borderless {i}")
    imgui.render()
    renderer.render(imgui.get_draw_data())
    pygame.display.flip()
    clock.tick(60)

print("Done!", flush=True)
mapper.keyboard.stop()
pygame.quit()
