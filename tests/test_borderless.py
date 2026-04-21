"""Test borderless window switching in isolation"""
import pygame
from pygame.locals import *
from pygame._sdl2.video import Window
from OpenGL.GL import *
import time
import sys

pygame.init()
pygame.display.set_mode((1280, 720), DOUBLEBUF | OPENGL)
win = Window.from_display_module()
print("Initial OK", flush=True)

desk = pygame.display.get_desktop_sizes()
dw, dh = desk[0]
print(f"Desktop: {dw}x{dh}", flush=True)

for cycle in range(5):
    # To borderless fullscreen
    win.borderless = True
    win.size = (dw, dh)
    win.position = (0, 0)
    pygame.event.pump()
    pygame.event.clear(pygame.VIDEORESIZE)
    for i in range(3):
        events = pygame.event.get()
        print(f"  Cycle {cycle} borderless frame {i}: {len(events)} events", flush=True)
        time.sleep(0.016)

    # Back to windowed
    win.borderless = False
    win.size = (1280, 720)
    pygame.event.pump()
    pygame.event.clear(pygame.VIDEORESIZE)
    for i in range(3):
        events = pygame.event.get()
        print(f"  Cycle {cycle} windowed frame {i}: {len(events)} events", flush=True)
        time.sleep(0.016)

    print(f"Cycle {cycle} OK", flush=True)

print("ALL PASSED", flush=True)
pygame.quit()
