# 机载端服务器 - 远程连接测试

## 概述

`air_unit_server.py` 是一个在机载端（Ubuntu）上运行的服务器脚本，用于测试与客户端的远程连接。

## 功能

- ✓ mDNS 服务注册（让客户端能发现）
- ✓ UDP 控制指令接收和 ACK 响应
- ✓ 模拟视频帧发送
- ✓ 心跳检测
- ✓ 实时统计信息

## 安装依赖

在机载端（Ubuntu）上安装：

```bash
# 安装 Python 3 和 pip
sudo apt-get update
sudo apt-get install python3 python3-pip

# 安装依赖
pip3 install zeroconf
```

## 基本使用

### 1. 启动服务器（默认配置）

```bash
python3 air_unit_server.py
```

输出示例：
```
2026-04-19 18:30:45 - INFO - Starting Air Unit Server: air_unit_01
2026-04-19 18:30:45 - INFO - Local hostname: ubuntu-machine
2026-04-19 18:30:45 - INFO - Local IP: 192.168.1.100
2026-04-19 18:30:45 - INFO - mDNS service registered: air_unit_01._pip_link._udp.local.
2026-04-19 18:30:45 - INFO - Control socket listening on port 6000
2026-04-19 18:30:45 - INFO - Video socket listening on port 5000
2026-04-19 18:30:45 - INFO - Air Unit Server started successfully
2026-04-19 18:30:45 - INFO - Waiting for client connection on 6000...
```

### 2. 自定义配置

```bash
# 指定机载端名称
python3 air_unit_server.py --name air_unit_02

# 指定端口
python3 air_unit_server.py --control-port 6001 --video-port 5001

# 启用详细日志
python3 air_unit_server.py --verbose

# 运行指定时间（秒）
python3 air_unit_server.py --duration 300

# 组合使用
python3 air_unit_server.py --name air_unit_02 --control-port 6001 --verbose
```

### 3. 停止服务器

按 `Ctrl+C` 停止，会打印最终统计信息：

```
============================================================
Air Unit Statistics
============================================================
Control commands received: 500
Heartbeats received: 50
ACKs sent: 550
Video frames sent: 1500
Connected client: 192.168.1.50:54119
============================================================
```

## 远程连接测试流程

### 前置条件

- 机载端（Ubuntu）和客户端（Windows）在同一网络
- 两台机器能互相 ping 通
- 防火墙允许 UDP 通信

### 步骤 1：启动机载端服务器

在 Ubuntu 上运行：

```bash
python3 air_unit_server.py
```

记下输出的 IP 地址和端口号。

### 步骤 2：启动客户端

在 Windows 上运行：

```bash
python main.py
```

### 步骤 3：连接到机载端

在客户端菜单中：
1. 按 ESC 打开菜单
2. 点击 SCAN 按钮发现机载端
3. 应该看到 `air_unit_01` 服务
4. 点击 Connect 连接

### 步骤 4：验证连接

**在客户端中观察：**
- 状态显示 "Connected"
- 视频帧开始显示
- 统计信息显示 FPS、RTT、丢包率

**在机载端中观察：**
```
2026-04-19 18:31:00 - INFO - Client connected: 192.168.1.50:54119
2026-04-19 18:31:00 - DEBUG - Received control command: seq=1, t1=1234567890.123456
2026-04-19 18:31:00 - DEBUG - Sent ACK to 192.168.1.50:54119, seq=1
2026-04-19 18:31:00 - DEBUG - Sent 30 video frames
```

## 命令行参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--name` | `air_unit_01` | 机载端名称 |
| `--control-port` | `6000` | 控制指令端口 |
| `--video-port` | `5000` | 视频端口 |
| `--duration` | `0` | 运行时间（秒），0 表示无限 |
| `--verbose` | 否 | 启用详细日志 |

## 测试场景

### 场景 1：基础连接测试

**目标：** 验证客户端能否发现和连接到机载端

**步骤：**
1. 启动机载端服务器
2. 启动客户端
3. 点击 SCAN 和 Connect

**预期结果：**
- ✓ 客户端显示 "Connected" 状态
- ✓ 机载端显示 "Client connected"
- ✓ 客户端显示视频帧

### 场景 2：控制指令测试

**目标：** 验证控制指令的发送和 ACK 接收

**步骤：**
1. 连接到机载端
2. 按 WASD 控制移动
3. 观察机载端的日志

**预期结果：**
- ✓ 机载端显示 "Control commands received" 增加
- ✓ 机载端显示 "ACKs sent" 增加
- ✓ 客户端显示 RTT 延迟

### 场景 3：长时间运行测试

**目标：** 测试系统的稳定性

**步骤：**
```bash
# 在机载端运行 10 分钟
python3 air_unit_server.py --duration 600
```

**观察指标：**
- 内存使用是否稳定
- 是否有错误日志
- 统计数据是否正常增长

