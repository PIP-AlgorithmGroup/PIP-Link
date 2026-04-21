"""Bisect: add stop_text_input + custom fonts + GL texture to find crash trigger"""
import pygame
from pygame.locals import *
from pygame._sdl2.video import Window
from OpenGL.GL import *
import imgui
from imgui.integrations.pygame import PygameRenderer
from pynput import keyboard
import numpy as np
import time
import sys

def on_press(key):
    pass
def on_release(key):
    pass

pygame.init()
pygame.display.set_mode((1280, 720), DOUBLEBUF | OPENGL)

glClearColor(0.0, 0.0, 0.0, 1.0)
glEnable(GL_TEXTURE_2D)
glMatrixMode(GL_PROJECTION)
glOrtho(-1, 1, -1, 1, -1, 1)
glMatrixMode(GL_MODELVIEW)

imgui.create_context()
io = imgui.get_io()
io.display_size = (1280, 720)

# Load custom fonts (same as app)
try:
    io.fonts.add_font_from_file_ttf("assets/fonts/NotoSansSC-Bold.ttf", 22)
    io.fonts.add_font_from_file_ttf("assets/fonts/NotoSansSC-Regular.ttf", 16)
    io.fonts.add_font_from_file_ttf("assets/fonts/JetBrainsMono-Regular.ttf", 18)
    print("Fonts loaded", flush=True)
except Exception as e:
    print(f"Font load failed: {e}", flush=True)

renderer = PygameRenderer()

# GL texture (like VideoRenderer)
tex_id = glGenTextures(1)
glBindTexture(GL_TEXTURE_2D, tex_id)
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR)
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR)

# stop_text_input like the app does
pygame.key.stop_text_input()

listener = keyboard.Listener(on_press=on_press, on_release=on_release)
listener.start()
print("All initialized", flush=True)

win = Window.from_display_module()
desk = pygame.display.get_desktop_sizes()
dw, dh = desk[0]

for cycle in range(5):
    # To borderless
    win.borderless = True
    win.size = (dw, dh)
    win.position = (0, 0)
    pygame.event.pump()
    pygame.event.clear(pygame.VIDEORESIZE)
    w, h = pygame.display.get_window_size()
    glViewport(0, 0, w, h)
    io.display_size = (w, h)

    for i in range(3):
        events = pygame.event.get()
        for e in events:
            if e.type != pygame.VIDEORESIZE:
                renderer.process_event(e)
        glClear(GL_COLOR_BUFFER_BIT)
        imgui.new_frame()
        imgui.text(f"Borderless cycle {cycle} frame {i}")
        imgui.render()
        renderer.render(imgui.get_draw_data())
        # Mimic app's text input toggle
        if io.want_text_input:
            pygame.key.start_text_input()
        else:
            pygame.key.stop_text_input()
        pygame.display.flip()
        print(f"  Cycle {cycle} borderless frame {i}: OK", flush=True)
        time.sleep(0.016)

    # Back to windowed
    win.borderless = False
    win.size = (1280, 720)
    pygame.event.pump()
    pygame.event.clear(pygame.VIDEORESIZE)
    w, h = pygame.display.get_window_size()
    glViewport(0, 0, w, h)
    io.display_size = (w, h)

    for i in range(3):
        events = pygame.event.get()
        for e in events:
            if e.type != pygame.VIDEORESIZE:
                renderer.process_event(e)
        glClear(GL_COLOR_BUFFER_BIT)
        imgui.new_frame()
        imgui.text(f"Windowed cycle {cycle} frame {i}")
        imgui.render()
        renderer.render(imgui.get_draw_data())
        if io.want_text_input:
            pygame.key.start_text_input()
        else:
            pygame.key.stop_text_input()
        pygame.display.flip()
        print(f"  Cycle {cycle} windowed frame {i}: OK", flush=True)
        time.sleep(0.016)

    print(f"Cycle {cycle} OK", flush=True)

listener.stop()
print("ALL PASSED", flush=True)
pygame.quit()
