"""Input mapping - WASD to control commands"""

import pygame


class InputMapper:
    """Input mapper"""

    def __init__(self):
        pass

    def map_keyboard_to_command(self, keys_pressed: set) -> dict:
        """Map keyboard input to control commands"""
        command = {
            "forward": 0.0,
            "turn": 0.0,
            "action": 0,
            "sprint": 0.0,
        }

        # W/S forward/backward
        if ord('w') in keys_pressed:
            command["forward"] = 1.0
        elif ord('s') in keys_pressed:
            command["forward"] = -1.0

        # A/D left/right
        if ord('a') in keys_pressed:
            command["turn"] = -1.0
        elif ord('d') in keys_pressed:
            command["turn"] = 1.0

        # Space action
        if pygame.K_SPACE in keys_pressed:
            command["action"] = 1

        # Shift sprint
        if pygame.K_LSHIFT in keys_pressed or pygame.K_RSHIFT in keys_pressed:
            command["sprint"] = 1.0

        return command

    def map_mouse_to_command(self, dx: int, dy: int, buttons: tuple, sensitivity: float = 1.0) -> dict:
        """Map mouse input to control commands

        Args:
            dx: Mouse relative displacement X
            dy: Mouse relative displacement Y
            buttons: (left, middle, right) button state
            sensitivity: Sensitivity multiplier
        """
        command = {
            "pitch": float(dy) * sensitivity * 0.01,  # Convert to radian level
            "yaw": float(dx) * sensitivity * 0.01,
            "fire": 1 if buttons[0] else 0,  # Left button fire
            "aim": 1 if buttons[2] else 0,  # Right button aim
        }

        return command
