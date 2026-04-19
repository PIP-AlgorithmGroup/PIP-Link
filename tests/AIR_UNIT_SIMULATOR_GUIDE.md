# 机载端 mDNS 测试脚本使用指南

## 概述

`air_unit_simulator.py` 是一个机载端模拟器，用于测试 PIP-Link 客户端的基础功能。它提供：

- mDNS 服务注册（让客户端能发现）
- UDP 控制指令接收和 ACK 响应
- 模拟视频帧发送

## 安装依赖

```bash
# 在 PIP_Link 环境中
conda activate PIP_Link
pip install zeroconf
```

## 基本使用

### 1. 启动模拟器（默认配置）

```bash
python tests/air_unit_simulator.py
```

输出示例：
```
2026-04-19 18:30:45,123 - __main__ - INFO - Starting Air Unit Simulator: air_unit_01
2026-04-19 18:30:45,124 - __main__ - INFO - Local IP: 192.168.1.100
2026-04-19 18:30:45,125 - __main__ - INFO - mDNS service registered: air_unit_01._pip_link._udp.local.
2026-04-19 18:30:45,126 - __main__ - INFO - Control socket listening on port 6000
2026-04-19 18:30:45,127 - __main__ - INFO - Video socket listening on port 5000
2026-04-19 18:30:45,128 - __main__ - INFO - Air Unit Simulator started successfully
```

### 2. 自定义配置

```bash
# 指定机载端名称
python tests/air_unit_simulator.py --name air_unit_02

# 指定端口
python tests/air_unit_simulator.py --control-port 6001 --video-port 5001

# 运行指定时间（秒）
python tests/air_unit_simulator.py --duration 60

# 组合使用
python tests/air_unit_simulator.py --name air_unit_02 --control-port 6001 --duration 120
```

### 3. 停止模拟器

按 `Ctrl+C` 停止，会打印最终统计信息：

```
============================================================
Air Unit Statistics
============================================================
Control commands received: 500
ACKs sent: 500
Video frames sent: 1500
============================================================
```

## 测试流程

### 场景 1：基础连接测试

**步骤 1：启动模拟器**
```bash
python tests/air_unit_simulator.py
```

**步骤 2：启动客户端**
```bash
python main.py
```

**步骤 3：在客户端菜单中点击 SCAN**
- 应该发现 `air_unit_01` 服务
- 点击 Connect 连接

**预期结果：**
- 客户端显示 "Connected" 状态
- 模拟器输出显示接收到控制指令和发送 ACK
- 客户端显示视频帧和统计信息

### 场景 2：多机载端测试

**终端 1：启动第一个机载端**
```bash
python tests/air_unit_simulator.py --name air_unit_01 --control-port 6000
```

**终端 2：启动第二个机载端**
```bash
python tests/air_unit_simulator.py --name air_unit_02 --control-port 6001
```

**步骤 3：启动客户端**
```bash
python main.py
```

**预期结果：**
- 客户端 SCAN 时发现两个机载端
- 可以连接到任一机载端

### 场景 3：性能测试

**启动模拟器（运行 60 秒）**
```bash
python tests/air_unit_simulator.py --duration 60
```

**启动客户端并连接**
```bash
python main.py
```

**观察统计信息：**
- 控制指令接收数
- ACK 发送数
- 视频帧发送数
- 客户端的 FPS、RTT、丢包率

## 命令行参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--name` | `air_unit_01` | 机载端名称 |
| `--control-port` | `6000` | 控制指令端口 |
| `--video-port` | `5000` | 视频端口 |
| `--duration` | `0` | 运行时间（秒），0 表示无限 |

## 日志级别

默认日志级别为 INFO。要查看更详细的日志，可以修改代码中的：

```python
logging.basicConfig(level=logging.DEBUG)  # 改为 DEBUG
```

## 故障排除

### 问题 1：mDNS 服务注册失败

**错误信息：**
```
Failed to start mDNS: [Errno 10048] Only one usage of each socket address
```

**原因：** 端口已被占用

**解决：**
```bash
# 使用不同的端口
python tests/air_unit_simulator.py --control-port 6002 --video-port 5002
```

### 问题 2：客户端无法发现服务

**原因：**
- 防火墙阻止 mDNS（端口 5353）
- 网络不连通
- 模拟器未启动

**解决：**
1. 检查防火墙设置
2. 检查网络连接
3. 确保模拟器已启动

### 问题 3：客户端连接后无视频

**原因：**
- 视频端口配置错误
- 客户端和模拟器在不同网络

**解决：**
1. 确保视频端口与控制端口一致（默认相差 1000）
2. 检查网络连接

## 性能指标

在本地测试中的典型性能：

| 指标 | 值 |
|------|-----|
| 控制指令发送率 | 50 Hz |
| 视频帧发送率 | ~30 fps |
| ACK 响应时间 | < 1ms |
| RTT 测量精度 | 微秒级 |

## 扩展功能

### 添加自定义视频数据

修改 `_video_sender_thread` 方法中的视频帧生成：

```python
# 替换这一行
frame_data = struct.pack("=I", frame_id) + b"VIDEO_FRAME_DATA" * 100

# 为你的自定义数据
frame_data = generate_custom_video_frame(frame_id)
```

### 模拟网络延迟

在 `_send_ack` 方法中添加延迟：

```python
time.sleep(0.01)  # 10ms 延迟
self.control_socket.sendto(message, addr)
```

### 模拟丢包

在 `_video_sender_thread` 中添加随机丢包：

```python
import random
if random.random() < 0.05:  # 5% 丢包率
    continue
self.video_socket.sendto(frame_data, client_addr)
```

## 技术细节

### mDNS 服务格式

```
Service Type: _pip_link._udp.local.
Service Name: air_unit_01._pip_link._udp.local.
Properties:
  - video_port: 5000
  - control_port: 6000
  - version: 1.0
```

### 消息格式

**控制指令：**
```
[Magic:2][Version:1][MsgType:1][Reserved:1][Seq:4][t1:8][Payload:16][CRC32:4]
```

**ACK 响应：**
```
[Magic:2][Version:1][MsgType:1][Reserved:1][Seq:4][t2:8][t3:8][CRC32:4]
```

### 线程模型

- 主线程：mDNS 服务管理
- 控制接收线程：接收指令并发送 ACK
- 视频发送线程：模拟视频帧发送

## 常见问题

**Q: 如何同时运行多个模拟器？**
A: 使用不同的端口和名称：
```bash
python tests/air_unit_simulator.py --name air_unit_01 --control-port 6000 &
python tests/air_unit_simulator.py --name air_unit_02 --control-port 6001 &
```

**Q: 模拟器可以在 Windows 上运行吗？**
A: 可以，但 mDNS 在 Windows 上的支持可能有限。建议在 Linux 或 macOS 上测试。

**Q: 如何测试网络延迟？**
A: 在 `_send_ack` 方法中添加 `time.sleep()` 来模拟延迟。

**Q: 如何测试丢包？**
A: 在 `_video_sender_thread` 中添加随机跳过发送。

## 支持

遇到问题？
1. 检查日志输出
2. 查看 USER_GUIDE.md 了解客户端使用
3. 查看 BACKEND_SUMMARY.md 了解系统架构
