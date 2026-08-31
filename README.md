# PIP-Link C++ Ground Station

这是 PIP-Link Windows 地面端的 C++20 重构工程。旧 Python 地面端已从本分支移除，当前版本已经具备可运行的原生桌面界面框架。

## 当前目标

- `pip_link_core`：与 UI 无关的核心静态库。
- `pip_link_desktop`：SDL3、Dear ImGui 和 Direct3D 11 桌面层。
- `pip_link_ground_station`：Windows 地面端程序入口。
- `pip_link_core_smoke_test`：不依赖外部测试框架的 CTest 冒烟测试。
- UTF-8 中文界面、Windows 中文字体回退、高 DPI 缩放和帧率无关动画。
- 深色 cyan 设计系统、侧边导航、状态卡和各核心模块的页面入口。

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
    │   ├── core/
    │   ├── platform/
    │   └── ui/
    ├── src/
    │   ├── app/
    │   ├── core/
    │   ├── platform/
    │   └── ui/
    └── tests/
```
