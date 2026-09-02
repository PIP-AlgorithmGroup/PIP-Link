# PIP-Link C++ 第一视角机器人控制客户端

这是 PIP-Link Windows 地面端的 C++20 重构工程。产品交互按第一视角机器人控制客户端设计，而不是传统仪表式地面站。旧 Python 地面端已从本分支移除，当前版本具备可运行的原生桌面前端与原生网络、图传、录制后端。

## 当前目标

- `pip_link_core`：与 UI 无关的核心静态库。
- `pip_link_desktop`：SDL3、Dear ImGui 和 Direct3D 11 桌面层。
- `pip_link_ground_station`：Windows 地面端程序入口。
- `pip_link_core_smoke_test`：不依赖外部测试框架的 CTest 冒烟测试。
- UTF-8 中文界面、Windows 中文字体回退、高 DPI 缩放和帧率无关动画。
- 第一视角图传占满整个工作区；左下角键鼠输入、右下角 FPS/码率/RTT/丢包率和底部 READY 仅作为 HUD 覆盖层。
- 无视频帧时使用明亮连接引导页，不以黑屏伪装视频区域；收到画面后自动切换为完整图传。
- `Esc` 打开整页设置；连接、图传、控制、录制、诊断、日志和界面 7 个分类具备完整前端交互。
- 设置页使用响应式卡片布局、圆角动画开关和帧率无关的平滑滚动，并根据可用桌面尺寸选择初始窗口大小。
- 图传远端参数使用 120ms 防抖，松开滑块时立即提交；显示模式提供 30 秒确认和自动回滚。
- 打开设置、打开控制台、窗口失焦或断开连接时会强制退出 READY，并停止转发控制输入。
- `GroundStationBackendRuntime` 实现 mDNS 发现、UDP 会话、心跳/控制协议、自动重连、视频分片与 FEC、Media Foundation/WIC 解码、D3D11 纹理、录像、截图、设置、诊断和审计日志；`GroundStationBackendStub` 仅供 UI 单元测试使用。

SDL3 与 Dear ImGui 由 CMake `FetchContent` 下载并固定版本，首次配置需要能够访问 GitHub。Direct3D 11、Media Foundation 与 WIC 使用 Windows SDK/MinGW 自带的系统库。H.264 优先使用 Windows 解码器，不兼容时自动切换到 FFmpeg。录像会在 D3D11 最终合成后采集完整窗口（含设置页面、HUD、控制台和软件光标），并由 FFmpeg 输出 H.264 MP4/MKV 或 FFV1 无损 MKV，因此请把 `ffmpeg.exe` 加入 `PATH`；PNG 截图不依赖 FFmpeg。

## 命令行构建

在本目录执行：

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

运行程序：

```powershell
.\out\build\debug\pip_link_ground_station.exe
```

运行时快捷键：

- `Esc`：在第一视角图传和整页设置之间切换；
- `F6`：切换 READY 安全状态；
- `Tab`：显示或隐藏输入 HUD。

## 运行时后端

- 手动连接地址填写机载端控制端口，例如 `192.168.1.10:6000`；未经过 mDNS 发现时，图传端口按“控制端口减 1000”推导，因此默认对应 `5000`。
- mDNS 浏览 `_pip-link._udp.local`，并读取机载端公布的 `control_port` 与 `video_port`。
- 用户设置、审计日志和导出的诊断文件保存在 `%LOCALAPPDATA%\PIP-Link`；录像与截图写入界面中选择的目录。
- 连接、录像和错误状态由真实后端状态机驱动。开发者控制台输入 `help` 可以查看当前支持的诊断命令。

## 在 CLion 中打开

1. 启动 CLion，选择 **Open**。
2. 选择 PIP-Link 仓库根目录。
3. CLion 检测到 `CMakeLists.txt` 后，选择 **Open as Project**。
4. 打开 **Settings | Build, Execution, Deployment | Toolchains**。
5. Windows 下可以选择以下任一工具链：
   - 推荐：Visual Studio 2022 的 MSVC；
   - 当前本机可用：MinGW，编译器目录为 `D:\mingw64\bin`。
6. 确认 CMake、C++ Compiler、Debugger 和 Make 均显示可用。
7. 打开 **Settings | Build, Execution, Deployment | CMake**。
8. 启用 `debug` CMake Preset；它使用 Ninja，构建目录为 `out/build/debug`。继续使用 CLion 默认生成的 `cmake-build-debug` 也可以。
9. 点击 **Reload CMake Project**，等待 SDL3 与 Dear ImGui 首次下载和配置完成。
10. 顶部运行目标选择 `pip_link_ground_station`，点击锤子图标编译，点击绿色三角运行。

如果 CMake 页面显示 **Not a plain CMake project**，通常是 CLion 打开了旧缓存。关闭项目，删除或排除 `.idea` 后重新从仓库根目录的 `CMakeLists.txt` 打开即可。

运行测试时，打开 CLion 的 CTest/Test Results 工具窗口，运行 `pip_link_core_smoke_test`；也可以右键 `tests/core_smoke_test.cpp` 后选择运行对应测试。

## 目录结构

```text
PIP-Link/
├── CMakeLists.txt
├── CMakePresets.json
└── ground_station/
    ├── include/pip_link/
    │   ├── app/
    │   ├── backend/
    │   ├── core/
    │   ├── platform/
    │   └── ui/
    ├── src/
    │   ├── app/
    │   ├── backend/
    │   ├── core/
    │   ├── platform/
    │   └── ui/
    └── tests/
```
