"""指令系统基类与注册表"""

from abc import ABC, abstractmethod
from dataclasses import dataclass, field


@dataclass
class CommandResult:
    success: bool
    message: str = ""


class Command(ABC):
    @property
    @abstractmethod
    def name(self) -> str: ...

    @property
    def aliases(self) -> list[str]:
        return []

    @property
    def description(self) -> str:
        return ""

    @abstractmethod
    def execute(self, args: list[str]) -> CommandResult: ...


class CommandRegistry:
    def __init__(self):
        self._commands: dict[str, Command] = {}

    def register(self, cmd: Command) -> None:
        self._commands[cmd.name] = cmd
        for alias in cmd.aliases:
            self._commands[alias] = cmd

    def dispatch(self, input_str: str) -> CommandResult:
        parts = input_str.strip().split()
        if not parts:
            return CommandResult(True)
        name, args = parts[0].lower(), parts[1:]
        cmd = self._commands.get(name)
        if cmd is None:
            return CommandResult(False, f"Unknown command: {name}")
        return cmd.execute(args)

    @property
    def names(self) -> list[str]:
        return sorted(self._commands.keys())
