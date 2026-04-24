<div align="center">

# PIP-Link

**高性能低延迟远程桌面控制系统**

[![Version](https://img.shields.io/badge/version-v2.0.0-blue.svg)](https://github.com/P1ne4pp1e/PIP-Link/releases)
[![License](https://img.shields.io/badge/license-Apache%202.0-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows-lightgrey.svg)]()
[![Python](https://img.shields.io/badge/python-3.10%2B-yellow.svg)]()

*基于 UDP + H.264/JPEG 编码的实时视频流传输与键鼠控制系统*

</div>

---

![001](.\assets\imgs\main_001.png)

---

## 目录

- [项目简介](#项目简介)
- [功能特性](#功能特性)
- [系统架构](#系统架构)
- [快速开始](#快速开始)
- [连接教程](#连接教程)
- [使用教程](#使用教程)
- [菜单各标签页说明](#菜单各标签页说明)
- [配置参数](#配置参数)
- [开发者接口](#开发者接口)
- [打包发布](#打包发布)
- [许可证](#许可证)
- [联系方式](#联系方式)

---

## 项目简介

PIP-Link 是一套专为低延迟场景设计的远程桌面控制系统，分为**地面站（Ground Unit）**和**机载端（Air Unit）**两部分：

- **地面站** (`main.py`) — 运行在操作员 PC 上，负责接收视频流、渲染画面、捕获键鼠输入并回传控制指令。
- **机载端** (`air_unit_server.py`) — 运行在被控设备上，负责采集屏幕、编码视频流、执行键鼠指令。

两端通过 **mDNS 自动发现**或手动 IP 直连，采用自定义二进制协议（CRC32 校验）通过 UDP 通信，支持 H.264 软编码和 JPEG 回退，内置 FEC 前向纠错以应对网络丢包。

> ⚠️ **关于机载端**
>
> 当前仓库中的 `air_unit_server.py` 是一个 **Python 测试脚本**，用于快速验证协议与功能，适合在普通 PC/树莓派等设备上测试。
>
> **正式的机载端将以 ROS 2 节点形式发布**，届时可直接集成到机器人/无人机的 ROS 2 工作空间中，支持话题订阅、服务调用等标准 ROS 2 接口。ROS 2 节点版本将在单独的仓库中发布，敬请关注。

---

## 功能特性

### 视频传输
- H.264（软编码）/ JPEG 双编码器，可实时切换
- UDP 分片传输，支持大帧分块重组
- **FEC 前向纠错**：可配置冗余度（0.0–0.5），丢包时无需重传即可恢复帧数据
- 动态码率和帧率调节
- 帧级 ACK/NACK 机制，支持选择性重传

### 控制输入
- **全键盘透传**：10 字节位图编码，支持任意多键同按
- **鼠标透传**：相对坐标（dx/dy）+ 5 键位图（含侧键）+ 滚轮
- 可配置鼠标灵敏度
- 菜单模式 / 控制模式一键切换

### 网络与延迟
- **mDNS 自动发现**（zeroconf），局域网内零配置连接
- **四时间戳 RTT 测量**：精确计算上行/下行单向延迟和时钟偏移
- 心跳保活 + 自动重连机制
- 最近 100 帧丢包率、延迟、带宽统计

### 界面与显示
- 基于 **PyImGui + OpenGL** 的硬件加速 UI，帧率无关动画
- 多标签菜单页（见下方[标签页说明](#菜单各标签页说明)）
- 弹性平滑滚动（指数衰减插值）
- 内置开发者控制台（支持自定义命令扩展）
- 多显示器支持，窗口/无边框全屏自由切换
- 实时性能图表（FPS、延迟历史曲线）

### 录制与截图
- 屏幕录制：MP4 / MKV / AVI 格式，帧率跟随流帧率
- 一键截图，保存至可配置目录
- 鼠标光标叠加渲染至录制帧

### 其他
- DPI 感知（Per-Monitor DPI Aware）
- 审计日志：记录所有关键操作（连接/断开/参数变更/录制）
- 参数双向同步：地面站与机载端参数实时同步
- 配置持久化（config.json）

---

## 系统架构

```
PIP-Link/
├── main.py                    # 程序入口
├── config.py                  # 全局常量配置
├── config.json                # 用户运行时配置（持久化）
├── air_unit_server.py         # 机载端测试脚本（被控设备运行）
│
├── core/                      # 核心应用层
│   ├── app.py                 # Application — 主循环与子系统协调
│   ├── window_manager.py      # 窗口模式、分辨率、多显示器管理
│   ├── recorder.py            # 视频录制与截图
│   └── command.py             # 命令模式基类与注册表
│
├── ui/                        # 用户界面层
│   ├── imgui_ui.py            # ImGuiUI — 所有菜单、HUD、状态栏绘制
│   ├── renderer.py            # VideoRenderer — OpenGL 纹理视频渲染
│   ├── input_handler.py       # InputHandler — 键鼠事件捕获
│   ├── console.py             # GameConsole — 开发者控制台
│   └── theme.py               # Theme — ImGui 主题配置
│
├── network/                   # 网络层
│   ├── session.py             # SessionManager — 连接生命周期管理
│   ├── protocol.py            # Protocol — 消息编解码与 CRC32 校验
│   ├── control_sender.py      # ControlSender — 控制指令发送（UDP）
│   ├── video_receiver.py      # VideoReceiver — 视频帧接收与重组
│   ├── heartbeat.py           # HeartbeatManager — 心跳与 RTT 测量
│   ├── fec.py                 # FECEncoder / FECDecoder — 前向纠错
│   └── keyboard_encoder.py    # KeyboardEncoder — 键盘状态采集
│
├── logic/                     # 业务逻辑层
│   ├── param_manager.py       # ParamManager — 参数存取
│   ├── latency_calculator.py  # LatencyCalculator — RTT/单向延迟计算
│   ├── status_monitor.py      # StatusMonitor — 性能统计与历史
│   ├── config_manager.py      # ConfigManager — JSON 配置持久化
│   └── audit_logger.py        # AuditLogger — 操作审计日志
│
└── assets/                    # 静态资源（字体、图标）
```

### 通信协议

```
[Magic:2][Version:1][MsgType:1][Reserved:1][Seq:4][...payload...][CRC32:4]
```

| 消息类型 | 值 | 用途 |
|---|---|---|
| CONTROL_COMMAND | 0x01 | 键鼠控制指令（37 字节） |
| PARAM_UPDATE | 0x02 | 参数同步 |
| PARAM_QUERY | 0x03 | 参数查询 |
| HEARTBEAT | 0x04 | 心跳保活 + RTT |
| ACK | 0x05 | 控制指令确认 |
| VIDEO_ACK | 0x06 | 视频帧确认 |
| VIDEO_NACK | 0x07 | 视频帧选择性重传请求 |

---

## 快速开始

### 环境要求

| 项目 | 要求 |
|---|---|
| 操作系统 | Windows 10/11 x64（地面站）；Linux / Windows（机载端） |
| Python | 3.10 或更高版本 |
| 网络 | 地面站与机载端须处于同一局域网（mDNS 发现），或 IP 可达 |

### 从源码运行

**第一步：克隆仓库**

```bash
git clone https://github.com/P1ne4pp1e/PIP-Link.git
cd PIP-Link
```

**第二步：创建环境并安装依赖**

```bash
conda create -n PIP_Link python=3.10
conda activate PIP_Link
pip install -r requirements.txt
```

**第三步：在被控设备上启动机载端**

```bash
python air_unit_server.py
```

机载端启动后将自动通过 mDNS 广播自身服务（`_pip-link._udp.local.`），终端会打印监听地址和端口。

**第四步：在操作员 PC 上启动地面站**

```bash
conda activate PIP_Link
python main.py
```

### 安装包安装（仅地面站）

从 [Releases](https://github.com/P1ne4pp1e/PIP-Link/releases) 页面下载最新版 `PIP-Link-Setup-v2.0.0.exe`，按向导安装即可，无需配置 Python 环境。

---

## 连接教程

![connect_001](.\assets\imgs\connect_001.png)

### 方式一：自动发现（推荐）

适用于地面站与机载端处于同一局域网的情况。

1. 确认机载端已运行（`python air_unit_server.py`）
2. 启动地面站后，按 `Tab` 键打开菜单
3. 进入 **CONNECTION** 标签页
4. 点击 **Scan** 按钮，等待 1–3 秒，设备列表中会出现已发现的机载端
5. 点击列表中的设备名称将其选中（高亮显示）
6. 点击 **Connect** 按钮
7. 状态栏显示 `CONNECTED`，视频画面出现，连接成功

> 若扫描后列表为空，请检查：
> - 防火墙是否放行 UDP 端口（5005、5006）和 mDNS（UDP 5353）
> - 机载端是否已正常启动（终端无报错）
> - 两台设备是否在同一子网

### 方式二：手动输入设备名

若自动发现失败，但已知机载端设备名：

1. 在 CONNECTION 标签页的输入框中输入机载端设备名（与机载端终端显示的名称一致）
2. 点击 **Connect by Name**，地面站将尝试 mDNS 解析该名称并连接
3. 解析超时（约 5 秒）说明设备不可达，请改用方式三

### 方式三：直接 IP 连接

> 当前版本通过 mDNS 解析，如需纯 IP 直连请在 `air_unit_server.py` 中确认端口，并在防火墙中放行对应端口。

### 断开连接

- 打开菜单 → CONNECTION 标签页 → 点击 **Disconnect**
- 或关闭地面站窗口，会自动断开并发送断线通知

---

## 使用教程

### 默认快捷键

| 按键 | 功能 |
|---|---|
| `ESC` | 打开 / 关闭菜单（同时切换控制模式） |
| `~ ` | 显示 / 隐藏开发者控制台 |
| `TAB` | 显示 / 隐藏 HUD 状态栏 |
| `F5` | 切换就绪（Ready）状态 |

> 所有快捷键均可在菜单 **CONTROL SETTINGS** 标签页中自定义。

### 开始控制

连接成功后，按以下步骤进入控制模式：

1. 按 `ESC` **关闭菜单**，回到视频画面
2. 按 `F5`（或菜单内配置的 Ready 键）进入**就绪状态**
   - 状态栏右侧指示灯变为绿色，提示"READY"
   - 鼠标自动锁定在窗口内（相对模式）
3. 此后所有键盘和鼠标输入实时透传至机载端

> **退出控制模式**：按 `ESC` 打开菜单，鼠标自动解锁，键盘恢复本地输入；或按`F5`进入NOT READY状态。

---

> <img src=".\assets\imgs\hud_001.png" alt="hud_001" style="zoom:50%;" />
>
> <img src=".\assets\imgs\hud_002.png" alt="hud_002" style="zoom:80%;" />

---



### HUD 说明

控制模式下，屏幕角落会显示轻量级 HUD：

- **左上角**：当前会话状态（CONNECTED / READY）、延迟（ms）、帧率（FPS）
- **右下角**：键盘当前按键状态标签、鼠标移动方向摇杆

按 `TAB` 可随时隐藏/显示 HUD，不影响控制输入。

### 开发者控制台

按 `~` 打开控制台（界面底部滑入），支持以下内置命令：

| 命令 | 功能 |
|---|---|
| `help` | 列出所有可用命令及说明 |
| `clear` | 清空控制台输出 |
| `connect` | 启动设备扫描 |
| `disconnect` | 断开当前连接 |

所有通过 `print()` 输出的日志也会同步显示在控制台中，便于调试。

---

## 菜单各标签页说明

按 `Tab` 打开菜单后，顶部为可横向滚动的标签栏，各标签功能如下：

---

### CONNECTION — 连接管理

用于发现、选择并连接机载端设备。

| 控件 | 说明 |
|---|---|
| **Scan** | 启动 mDNS 扫描，搜索局域网内运行机载端的设备 |
| 设备列表 | 显示已发现的设备名称、IP 地址、端口号，点击选中 |
| **Connect** | 连接当前选中的设备 |
| 名称输入框 | 手动输入设备名称 |
| **Connect by Name** | 通过 mDNS 解析指定名称并连接 |
| **Disconnect** | 断开当前连接 |
| 状态指示 | 实时显示会话状态（IDLE / DISCOVERING / CONNECTING / CONNECTED） |

---

### PARAMETERS — 参数设置

调整控制相关的本地参数（不影响视频流，仅作用于本地输入处理）。

| 参数 | 说明 |
|---|---|
| Mouse Sensitivity | 鼠标灵敏度倍率（0.1×–5.0×） |
| Invert Pitch | 反转鼠标纵轴方向 |
| Show Performance Graph | 是否在诊断页显示历史曲线图 |
| Show Debug Info | 是否显示额外调试信息 |

---

### VIDEO — 视频与流参数

![recording_001](.\assets\imgs\recording_001.png)

调整视频编码和传输参数，修改后实时同步至机载端生效。

| 参数 | 说明 |
|---|---|
| Encoder | 编码器选择：H.264（低延迟，推荐）/ JPEG（兼容性强） |
| Bitrate | 目标码率（kbps），影响画质与带宽占用 |
| FPS | 目标帧率（fps），影响流畅度与延迟 |
| FEC Enabled | 是否启用前向纠错，开启后抗丢包能力显著提升 |
| FEC Redundancy | FEC 冗余度（0.0–0.5），值越高抗丢包越强，带宽消耗越大 |

---

### RECORDING — 录制与截图

管理本地录制和截图功能。

| 控件 | 说明 |
|---|---|
| **Start / Stop Recording** | 开始或停止录制当前视频画面 |
| Recording Format | 录制格式：MP4 / MKV / AVI |
| **Screenshot** | 截图当前帧并保存 |
| **Open Folder** | 打开文件夹选择对话框，更改保存目录 |
| 当前保存路径 | 显示录制和截图的保存根目录 |

录制文件保存在 `<save_dir>/recordings/`，截图保存在 `<save_dir>/screenshots/`。

---

### DIAGNOSTICS — 诊断与性能

![diag_001](.\assets\imgs\diag_001.png)

![diag_002](.\assets\imgs\diag_002.png)

实时展示连接质量与系统性能指标。

| 指标 | 说明 |
|---|---|
| RTT | 往返延迟（ms），反映整体网络质量 |
| Uplink Delay | 上行延迟（控制指令到达机载端的时间） |
| Downlink Delay | 下行延迟（视频帧到达地面站的时间） |
| FPS | 当前视频渲染帧率 |
| Packet Loss | 最近 100 帧的控制指令丢包率 |
| Bandwidth | 当前视频流带宽（kbps） |
| FPS / Latency History | 历史曲线图（最近若干帧，可在 PARAMETERS 中开关） |

---

### CONTROL SETTINGS — 控制与按键绑定

自定义所有快捷键绑定。

| 控件 | 说明 |
|---|---|
| Toggle Menu | 打开/关闭菜单的按键（默认 `Tab`） |
| Toggle Console | 控制台开关键（默认 `~`） |
| Toggle HUD | HUD 开关键（默认 `TAB`） |
| Ready | 就绪状态切换键（默认 `ESC`） |
| **[Click to Bind]** | 点击后按下任意键完成绑定 |

---

### DEBUG — 调试信息

显示底层网络和解码的详细调试数据，适合开发者排查问题。

| 信息 | 说明 |
|---|---|
| Session State | 当前会话状态机状态 |
| Control Seq | 最近发送的控制指令序列号 |
| Video Frame ID | 最近收到的视频帧 ID |
| FEC Stats | FEC 恢复成功/失败次数 |
| Heartbeat | 心跳发送/接收计数 |

---

### AUDIT — 审计日志

![audit_001](.\assets\imgs\audit_001.png)

显示本次运行的操作审计记录，包含时间戳和事件类型：

- `connect` / `disconnect`：连接与断开事件
- `param_change`：参数变更（含参数名与新值）
- `recording`：录制开始/停止及文件路径
- `screenshot`：截图保存路径

审计日志同时持久化写入 `logs/` 目录，便于事后追溯。

---

### ABOUT — 关于

显示版本信息、许可证声明和联系方式。

---

## 配置参数

运行时配置保存在 `config.json`，首次运行时自动生成。

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `stream_encoder` | int | 1 | 编码器：0=JPEG, 1=H.264 |
| `stream_bitrate` | int | 2000 | 码率（kbps） |
| `stream_fps` | int | 30 | 目标帧率 |
| `stream_fec_enabled` | bool | true | FEC 开关 |
| `stream_fec_redundancy` | float | 0.2 | FEC 冗余度（0.0–0.5） |
| `mouse_sensitivity` | float | 1.0 | 鼠标灵敏度倍率 |
| `invert_pitch` | bool | false | 反转鼠标纵轴 |
| `window_mode` | int | 0 | 0=窗口模式, 1=无边框全屏 |
| `fullscreen_display` | int | -1 | 全屏目标显示器（-1=当前） |
| `save_dir` | str | "." | 录制/截图保存根目录 |
| `recording_format` | int | 0 | 0=MP4, 1=MKV, 2=AVI |
| `key_bindings` | object | 见默认值 | 快捷键绑定 |

---

## 开发者接口

PIP-Link 采用高度模块化设计，各子系统通过回调解耦，便于二次开发。

### Application

```python
from core.app import Application

app = Application()
app.connect("_pip-link._udp.local.")  # 启动 mDNS 发现
app.disconnect()                       # 断开连接
app.run()                              # 主循环（阻塞）
```

### SessionManager

```python
from network.session import SessionManager, SessionState

session = SessionManager()

# 注册事件回调
session.on_state_changed        = lambda state: ...   # SessionState 变化
session.on_services_discovered  = lambda svcs: ...    # 发现新设备
session.on_param_response       = lambda params: ...  # 收到机载端参数回包
session.on_ready_changed        = lambda ready: ...   # Ready 状态变化

session.start_discovery("_pip-link._udp.local.")     # 开始扫描
session.connect_to_service("name", service_info)     # 连接指定服务
session.send_param_update({"bitrate": 4000})         # 发送参数更新
stats = session.get_statistics()                      # 获取统计字典
```

### Protocol

```python
from network.protocol import Protocol

# 构建控制指令
data = Protocol.build_control_command(
    seq=1, t1=time.time(),
    keyboard_state=b'\x00' * 10,
    mouse_dx=10, mouse_dy=-5,
    mouse_buttons=0b00001,   # bit0=左键, bit1=右键, bit2=中键, bit3/4=侧键
    scroll_delta=0
)

# 解析 ACK（t2=机载端收到时刻, t3=机载端发送ACK时刻）
seq, t2, t3 = Protocol.parse_ack(raw_bytes)

# 构建/解析心跳
hb = Protocol.build_heartbeat(seq=1, t1=time.time())
seq, t1 = Protocol.parse_heartbeat(raw_bytes)
```

### 自定义命令（控制台扩展）

```python
from core.command import Command, CommandResult, CommandRegistry

class MyCommand(Command):
    @property
    def name(self) -> str:
        return "hello"

    @property
    def description(self) -> str:
        return "Print a greeting"

    def execute(self, args: list[str]) -> CommandResult:
        return CommandResult(success=True, message=f"Hello, {args[0] if args else 'world'}!")

registry = CommandRegistry()
registry.register(MyCommand())
result = registry.dispatch("hello PIP-Link")
```

### ParamManager

```python
from logic.param_manager import ParamManager

pm = ParamManager()
pm.set_param("stream_fps", 60)
fps = pm.get_param("stream_fps")       # -> int
all_params = pm.get_all_params()       # -> dict
```

---

## 打包发布

### PyInstaller 打包

```bash
conda activate PIP_Link
pyinstaller PIP-Link.spec
```

输出位于 `dist/PIP-Link.exe`。

### Inno Setup 安装包

1. 安装 [Inno Setup 6](https://jrsoftware.org/isinfo.php)
2. 完成 PyInstaller 打包（确保 `dist/` 目录存在）
3. 用 Inno Setup Compiler 打开 `installer.iss`，按 `Ctrl+F9` 编译
4. 安装包输出至 `installer/PIP-Link-Setup-v2.0.0.exe`

---

## 许可证

本项目基于 [Apache License 2.0](LICENSE) 开源。

```
Copyright 2025 P1ne4pp1e

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0
```

---

## 联系方式

- **邮箱**：mabolong2006@outlook.com
- **GitHub Issues**：[提交 Bug 或功能请求](https://github.com/P1ne4pp1e/PIP-Link/issues)
- **Releases**：[下载最新版本](https://github.com/P1ne4pp1e/PIP-Link/releases)

---

<div align="center">

*Built with ❤️ for low-latency remote control*

</div>
