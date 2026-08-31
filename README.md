# PIP-Link C++ 第一视角机器人控制客户端

这是 PIP-Link Windows 地面端的 C++20 重构工程。产品交互按第一视角机器人控制客户端设计，而不是传统仪表式地面站。旧 Python 地面端已从本分支移除，当前版本具备可运行的原生桌面前端与待接入的后端接口。

## 当前目标

- `pip_link_core`：与 UI 无关的核心静态库。
- `pip_link_desktop`：SDL3、Dear ImGui 和 Direct3D 11 桌面层。
- `pip_link_ground_station`：Windows 地面端程序入口。
- `pip_link_core_smoke_test`：不依赖外部测试框架的 CTest 冒烟测试。
- UTF-8 中文界面、Windows 中文字体回退、高 DPI 缩放和帧率无关动画。
- 第一视角图传占满整个工作区；左下角键鼠输入、右下角 FPS/码率/RTT/丢包率和底部 READY 仅作为 HUD 覆盖层。
- `Esc` 打开整页设置；连接、图传、控制、录制、诊断、日志和界面 7 个分类具备完整前端交互。
- 图传远端参数使用 120ms 防抖，松开滑块时立即提交；显示模式提供 30 秒确认和自动回滚。
- 打开设置、打开控制台、窗口失焦或断开连接时会强制退出 READY，并停止转发控制输入。
- `GroundStationBackend` 暴露图传纹理、输入、设备、网络、视频、控制、录制和日志接口；当前 Stub 中以 `TODO` 标记真实后端接入点。

SDL3 与 Dear ImGui 由 CMake `FetchContent` 下载并固定版本，首次配置需要能够访问 GitHub。Direct3D 11 使用 Windows SDK/MinGW 自带的系统库。后续将在此基础上接入 FFmpeg、网络协议和业务模块。

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
