# PIP-Link C++ 第一视角机器人控制客户端

这是 PIP-Link Windows 地面端的 C++20 重构工程。产品交互按第一视角机器人控制客户端设计，而不是传统仪表式地面站。旧 Python 地面端已从本分支移除，当前版本具备可运行的原生桌面前端与原生网络、图传、录制后端。

## 当前实现

- `pip_link_core`：与 UI 无关的核心静态库。
- `pip_link_desktop`：SDL3、Dear ImGui 和 Direct3D 11 桌面层。
- `pip_link_ground_station`：Windows 地面端程序入口。
- CTest 覆盖核心逻辑、UDP 协议、连接重连、媒体管线、配置迁移、UI 输入和视觉契约。
- UTF-8 中文界面、Windows 中文字体回退、高 DPI 缩放和帧率无关动画。
- 第一视角图传占满整个工作区；左下角键鼠输入、右下角 FPS/码率/RTT/丢包率和底部 READY 仅作为 HUD 覆盖层。
- 无视频帧时使用明亮连接引导页，不以黑屏伪装视频区域；收到画面后自动切换为完整图传。
- `Esc` 打开整页设置；连接、图传、控制、录制、诊断、日志和界面 7 个分类具备完整前端交互。
- 设置页使用响应式卡片布局、圆角动画开关和帧率无关的平滑滚动，并根据可用桌面尺寸选择初始窗口大小。
- 图传远端参数使用 120ms 防抖，松开滑块时立即提交；显示模式提供 30 秒确认和自动回滚。
- 打开设置、打开控制台、窗口失焦或断开连接时会强制退出 READY，并停止转发控制输入。
- `GroundStationBackendRuntime` 实现 mDNS 发现、UDP 会话、心跳/控制协议、自动重连、视频分片与 FEC、Media Foundation/WIC 解码、D3D11 纹理、录像、截图、设置、诊断和审计日志；`GroundStationBackendStub` 仅供 UI 单元测试使用。
- `src/remote_link` 提供 ROS2 机载通信节点，消息包统一使用 `pip_msgs`，默认订阅 `/io/video_frame`，话题名和 UDP 端口均可通过 ROS2 参数配置。

SDL3 与 Dear ImGui 由 CMake `FetchContent` 下载并固定版本，首次配置需要能够访问 GitHub。Direct3D 11、Media Foundation 与 WIC 使用 Windows SDK/MinGW 自带的系统库。H.264 优先使用 Windows 解码器，不兼容时自动切换到 FFmpeg。录像会在 D3D11 最终合成后采集完整窗口（含设置页面、HUD、控制台和录制用光标标识），并由 FFmpeg 输出 H.264 MP4/MKV 或 FFV1 无损 MKV，因此请把 `ffmpeg.exe` 放在程序目录或加入 `PATH`；PNG 截图不依赖 FFmpeg。发行包自带 SIL OFL 1.1 授权的 Noto Sans CJK SC，不依赖用户电脑中的中文字体。

详细说明：

- [地面端使用指南](docs/USER_GUIDE.md)
- [开发、CLion 与 ROS2 集成说明](docs/DEVELOPMENT.md)
- [Windows 打包与发布](docs/RELEASING.md)

## 命令行构建

在本目录执行：

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

运行程序：

```powershell
.\out\build\debug\PIP-Link.exe
```

运行时快捷键：

| 默认按键 | 动作 |
|---|---|
| `Esc` | 在第一视角图传和整页设置之间切换 |
| `F6` | 切换 READY 安全状态 |
| `Tab` | 显示或隐藏输入 HUD |
| `` ` `` | 打开或关闭开发者控制台 |
| `F9` | 开始录制 |
| `F10` | 截屏 |
| 未绑定 | 暂停/继续录制 |
| 未绑定 | 结束录制 |

所有快捷键均可在“控制 → 键盘绑定”中修改或清空。开始录制可以与暂停/继续或结束共用按键，但暂停/继续与结束互斥；状态分发保证一次按键只执行一个录制动作。

## 运行时后端

- 手动连接时分别填写机载端地址、控制端口和视频端口，例如 `192.168.1.10:6000` 与 `5000`。
- mDNS 浏览 `_pip-link._udp.local`，并读取机载端公布的 `control_port` 与 `video_port`。
- 用户设置、审计日志和导出的诊断文件保存在程序所在目录；录像与截图写入界面中选择的目录。相对保存目录以 Windows“视频”目录为基准。
- 连接、录像和错误状态由真实后端状态机驱动。开发者控制台输入 `help` 可以查看当前支持的诊断命令。
- 录像支持暂停、继续和结束；暂停期间停止提交画面，继续后在同一文件中保持连续时间轴。

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

运行测试时，打开 CLion 的 CTest/Test Results 工具窗口运行全部测试；也可以在内置终端执行 `ctest --preset debug --output-on-failure`。

## 目录结构

```text
PIP-Link/
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── docs/
│   ├── USER_GUIDE.md
│   └── DEVELOPMENT.md
├── ground_station/
│   ├── include/pip_link/
│   │   ├── app/
│   │   ├── backend/
│   │   ├── core/
│   │   ├── platform/
│   │   └── ui/
│   ├── src/
│   │   ├── app/
│   │   ├── backend/
│   │   ├── core/
│   │   ├── platform/
│   │   └── ui/
│   └── tests/
└── src/
    ├── remote_link/
    └── test_frame_publisher/
```