### 场景 4：多客户端测试

**目标：** 测试多个客户端连接

**步骤：**
1. 启动机载端服务器
2. 启动第一个客户端并连接
3. 启动第二个客户端并连接

**预期结果：**
- ✓ 两个客户端都能连接
- ✓ 机载端接收来自两个客户端的指令
- ✓ 两个客户端都能接收视频

## 故障排除

### 问题 1：mDNS 服务注册失败

**错误：**
```
Failed to start mDNS: [Errno 98] Address already in use
```

**原因：** 端口已被占用

**解决：**
```bash
# 使用不同的端口
python3 air_unit_server.py --control-port 6002 --video-port 5002
```

### 问题 2：客户端无法发现服务

**原因：**
- 防火墙阻止 mDNS（端口 5353）
- 网络不连通
- 服务器未启动

**解决：**
1. 检查防火墙设置
   ```bash
   sudo ufw allow 5353/udp  # 允许 mDNS
   sudo ufw allow 6000/udp  # 允许控制端口
   sudo ufw allow 5000/udp  # 允许视频端口
   ```
2. 检查网络连接
   ```bash
   ping <client_ip>
   ```
3. 确保服务器已启动

### 问题 3：连接后无视频

**原因：** 视频端口配置错误或防火墙阻止

**解决：**
1. 确保视频端口与控制端口一致（默认相差 1000）
2. 检查防火墙设置
   ```bash
   sudo ufw allow 5000/udp
   ```

### 问题 4：连接不稳定

**原因：** 网络延迟或丢包

**解决：**
1. 检查网络连接质量
   ```bash
   ping -c 100 <client_ip>
   ```
2. 查看详细日志
   ```bash
   python3 air_unit_server.py --verbose
   ```

## 性能指标

在局域网测试中的典型性能：

| 指标 | 值 |
|------|-----|
| 控制指令接收率 | 50 Hz |
| 视频帧发送率 | ~30 fps |
| ACK 响应时间 | < 1ms |
| RTT 测量精度 | 微秒级 |
| 平均丢包率 | < 1% |

## 日志和调试

### 启用详细日志

```bash
python3 air_unit_server.py --verbose
```

### 常见日志消息

**启动：**
```
Starting Air Unit Server: air_unit_01
Local IP: 192.168.1.100
mDNS service registered: air_unit_01._pip_link._udp.local.
```

**客户端连接：**
```
Client connected: 192.168.1.50:54119
```

**接收指令：**
```
Received control command: seq=1, t1=1234567890.123456
Sent ACK to 192.168.1.50:54119, seq=1
```

**发送视频：**
```
Sent 30 video frames
```

## 扩展功能

### 模拟网络延迟

编辑 `_send_ack` 方法：

```python
def _send_ack(self, addr: tuple, seq: int):
    time.sleep(0.01)  # 10ms 延迟
    # ... 发送 ACK
```

### 模拟丢包

编辑 `_video_sender_thread` 方法：

```python
import random
if random.random() < 0.05:  # 5% 丢包率
    continue
self.video_socket.sendto(frame_data, self.client_addr)
```

### 自定义视频数据

编辑 `_video_sender_thread` 方法中的视频帧生成：

```python
# 替换这一行
frame_data = struct.pack("=I", frame_id) + b"VIDEO_FRAME_DATA" * 100

# 为你的自定义数据
frame_data = generate_custom_video_frame(frame_id)
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
  - device_type: air_unit
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

**心跳：**
```
[Magic:2][Version:1][MsgType:1][Reserved:1][Seq:4][t1:8][CRC32:4]
```

### 线程模型

- 主线程：mDNS 服务管理和统计输出
- 控制接收线程：接收指令和心跳，发送 ACK
- 视频发送线程：模拟视频帧发送

## 常见问题

**Q: 如何在后台运行服务器？**
A: 使用 `nohup` 或 `screen`：
```bash
nohup python3 air_unit_server.py > air_unit.log 2>&1 &
# 或
screen -S air_unit
python3 air_unit_server.py
# 按 Ctrl+A 然后 D 分离
```

**Q: 如何查看运行日志？**
A:
```bash
tail -f air_unit.log
```

**Q: 如何同时运行多个服务器？**
A: 使用不同的端口：
```bash
python3 air_unit_server.py --name air_unit_01 --control-port 6000 &
python3 air_unit_server.py --name air_unit_02 --control-port 6001 &
```

**Q: 如何测试网络延迟？**
A: 在 `_send_ack` 方法中添加 `time.sleep()` 来模拟延迟。

## 支持

遇到问题？
1. 检查日志输出（使用 `--verbose` 获取详细日志）
2. 查看 USER_GUIDE.md 了解客户端使用
3. 查看 TESTING_GUIDE.md 了解测试方法
4. 查看 BACKEND_SUMMARY.md 了解系统架构
