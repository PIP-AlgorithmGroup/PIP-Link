"""Test borderless switching with pynput active"""
import pygame
from pygame.locals import *
from pygame._sdl2.video import Window
from OpenGL.GL import *
from pynput import keyboard
import time
import sys

def on_press(key):
    pass

def on_release(key):
    pass

pygame.init()
pygame.display.set_mode((1280, 720), DOUBLEBUF | OPENGL)
win = Window.from_display_module()

listener = keyboard.Listener(on_press=on_press, on_release=on_release)
listener.start()
print("pynput listener started", flush=True)

desk = pygame.display.get_desktop_sizes()
dw, dh = desk[0]

for cycle in range(5):
    win.borderless = True
    win.size = (dw, dh)
    win.position = (0, 0)
    pygame.event.pump()
    pygame.event.clear(pygame.VIDEORESIZE)
    for i in range(3):
        events = pygame.event.get()
        print(f"  Cycle {cycle} borderless frame {i}: {len(events)} events", flush=True)
        time.sleep(0.016)

    win.borderless = False
    win.size = (1280, 720)
    pygame.event.pump()
    pygame.event.clear(pygame.VIDEORESIZE)
    for i in range(3):
        events = pygame.event.get()
        print(f"  Cycle {cycle} windowed frame {i}: {len(events)} events", flush=True)
        time.sleep(0.016)

    print(f"Cycle {cycle} OK", flush=True)

listener.stop()
print("ALL PASSED", flush=True)
pygame.quit()
