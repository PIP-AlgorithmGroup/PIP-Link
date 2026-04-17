"""Input handling"""

import pygame
from typing import Optional, Callable, Tuple


class InputHandler:
    """Input handler"""

    def __init__(self):
        self.keys_pressed = set()
        self.mouse_delta = (0, 0)
        self.mouse_buttons = (False, False, False)  # (left, middle, right)
        self.scroll_delta = 0
        self.mouse_locked = False
        self.on_toggle_menu: Optional[Callable] = None

    def handle_events(self, imgui_renderer=None) -> bool:
        """Handle events, return running status"""
        self.mouse_delta = (0, 0)
        self.scroll_delta = 0

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                return False

            # Forward ImGui events
            if imgui_renderer:
                imgui_renderer.process_event(event)

            if event.type == pygame.KEYDOWN:
                self.keys_pressed.add(event.key)
                # ESC toggles menu
                if event.key == pygame.K_ESCAPE:
                    if self.on_toggle_menu:
                        self.on_toggle_menu()

            elif event.type == pygame.KEYUP:
                self.keys_pressed.discard(event.key)

            elif event.type == pygame.MOUSEMOTION:
                self.mouse_delta = (event.rel[0], event.rel[1])

            elif event.type == pygame.MOUSEBUTTONDOWN:
                if event.button == 1:  # Left
                    self.mouse_buttons = (True, self.mouse_buttons[1], self.mouse_buttons[2])
                elif event.button == 2:  # Middle
                    self.mouse_buttons = (self.mouse_buttons[0], True, self.mouse_buttons[2])
                elif event.button == 3:  # Right
                    self.mouse_buttons = (self.mouse_buttons[0], self.mouse_buttons[1], True)
                elif event.button == 4:  # Scroll up
                    self.scroll_delta = 1
                elif event.button == 5:  # Scroll down
                    self.scroll_delta = -1

            elif event.type == pygame.MOUSEBUTTONUP:
                if event.button == 1:
                    self.mouse_buttons = (False, self.mouse_buttons[1], self.mouse_buttons[2])
                elif event.button == 2:
                    self.mouse_buttons = (self.mouse_buttons[0], False, self.mouse_buttons[2])
                elif event.button == 3:
                    self.mouse_buttons = (self.mouse_buttons[0], self.mouse_buttons[1], False)

        return True

    def get_mouse_delta(self) -> Tuple[int, int]:
        """Get mouse relative displacement this frame"""
        return self.mouse_delta

    def get_mouse_buttons(self) -> Tuple[bool, bool, bool]:
        """Get mouse button state (left, middle, right)"""
        return self.mouse_buttons

    def get_scroll(self) -> int:
        """Get scroll delta"""
        return self.scroll_delta

    def set_mouse_locked(self, locked: bool) -> None:
        """Control mouse lock (FPS mode)"""
        if locked != self.mouse_locked:
            pygame.event.set_grab(locked)
            pygame.mouse.set_visible(not locked)
            self.mouse_locked = locked

    def is_key_pressed(self, key: int) -> bool:
        """Check if key is pressed"""
        return key in self.keys_pressed
