"""Input mapping - keyboard to control commands"""

import logging
from network.keyboard_encoder import KeyboardEncoder


logger = logging.getLogger(__name__)


class InputMapper:
    """Input mapper - maps keyboard input to control commands"""

    def __init__(self):
        self.keyboard = KeyboardEncoder()
        self.keyboard.start()

    def get_control_command(self) -> dict:
        """Get current control command from keyboard state"""
        keyboard_state = self.keyboard.get_state()

        command = {
            "forward": 0.0,
            "turn": 0.0,
            "action": 0,
            "sprint": 0.0,
        }

        if len(keyboard_state) >= 10:
            # Assuming format: [w, a, s, d, space, shift, ...]
            w, a, s, d = keyboard_state[0], keyboard_state[1], keyboard_state[2], keyboard_state[3]

            # Forward/backward
            if w:
                command["forward"] = 1.0
            elif s:
                command["forward"] = -1.0

            # Turn left/right
            if a:
                command["turn"] = -1.0
            elif d:
                command["turn"] = 1.0

            # Action (space)
            if keyboard_state[4]:
                command["action"] = 1

            # Sprint (shift)
            if keyboard_state[5]:
                command["sprint"] = 1.0

        return command

    def stop(self):
        """Stop keyboard listener"""
        self.keyboard.stop()
