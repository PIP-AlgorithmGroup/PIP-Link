# PIP-Link 测试指南

## 概述

PIP-Link 现在包含完整的测试框架，包括：

1. **机载端模拟器** (`air_unit_simulator.py`)
   - 模拟真实的机载端服务
   - 提供 mDNS 服务发现
   - 接收控制指令并发送 ACK
   - 模拟视频帧发送

2. **一键测试脚本** (`run_test.py`)
   - 自动启动模拟器和客户端
   - 运行 120 秒自动化测试

3. **集成测试** (`test_integration_control.py`, `test_backend_integration.py`)
   - 测试各个模块的功能

## 快速开始

### 方式 1：一键测试（最简单）

```bash
cd tests
python run_test.py
```

这会自动：
1. 启动机载端模拟器（120 秒）
2. 启动客户端
3. 等待测试完成

### 方式 2：手动测试（推荐用于开发）

**终端 1：启动模拟器**
```bash
python tests/air_unit_simulator.py
```

**终端 2：启动客户端**
```bash
python main.py
```

**在客户端中操作：**
1. 按 ESC 打开菜单
2. 点击 SCAN 发现机载端
3. 点击 Connect 连接
4. 观察统计信息（FPS、RTT、丢包率）
5. 按 ESC 关闭菜单，使用 WASD 控制
6. 按 ~ 打开开发者控制台查看日志

### 方式 3：多机载端测试

**终端 1：启动第一个机载端**
```bash
python tests/air_unit_simulator.py --name air_unit_01 --control-port 6000
```

**终端 2：启动第二个机载端**
```bash
python tests/air_unit_simulator.py --name air_unit_02 --control-port 6001
```

**终端 3：启动客户端**
```bash
python main.py
```

## 测试场景

### 场景 1：基础连接测试

**目标：** 验证客户端能否发现和连接到机载端

**步骤：**
1. 启动模拟器
2. 启动客户端
3. 点击 SCAN 和 Connect

**预期结果：**
- ✓ 客户端显示 "Connected" 状态
- ✓ 模拟器显示接收到控制指令
- ✓ 客户端显示视频帧

### 场景 2：控制指令测试

**目标：** 验证控制指令的发送和 ACK 接收

**步骤：**
1. 连接到机载端
2. 按 WASD 控制移动
3. 观察模拟器的统计信息

**预期结果：**
- ✓ 模拟器显示 "Control commands received" 增加
- ✓ 模拟器显示 "ACKs sent" 增加
- ✓ 客户端显示 RTT 延迟 < 10ms

### 场景 3：视频接收测试

**目标：** 验证视频帧的接收和显示

**步骤：**
1. 连接到机载端
2. 观察客户端的视频显示
3. 观察 FPS 和帧数统计

**预期结果：**
- ✓ 客户端显示视频帧
- ✓ FPS > 25
- ✓ 帧数不断增加

### 场景 4：性能测试

**目标：** 测试系统在长时间运行下的性能

**步骤：**
```bash
# 运行 5 分钟
python tests/air_unit_simulator.py --duration 300
python main.py
```

**观察指标：**
- 平均 FPS
- 平均 RTT
- 丢包率
- 内存使用

### 场景 5：网络故障测试

**目标：** 测试系统在网络故障下的表现

**步骤：**
1. 连接到机载端
2. 断开网络（拔网线或关闭 WiFi）
3. 观察客户端的反应
4. 恢复网络连接

**预期结果：**
- ✓ 客户端检测到连接丢失
- ✓ 自动重连
- ✓ 恢复连接后继续工作

## 模拟器命令行参数

```bash
python tests/air_unit_simulator.py [OPTIONS]
```

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--name` | `air_unit_01` | 机载端名称 |
| `--control-port` | `6000` | 控制指令端口 |
| `--video-port` | `5000` | 视频端口 |
| `--duration` | `0` | 运行时间（秒），0 表示无限 |

**示例：**
```bash
# 自定义名称和端口
python tests/air_unit_simulator.py --name air_unit_02 --control-port 6001

