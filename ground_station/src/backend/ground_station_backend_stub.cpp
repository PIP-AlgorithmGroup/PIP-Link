#include "pip_link/backend/ground_station_backend.hpp"

namespace pip_link::backend {

std::vector<DeviceInfo> GroundStationBackendStub::discovered_devices() const {
    return {};
}

TelemetrySnapshot GroundStationBackendStub::telemetry() const {
    return {};
}

std::vector<AuditEntry> GroundStationBackendStub::audit_entries() const {
    return {};
}

VideoSurface GroundStationBackendStub::latest_video_surface() const {
    return {};
}

RuntimeState GroundStationBackendStub::runtime_state() const {
    return {};
}

BackendPreferences GroundStationBackendStub::preferences() const { return {}; }

void GroundStationBackendStub::scan_devices(const std::string&) {}

void GroundStationBackendStub::connect_device(const DeviceInfo&) {}

void GroundStationBackendStub::disconnect_device() {}

void GroundStationBackendStub::apply_connection_settings(int, int, int, bool) {}

void GroundStationBackendStub::apply_input_settings(float, float, bool) {}

void GroundStationBackendStub::apply_video_settings(int, int, int, int, int, int, int,
                                                    bool, float, int, int, int, int,
                                                    bool, bool) {}

void GroundStationBackendStub::preview_display_settings(int, int, int) {}

void GroundStationBackendStub::confirm_display_settings() {}

void GroundStationBackendStub::revert_display_settings() {}

void GroundStationBackendStub::apply_control_settings(float, bool, bool, bool) {}

void GroundStationBackendStub::apply_interface_settings(float, float, bool, bool, bool, int) {}

void GroundStationBackendStub::apply_diagnostics_settings(bool, bool, bool) {}

void GroundStationBackendStub::set_ready(bool) {}

MediaActionResult GroundStationBackendStub::start_recording(
    const std::string&, int, int, int) {
    return {true, "录像已开始"};
}

MediaActionResult GroundStationBackendStub::set_recording_paused(bool paused) {
    return {true, paused ? "录像已暂停" : "录像已继续"};
}

void GroundStationBackendStub::stop_recording() {}

MediaActionResult GroundStationBackendStub::take_screenshot(const std::string&) {
    return {true, "截图已保存"};
}

bool GroundStationBackendStub::needs_composited_frame() const { return false; }

void GroundStationBackendStub::submit_composited_frame(CompositedFrame) {}

MediaActionResult GroundStationBackendStub::open_recordings_folder(const std::string&) {
    return {true, "保存目录已打开"};
}

DirectorySelectionResult GroundStationBackendStub::choose_recording_directory(
    const std::string&) {
    return {};
}

void GroundStationBackendStub::save_key_bindings(const std::vector<int>&) {}

void GroundStationBackendStub::apply_gamepad_settings(float, bool) {}

void GroundStationBackendStub::export_diagnostics() {}

void GroundStationBackendStub::export_audit_log() {}

void GroundStationBackendStub::clear_audit_log() {}

void GroundStationBackendStub::submit_control_input(const ControlInput&) {}

std::string GroundStationBackendStub::execute_console_command(const std::string&) {
    return {};
}

}  // namespace pip_link::backend
