"""Configuration persistence manager"""

import json
import os
from pathlib import Path
from typing import Dict, Any, Optional


class ConfigManager:
    """Manage application configuration persistence"""

    def __init__(self, config_path: str = "config.json"):
        self.config_path = Path(config_path)
        self.config: Dict[str, Any] = {
            # Input bindings
            "key_bindings": {
                "forward": "W",
                "backward": "S",
                "left": "A",
                "right": "D",
                "sprint": "Shift",
                "special_1": "E",
                "special_2": "F",
            },
            # Connection
            "last_service_name": "_pip_link._udp",
            "last_server_ip": "",
            "last_server_port": 0,
            # UI
            "window_width": 1600,
            "window_height": 900,
            "fullscreen": False,
        }
        self.load()

    def load(self) -> bool:
        """Load config from file. Return True if loaded, False if using defaults."""
        if not self.config_path.exists():
            return False

        try:
            with open(self.config_path, "r", encoding="utf-8") as f:
                loaded = json.load(f)
                # Merge with defaults (preserve new keys if config is outdated)
                self.config.update(loaded)
            print(f"[ConfigManager] Loaded config from {self.config_path}")
            return True
        except Exception as e:
            print(f"[ConfigManager] Failed to load config: {e}")
            return False

    def save(self) -> bool:
        """Save config to file. Return True if successful."""
        try:
            self.config_path.parent.mkdir(parents=True, exist_ok=True)
            with open(self.config_path, "w", encoding="utf-8") as f:
                json.dump(self.config, f, indent=2, ensure_ascii=False)
            print(f"[ConfigManager] Saved config to {self.config_path}")
            return True
        except Exception as e:
            print(f"[ConfigManager] Failed to save config: {e}")
            return False

    def get(self, key: str, default: Any = None) -> Any:
        """Get config value"""
        return self.config.get(key, default)

    def set(self, key: str, value: Any) -> None:
        """Set config value"""
        self.config[key] = value

    def get_key_bindings(self) -> Dict[str, str]:
        """Get all key bindings"""
        return self.config.get("key_bindings", {}).copy()

    def set_key_binding(self, action: str, key: str) -> None:
        """Set a key binding"""
        if "key_bindings" not in self.config:
            self.config["key_bindings"] = {}
        self.config["key_bindings"][action] = key

    def get_last_connection(self) -> tuple:
        """Get last connection info (service_name, ip, port)"""
        return (
            self.config.get("last_service_name", "_pip_link._udp"),
            self.config.get("last_server_ip", ""),
            self.config.get("last_server_port", 0),
        )

    def set_last_connection(self, service_name: str, ip: str, port: int) -> None:
        """Save last connection info"""
        self.config["last_service_name"] = service_name
        self.config["last_server_ip"] = ip
        self.config["last_server_port"] = port
