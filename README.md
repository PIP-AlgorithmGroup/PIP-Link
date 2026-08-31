# PIP-Link C++ Ground Station

这是 PIP-Link Windows 地面端的 C++20 重构工程。旧 Python 地面端已从本分支移除，当前阶段提供无第三方依赖的基础框架，用于验证 CLion、CMake 和编译器配置。

## 当前目标

- `pip_link_core`：与 UI 无关的核心静态库。
- `pip_link_ground_station`：地面端程序入口。
- `pip_link_core_smoke_test`：不依赖外部测试框架的 CTest 冒烟测试。
- UTF-8、C++20、严格编译警告和 Windows Unicode 编译定义。

后续将在此基础上按阶段引入 SDL3、Dear ImGui、Direct3D 11、FFmpeg、网络协议和业务模块。

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
8. 启用 `debug` CMake Preset；它使用 CLion 自带的 Ninja，构建目录为 `out/build/debug`。
9. 等待右下角 CMake Reload 完成。
10. 顶部运行目标选择 `pip_link_ground_station`，点击锤子图标编译，点击绿色三角运行。

运行测试时，打开 CLion 的 CTest/Test Results 工具窗口，运行 `pip_link_core_smoke_test`；也可以右键 `tests/core_smoke_test.cpp` 后选择运行对应测试。

## 目录结构

```text
PIP-Link/
├── CMakeLists.txt
├── CMakePresets.json
└── ground_station/
    ├── include/pip_link/
    │   ├── app/
    │   └── core/
    ├── src/
    │   ├── app/
    │   └── core/
    └── tests/
```
