#include "pip_link/backend/ground_station_backend.hpp"

namespace pip_link::backend {

std::vector<DeviceInfo> GroundStationBackendStub::discovered_devices() const {
    // TODO: 返回 mDNS/手动发现得到的实时设备列表。
    return {};
}

TelemetrySnapshot GroundStationBackendStub::telemetry() const {
    // TODO: 返回会话、解码器和控制链路的实时统计。
    return {};
}

std::vector<AuditEntry> GroundStationBackendStub::audit_entries() const {
    // TODO: 从审计日志服务查询记录。
    return {};
}

VideoSurface GroundStationBackendStub::latest_video_surface() const {
    // TODO: 返回可供 Dear ImGui D3D11 后端绘制的 ID3D11ShaderResourceView。
    return {};
}

void GroundStationBackendStub::scan_devices(const std::string&) {
    // TODO: 启动服务发现。
}

void GroundStationBackendStub::connect_device(const DeviceInfo&) {
    // TODO: 建立地面端到机载端的会话。
}

void GroundStationBackendStub::disconnect_device() {
    // TODO: 安全断开当前会话。
}

void GroundStationBackendStub::apply_connection_settings(int, int, int, bool) {
    // TODO: 应用心跳、重连和传输层参数。
}

void GroundStationBackendStub::apply_input_settings(float, float, bool) {
    // TODO: 应用鼠标灵敏度、视野和俯仰方向。
}

void GroundStationBackendStub::apply_video_settings(int, int, int, int, int, int, int,
                                                    bool, float, int, int, int, int,
                                                    bool, bool) {
    // TODO: 更新视频接收与解码配置。
}

void GroundStationBackendStub::preview_display_settings(int, int, int) {
    // TODO: 预览分辨率、窗口模式和目标显示器，并保存可回滚的旧窗口状态。
}

void GroundStationBackendStub::confirm_display_settings() {
    // TODO: 确认当前窗口预览并持久化显示设置。
}

void GroundStationBackendStub::revert_display_settings() {
    // TODO: 回滚到预览前的窗口和显示器设置。
}

void GroundStationBackendStub::apply_control_settings(float, bool, bool, bool) {
    // TODO: 更新输入采集与控制发送配置。
}

void GroundStationBackendStub::apply_interface_settings(float, float, bool, bool, bool, int) {
    // TODO: 持久化 HUD、透明度、缩放和语言设置。
}

void GroundStationBackendStub::apply_diagnostics_settings(bool, bool, bool, bool) {
    // TODO: 持久化性能曲线、原始调试信息、日志详细度和模拟诊断开关。
}

void GroundStationBackendStub::set_ready(bool) {
    // TODO: 切换安全控制状态并通知机载端。
}

void GroundStationBackendStub::start_recording(const std::string&, int, int, int) {
    // TODO: 启动录像管线。
}

void GroundStationBackendStub::stop_recording() {
    // TODO: 停止录像并安全封装文件。
}

void GroundStationBackendStub::take_screenshot(const std::string&) {
    // TODO: 保存当前解码帧。
}

void GroundStationBackendStub::open_recordings_folder(const std::string&) {
    // TODO: 使用系统文件管理器打开录像目录。
}

void GroundStationBackendStub::save_key_bindings(const std::vector<int>&) {
    // TODO: 持久化快捷键配置。
}

void GroundStationBackendStub::apply_gamepad_settings(float, bool) {
    // TODO: 应用手柄死区和振动反馈参数。
}

void GroundStationBackendStub::export_diagnostics() {
    // TODO: 导出链路诊断报告。
}

void GroundStationBackendStub::export_audit_log() {
    // TODO: 导出审计日志。
}

void GroundStationBackendStub::clear_audit_log() {
    // TODO: 清空审计日志。
}

void GroundStationBackendStub::submit_control_input(const ControlInput&) {
    // TODO: 将键鼠输入编码后发送到当前机载会话。
}

std::string GroundStationBackendStub::execute_console_command(const std::string&) {
    // TODO: 执行后端诊断命令并返回输出。
    return {};
}

}  // namespace pip_link::backend
