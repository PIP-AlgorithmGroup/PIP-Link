# PIP-Link 开发与集成说明

## Windows 地面端

地面端使用 C++20、SDL3、Dear ImGui 和 Direct3D 11。网络、视频重组、FEC、解码、D3D11 纹理、窗口合成录制、设置持久化和诊断均由原生 C++ 实现。

### 环境要求

- Windows 10/11；
- CMake 3.24 或更高版本；
- Ninja；
- MSVC 2022 或 MinGW-w64；
- 可访问 GitHub，用于首次下载固定版本的 SDL3 `3.2.16` 与 Dear ImGui `1.91.9b`；
- 需要录像时，将 `ffmpeg.exe` 加入 `PATH`。

### 命令行构建

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Release 构建：

```powershell
cmake --preset release
cmake --build --preset release
```

Debug 可执行文件位于 `out/build/debug/pip_link_ground_station.exe`。

### CLion

1. 使用 **Open** 打开仓库根目录，不要只打开 `ground_station` 子目录。
2. 在 **Settings | Build, Execution, Deployment | Toolchains** 中选择 MSVC 2022 或 MinGW-w64。
3. 在 **Settings | Build, Execution, Deployment | CMake** 中启用 `debug` Preset。
4. 执行 **Reload CMake Project**。
5. 选择运行目标 `pip_link_ground_station` 后构建或调试。

如果 CMake 页面显示 **Not a plain CMake project**，关闭项目并从根目录的 `CMakeLists.txt` 重新打开。构建目录由 Preset 固定为 `out/build/debug`，不需要把旧的 `cmake-build-*` 目录作为项目打开。

### 主要目标

| 目标 | 用途 |
|---|---|
| `pip_link_core` | 与图形界面无关的核心静态库 |
| `pip_link_imgui` | SDL3、Dear ImGui 与 D3D11 适配层 |
| `pip_link_desktop` | 地面端 UI、平台与后端实现 |
| `pip_link_ground_station` | Windows GUI 程序 |

所有修改至少应通过：

```powershell
cmake --build out/build/debug -j 26
ctest --preset debug --output-on-failure
```

## ROS2 机载端 `remote_link`

`src/remote_link` 是独立的 ROS2 ament_cmake 包，不参与 Windows 地面端 CMake 构建。它依赖 `pip_msgs`，不再使用 `pip_vision_interfaces`。

主要 ROS2 接口：

| 参数 | 默认值 | 方向/用途 |
|---|---|---|
| `frame_topic` | `/io/video_frame` | 订阅 `sensor_msgs/msg/Image` |
| `command_topic` | `/io/remote_command` | 发布 `pip_msgs/msg/RemoteCommand` |
| `stats_topic` | `/remote_link/stats` | 发布 JSON 格式 `std_msgs/msg/String` |
| `control_port` | `6000` | UDP 控制与参数通道 |
| `video_port` | `5000` | UDP 视频通道 |
| `air_unit_name` | `air_unit_01` | mDNS 显示名称 |
| `mdns.interface_name` | `wlP1p1s0` | Avahi 注册使用的网卡 |

`frame_topic`、`command_topic` 和 `stats_topic` 为只读启动参数，需要在启动节点时传入，运行中不能改名。视频订阅使用 depth 1、best-effort、volatile QoS，支持 `bgr8`、`rgb8`、`bgra8`、`rgba8` 和 `mono8`。

图传参数 `target_fps`、`target_bitrate_kbps`、`jpeg_quality`、`encoder`、FEC、MTU 和图像增强项支持由地面端通过 UDP 更新。地面端建立连接时会查询当前值，远端收到更新后通过 ROS2 参数系统原子应用。

将包复制到机载工作区后，可按实际工作区结构构建：

```bash
sudo apt install libavahi-client-dev libopencv-dev zlib1g-dev \
  nlohmann-json3-dev libavcodec-dev libavutil-dev libswscale-dev

colcon build --packages-select pip_msgs remote_link
source install/setup.bash
ros2 run remote_link remote_link_node --ros-args \
  -p frame_topic:=/io/video_frame \
  -p command_topic:=/io/remote_command \
  -p stats_topic:=/remote_link/stats \
  -p control_port:=6000 \
  -p video_port:=5000
```

如果由 `pip_vision_startup` 的 launch 文件启动，应在 launch 参数中传入相同参数。启动后可先检查：

```bash
ros2 topic info /io/video_frame -v
ros2 topic hz /io/video_frame
ros2 param get /remote_link frame_topic
ros2 param get /remote_link command_topic
ros2 param get /remote_link stats_topic
```

仓库中的 `src/test_frame_publisher` 仅用于本地集成测试，默认向 `/io/video_frame` 发布测试图像，不应作为正式运行时视频源。

