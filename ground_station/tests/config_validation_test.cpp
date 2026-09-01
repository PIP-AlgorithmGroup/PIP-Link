#include "pip_link/backend/ground_station_backend.hpp"

#include <windows.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    const std::filesystem::path root = PIP_LINK_CONFIG_TEST_OUTPUT;
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "PIP-Link", error);
    SetEnvironmentVariableW(L"LOCALAPPDATA", root.wstring().c_str());
    std::ofstream settings(root / "PIP-Link" / "settings.ini", std::ios::trunc);
    settings << "heartbeat_ms=2147483647\n"
             << "reconnect_seconds=-9\n"
             << "mtu=9999\n"
             << "frame_rate=-1\n"
             << "bitrate_kbps=999999\n"
             << "fec_redundancy=nan\n"
             << "hud_scale=inf\n"
             << "gamepad_deadzone=-2\n"
             << "recording_format=99\n"
             << "last_video_port=8888\n"
             << "key_0=-1\n";
    settings.close();

    {
        pip_link::backend::GroundStationBackendRuntime backend(nullptr, nullptr, nullptr);
        const auto config = backend.preferences();
        if (config.heartbeat_ms != 5000 || config.reconnect_seconds != 1 ||
            config.mtu != 1500 || config.frame_rate != 24 ||
            config.bitrate_kbps != 80000 || !std::isfinite(config.fec_redundancy) ||
            !std::isfinite(config.hud_scale) || config.gamepad_deadzone != 0.0F ||
            config.recording_format != 2 || config.last_video_port != 8888 ||
            !config.key_bindings.empty()) {
            std::cerr << "persisted settings were not sanitized\n";
            return 1;
        }
    }
    std::filesystem::remove_all(root, error);
    return 0;
}
