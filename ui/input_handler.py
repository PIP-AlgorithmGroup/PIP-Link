"""Input handling"""

import pygame
from typing import Optional, Callable, Tuple


class InputHandler:
    """Input handler"""

    def __init__(self):
        self.keys_pressed = set()
        self.mouse_delta = (0, 0)
        self.mouse_buttons = (False, False, False)  # (left, middle, right)
        self.mouse_side_buttons = (False, False)     # (mouse4, mouse5)
        self.scroll_delta = 0
        self.mouse_locked = False
        self.on_toggle_menu: Optional[Callable] = None
        self.on_toggle_console: Optional[Callable] = None
        self.on_toggle_hud: Optional[Callable] = None
        # Key capture callback for rebinding: called with (pygame_key, key_name)
        self.on_key_capture: Optional[Callable] = None
        self._capturing_keys: bool = False

    def handle_events(self, imgui_renderer=None) -> bool:
        """Handle events, return running status"""
        self.mouse_delta = (0, 0)
        self.scroll_delta = 0

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                return False

            # Forward ImGui events (skip VIDEORESIZE to avoid renderer crash)
            if imgui_renderer and event.type != pygame.VIDEORESIZE:
                try:
                    imgui_renderer.process_event(event)
                except Exception:
                    pass

            if event.type == pygame.KEYDOWN:
                # Key capture mode: intercept for rebinding
                if self._capturing_keys and self.on_key_capture:
                    key_name = pygame.key.name(event.key)
                    self.on_key_capture(event.key, key_name)
                    self._capturing_keys = False
                    continue

                self.keys_pressed.add(event.key)
                # ESC toggles menu
                if event.key == pygame.K_ESCAPE:
                    if self.on_toggle_menu:
                        self.on_toggle_menu()
                # TAB toggles HUD
                if event.key == pygame.K_TAB:
                    if self.on_toggle_hud:
                        self.on_toggle_hud()
                # ` (backquote/tilde) toggles console
                if event.key == pygame.K_BACKQUOTE:
                    if self.on_toggle_console:
                        self.on_toggle_console()

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
                elif event.button == 6:  # Mouse4 (side back)
                    self.mouse_side_buttons = (True, self.mouse_side_buttons[1])
                elif event.button == 7:  # Mouse5 (side forward)
                    self.mouse_side_buttons = (self.mouse_side_buttons[0], True)

            elif event.type == pygame.MOUSEBUTTONUP:
                if event.button == 1:
                    self.mouse_buttons = (False, self.mouse_buttons[1], self.mouse_buttons[2])
                elif event.button == 2:
                    self.mouse_buttons = (self.mouse_buttons[0], False, self.mouse_buttons[2])
                elif event.button == 3:
                    self.mouse_buttons = (self.mouse_buttons[0], self.mouse_buttons[1], False)
                elif event.button == 6:
                    self.mouse_side_buttons = (False, self.mouse_side_buttons[1])
                elif event.button == 7:
                    self.mouse_side_buttons = (self.mouse_side_buttons[0], False)

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

    def start_key_capture(self):
        """Enter key capture mode for rebinding"""
        self._capturing_keys = True

    def is_capturing(self) -> bool:
        """Check if currently in key capture mode"""
        return self._capturing_keys
