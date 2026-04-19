# PIP-Link 测试脚本

本目录包含用于测试 PIP-Link 系统的各种脚本。

## 文件说明

### 1. air_unit_simulator.py
机载端 mDNS 模拟器，用于模拟真实的机载端服务。

**功能：**
- mDNS 服务注册（让客户端能发现）
- UDP 控制指令接收和 ACK 响应
- 模拟视频帧发送

**使用：**
```bash
# 基础使用
python air_unit_simulator.py

# 自定义配置
python air_unit_simulator.py --name air_unit_02 --control-port 6001 --duration 60

# 详细说明
python air_unit_simulator.py --help
```

详见 [AIR_UNIT_SIMULATOR_GUIDE.md](AIR_UNIT_SIMULATOR_GUIDE.md)

### 2. run_test.py
一键测试脚本，自动启动模拟器和客户端。

**使用：**
```bash
python run_test.py
```

**流程：**
1. 启动机载端模拟器（120 秒）
2. 启动客户端
3. 等待测试完成

### 3. test_integration_control.py
ControlSender 集成测试，测试控制指令发送和 ACK 接收。

**使用：**
```bash
python test_integration_control.py
```

### 4. test_backend_integration.py
完整后端系统集成测试。

**使用：**
```bash
python test_backend_integration.py
```

## 快速开始

### 方式 1：一键测试（推荐）

```bash
cd tests
python run_test.py
```

### 方式 2：手动测试

**终端 1：启动模拟器**
```bash
python air_unit_simulator.py
```

**终端 2：启动客户端**
```bash
cd ..
python main.py
```

**在客户端中：**
1. 按 ESC 打开菜单
2. 点击 SCAN 发现机载端
3. 点击 Connect 连接
4. 观察统计信息

### 方式 3：多机载端测试

**终端 1：启动第一个机载端**
```bash
python air_unit_simulator.py --name air_unit_01 --control-port 6000
```

**终端 2：启动第二个机载端**
```bash
python air_unit_simulator.py --name air_unit_02 --control-port 6001
```

**终端 3：启动客户端**
```bash
cd ..
python main.py
```

## 测试场景

### 场景 1：基础连接测试
验证客户端能否发现和连接到机载端。

**步骤：**
1. 启动模拟器
2. 启动客户端
3. 点击 SCAN 和 Connect
4. 验证连接成功

**预期结果：**
- 客户端显示 "Connected" 状态
- 模拟器显示接收到控制指令
- 客户端显示视频帧

### 场景 2：控制指令测试
验证控制指令的发送和 ACK 接收。

**步骤：**
1. 连接到机载端
2. 按 WASD 控制移动
3. 观察模拟器的统计信息

**预期结果：**
- 模拟器显示 "Control commands received" 增加
- 模拟器显示 "ACKs sent" 增加
- 客户端显示 RTT 延迟

### 场景 3：视频接收测试
验证视频帧的接收和显示。

**步骤：**
1. 连接到机载端
2. 观察客户端的视频显示
3. 观察 FPS 和帧数统计

**预期结果：**
- 客户端显示视频帧
- FPS > 25
- 帧数不断增加

### 场景 4：性能测试
测试系统在长时间运行下的性能。

**步骤：**
```bash
python air_unit_simulator.py --duration 300  # 5 分钟
python main.py
```

**观察指标：**
- 平均 FPS
- 平均 RTT
- 丢包率
- 内存使用

### 场景 5：网络故障测试
测试系统在网络故障下的表现。

**步骤：**
1. 连接到机载端
2. 断开网络（拔网线或关闭 WiFi）
3. 观察客户端的反应
4. 恢复网络连接

**预期结果：**
- 客户端检测到连接丢失
- 自动重连
- 恢复连接后继续工作

## 日志和调试

### 查看详细日志

修改脚本中的日志级别：

```python
logging.basicConfig(level=logging.DEBUG)  # 改为 DEBUG
```

### 开发者控制台

在客户端中按 `~` 打开开发者控制台查看日志。

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

### 问题：模拟器启动失败

**错误：** `Only one usage of each socket address`

**解决：** 使用不同的端口
```bash
python air_unit_simulator.py --control-port 6002 --video-port 5002
```

### 问题：客户端无法发现服务

**原因：** 防火墙或网络问题

**解决：**
1. 检查防火墙设置
2. 确保两个程序在同一网络
3. 检查 mDNS 是否启用

### 问题：连接后无视频

**原因：** 视频端口配置错误

**解决：** 确保视频端口 = 控制端口 + 1000

## 扩展

### 添加自定义测试

在 `tests/` 目录中创建新的测试脚本：

```python
"""自定义测试"""
from air_unit_simulator import AirUnitSimulator

simulator = AirUnitSimulator()
simulator.start()

# 你的测试代码

simulator.stop()
```

### 修改模拟器行为

编辑 `air_unit_simulator.py` 中的方法：
- `_send_ack()`: 修改 ACK 响应
- `_video_sender_thread()`: 修改视频发送
- `_control_receiver_thread()`: 修改控制处理

## 参考

- [USER_GUIDE.md](../USER_GUIDE.md) - 客户端使用指南
- [AIR_UNIT_SIMULATOR_GUIDE.md](AIR_UNIT_SIMULATOR_GUIDE.md) - 模拟器详细说明
- [BACKEND_SUMMARY.md](../BACKEND_SUMMARY.md) - 后端实现总结
- [IMPLEMENTATION_PLAN.md](../IMPLEMENTATION_PLAN.md) - 实现计划