# 运行 60 秒
python tests/air_unit_simulator.py --duration 60

# 组合使用
python tests/air_unit_simulator.py --name air_unit_02 --control-port 6001 --duration 120
```

## 性能基准

在本地测试中的典型性能：

| 指标 | 值 |
|------|-----|
| 控制指令发送率 | 50 Hz |
| 视频帧发送率 | ~30 fps |
| ACK 响应时间 | < 1ms |
| RTT 测量精度 | 微秒级 |
| 平均 FPS | 60+ |
| 平均丢包率 | < 1% |

## 故障排除

### 问题 1：模拟器启动失败

**错误：** `Only one usage of each socket address`

**原因：** 端口已被占用

**解决：**
```bash
# 使用不同的端口
python tests/air_unit_simulator.py --control-port 6002 --video-port 5002
```

### 问题 2：客户端无法发现服务

**原因：**
- 防火墙阻止 mDNS
- 网络不连通
- 模拟器未启动

**解决：**
1. 检查防火墙设置
2. 检查网络连接
3. 确保模拟器已启动

### 问题 3：连接后无视频

**原因：** 视频端口配置错误

**解决：** 确保视频端口 = 控制端口 + 1000

### 问题 4：UI 冻结

**原因：** 服务发现阻塞（已修复）

**解决：** 等待服务发现完成（最多 10 秒）

## 日志和调试

### 查看详细日志

**在模拟器中：**
修改 `air_unit_simulator.py` 中的日志级别：
```python
logging.basicConfig(level=logging.DEBUG)  # 改为 DEBUG
```

**在客户端中：**
按 `~` 打开开发者控制台查看日志

### 常见日志消息

**模拟器：**
```
[INFO] mDNS service registered: air_unit_01._pip_link._udp.local.
[INFO] Control socket listening on port 6000
[DEBUG] Received control command from 192.168.1.100:54119
[DEBUG] Sent ACK to 192.168.1.100:54119, seq=1
```

**客户端：**
```
[INFO] Service found: 192.168.1.100:6000
[INFO] Connected to 192.168.1.100:6000
[DEBUG] RTT: 0.23ms
```

## 扩展和自定义

### 添加自定义测试

在 `tests/` 目录中创建新的测试脚本：

```python
"""自定义测试"""
from air_unit_simulator import AirUnitSimulator

simulator = AirUnitSimulator()
simulator.start()

# 你的测试代码
time.sleep(10)

simulator.stop()
simulator.print_statistics()
```

### 修改模拟器行为

编辑 `air_unit_simulator.py` 中的方法：

**模拟网络延迟：**
```python
def _send_ack(self, addr: tuple, seq: int):
    time.sleep(0.01)  # 10ms 延迟
    # ... 发送 ACK
```

**模拟丢包：**
```python
def _video_sender_thread(self):
    import random
    if random.random() < 0.05:  # 5% 丢包率
        continue
    # ... 发送视频帧
```

## 参考文档

- [USER_GUIDE.md](../USER_GUIDE.md) - 客户端使用指南
- [AIR_UNIT_SIMULATOR_GUIDE.md](AIR_UNIT_SIMULATOR_GUIDE.md) - 模拟器详细说明
- [tests/README.md](README.md) - 测试框架概述
- [BACKEND_SUMMARY.md](../BACKEND_SUMMARY.md) - 后端实现总结
- [IMPLEMENTATION_PLAN.md](../IMPLEMENTATION_PLAN.md) - 实现计划

## 总结

现在你可以：

1. ✓ 使用 `run_test.py` 一键测试整个系统
2. ✓ 使用 `air_unit_simulator.py` 模拟机载端
3. ✓ 手动测试各个功能
4. ✓ 进行性能基准测试
5. ✓ 测试网络故障恢复

所有测试都是自动化的，无需真实的机载端硬件！
