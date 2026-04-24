<div align="center">

# PIP-Link

**高性能低延迟远程桌面控制系统**

[![Version](https://img.shields.io/badge/version-v2.0.0-blue.svg)](https://github.com/P1ne4pp1e/PIP-Link/releases)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows-lightgrey.svg)]()
[![Python](https://img.shields.io/badge/python-3.10%2B-yellow.svg)]()

*基于 UDP + H.264/JPEG 编码的实时视频流传输与键鼠控制系统*

</div>

---

> **📸 截图预留区 — 主界面**
>
> *[在此处插入：连接成功后的主视频画面截图，展示状态栏、HUD 覆盖层]*

---

## 目录

- [项目简介](#项目简介)
- [功能特性](#功能特性)
- [系统架构](#系统架构)
- [快速开始](#快速开始)
  - [环境要求](#环境要求)
  - [从源码运行](#从源码运行)
  - [安装包安装](#安装包安装)
- [使用说明](#使用说明)
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

两端通过 **mDNS 自动发现**或手动 IP 直连，采用自定义二进制协议（CRC32 校验）通过 UDP 通信，支持 H.264 硬/软编码和 JPEG 回退，内置 FEC 前向纠错以应对网络丢包。

---

## 功能特性

### 视频传输
- H.264（软编码）/ JPEG 双编码器，可实时切换
- UDP 分片传输，单帧最大支持 65535 块
- **FEC 前向纠错**：可配置冗余度（0.0–0.5），无需重传即可恢复丢失数据包
- 动态码率和帧率调节（1–60 FPS，100 kbps–20 Mbps）
- 帧级 ACK/NACK 机制，支持选择性重传

### 控制输入
- **全键盘透传**：10 字节位图编码，支持同时按下任意组合键
- **鼠标透传**：相对坐标（dx/dy）+ 5 键位图（含侧键）+ 滚轮
- 可配置鼠标灵敏度（0.1×–5.0×）
- 菜单模式 / 控制模式一键切换（默认 `Tab` 键）

### 网络与延迟
- **mDNS 自动发现**（zeroconf），局域网内零配置连接
- **四时间戳 RTT 测量**：精确计算上行/下行单向延迟和时钟偏移
- 心跳保活 + 自动重连机制
- 统计窗口：最近 100 帧的丢包率、延迟、带宽

### 用户界面
- 基于 **PyImGui + OpenGL** 的硬件加速 UI（帧率无关动画）
- CS2 风格深色主题，三级字体（标题/正文/等宽）
- 多标签菜单：连接、参数、视频、录制、诊断、控制设置、调试、审计、关于
- 弹性平滑滚动（指数衰减，tau=0.08s）
- 内置开发者控制台（支持自定义命令）

### 录制与截图
- 屏幕录制：MP4 / MKV / AVI 格式，帧率跟随流帧率
- 一键截图，保存至可配置目录
- 鼠标光标叠加渲染至录制帧

### 其他
- 多显示器支持：全屏可指定目标显示器
- DPI 感知（Per-Monitor DPI Aware）
- 审计日志：记录所有关键操作（连接/断开/参数变更/录制）
- 参数双向同步：地面站与机载端参数实时同步

---

## 系统架构

```
PIP-Link/
├── main.py                    # 程序入口
├── config.py                  # 全局常量配置
├── config.json                # 用户运行时配置（持久化）
├── air_unit_server.py         # 机载端服务器（被控设备运行）
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

### 通信协议消息格式

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

- **操作系统**：Windows 10/11 x64（地面站）；Linux/Windows（机载端）
- **Python**：3.10 或更高版本
- **conda 环境**（推荐）：`PIP_Link`

### 从源码运行

**1. 克隆仓库**

```bash
git clone https://github.com/P1ne4pp1e/PIP-Link.git
cd PIP-Link
```

**2. 创建 conda 环境并安装依赖**

```bash
conda create -n PIP_Link python=3.10
conda activate PIP_Link
pip install -r requirements.txt
```

**3. 在被控设备上启动机载端**

```bash
python air_unit_server.py
```

机载端启动后会自动通过 mDNS 广播自身服务（`_pip-link._udp.local.`）。

**4. 在操作员 PC 上启动地面站**

```bash
conda run -n PIP_Link python main.py
# 或激活环境后直接运行：
python main.py
```

---

> **📸 截图预留区 — 连接界面**
>
> *[在此处插入：CONNECTION 标签页截图，展示设备发现列表和连接按钮]*

---

**5. 连接设备**

- 打开菜单（默认 `Tab` 键）→ **CONNECTION** 标签页
- 点击 **Scan** 扫描局域网内的机载端设备
- 从列表中选择目标设备，或在输入框中直接输入设备名称
- 点击 **Connect**

### 安装包安装

从 [Releases](https://github.com/P1ne4pp1e/PIP-Link/releases) 页面下载最新版 `PIP-Link-Setup-v2.0.0.exe`，按向导安装即可。

---

## 使用说明

### 默认快捷键

| 按键 | 功能 |
|---|---|
| `Tab` | 切换菜单 / 控制模式 |
| `F1` | 切换开发者控制台 |
| `F2` | 切换 HUD 显示 |
| `F3` | 切换就绪状态（Ready） |

> 所有快捷键均可在菜单 **CONTROL SETTINGS** 标签页中自定义。

### 控制模式

进入 **Ready** 状态（状态栏右侧绿色指示灯亮起）后，键盘和鼠标输入将实时转发至机载端。此时：

- 鼠标被锁定在窗口内（相对模式）
- 所有键盘输入透传，不触发本地快捷键（除退出菜单的绑定键外）

按菜单键（默认 `Tab`）可随时退出控制模式，恢复正常鼠标。

---

> **📸 截图预留区 — 控制模式 HUD**
>
> *[在此处插入：控制模式下的 HUD 截图，展示键盘状态、鼠标摇杆、延迟信息]*

---

### 视频参数调整

菜单 → **VIDEO** 标签页：

- **Encoder**：H.264（低延迟，推荐）/ JPEG（兼容性好）
- **Bitrate**：100 kbps – 20 Mbps，拖动滑块实时生效
- **FPS**：1 – 60，影响延迟和带宽
- **FEC**：开启后可在丢包率 < 冗余度时无损恢复帧数据

### 录制与截图

菜单 → **RECORDING** 标签页：

- 点击 **Start Recording** 开始录制，**Stop** 停止
- 点击 **Screenshot** 截图当前帧
- 点击 **Open Folder** 选择保存目录

---

## 配置参数

运行时配置保存在 `config.json`，以下为主要参数说明：

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `stream_encoder` | int | 1 | 编码器：0=JPEG, 1=H.264 |
| `stream_bitrate` | int | 2000 | 码率（kbps） |
| `stream_fps` | int | 30 | 目标帧率 |
| `stream_fec_enabled` | bool | true | FEC 开关 |
| `stream_fec_redundancy` | float | 0.2 | FEC 冗余度（0.0–0.5） |
| `mouse_sensitivity` | float | 1.0 | 鼠标灵敏度倍率 |
| `window_mode` | int | 0 | 0=窗口, 1=无边框全屏 |
| `fullscreen_display` | int | -1 | 全屏目标显示器（-1=当前） |
| `save_dir` | str | "." | 录制/截图保存目录 |
| `recording_format` | int | 0 | 0=MP4, 1=MKV, 2=AVI |

---

## 开发者接口

PIP-Link 采用模块化设计，各子系统通过依赖注入组合，便于扩展。

### Application 公开 API

```python
from core.app import Application

app = Application()

# 启动 mDNS 发现并连接
app.connect("_pip-link._udp.local.")

# 断开连接
app.disconnect()

# 主循环
app.run()
```

### SessionManager

```python
from network.session import SessionManager, SessionState

session = SessionManager()

# 注册回调
session.on_state_changed = lambda state: print(f"State: {state}")
session.on_services_discovered = lambda services: print(services)
session.on_param_response = lambda params: print(params)

# 发现设备
session.start_discovery("_pip-link._udp.local.")

# 直接连接
session.connect_to_service("device_name", service_info_dict)

# 获取统计信息
stats = session.get_statistics()
# 返回：{"latency_ms", "fps", "packet_loss", "bandwidth_kbps", ...}
```

### Protocol

```python
from network.protocol import Protocol

# 构建控制指令
data = Protocol.build_control_command(
    seq=1, t1=time.time(),
    keyboard_state=b'\x00' * 10,
    mouse_dx=10, mouse_dy=-5,
    mouse_buttons=0b00001,  # 左键
    scroll_delta=0
)

# 解析 ACK
seq, t2, t3 = Protocol.parse_ack(raw_bytes)

# 构建心跳
hb = Protocol.build_heartbeat(seq=42, t1=time.time())
```

### 自定义命令

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

# 读取参数
bitrate = pm.get_param("stream_bitrate")  # -> int

# 设置参数
pm.set_param("stream_fps", 60)

# 获取全部参数
all_params = pm.get_all_params()  # -> dict
```

---

## 打包发布

### 使用 PyInstaller 打包

```bash
conda activate PIP_Link
pyinstaller PIP-Link.spec
```

打包完成后，可执行文件位于 `dist/PIP-Link.exe`。

### 构建安装包（Inno Setup）

1. 安装 [Inno Setup 6](https://jrsoftware.org/isinfo.php)
2. 确保已完成 PyInstaller 打包（`dist/` 目录存在）
3. 用 Inno Setup 编译器打开 `installer.iss`，点击 **Build** → **Compile**
4. 安装包输出至 `installer/PIP-Link-Setup-v2.0.0.exe`

---

> **📸 截图预留区 — 安装向导**
>
> *[在此处插入：Inno Setup 安装向导截图]*

---

## 许可证

本项目基于 [MIT License](LICENSE) 开源。

```
Copyright (c) 2025 P1ne4pp1e
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
