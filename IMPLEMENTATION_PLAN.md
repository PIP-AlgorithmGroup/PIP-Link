# 客户端后端实现计划

## 阶段 1：协议与基础通信

### 1.1 Protocol 类（消息编解码）
**目标**：实现消息的序列化/反序列化和 CRC 校验

**文件**：`network/protocol.py`

**功能**：
- 消息头编码/解码（Magic、Version、MsgType、Seq）
- 时间戳编码/解码（t1、t2、t3）
- Payload 编码/解码
- CRC32 校验

**测试方法**：
```bash
# 单元测试
python -m pytest network/test_protocol.py -v

# 测试内容：
# 1. 控制指令消息编解码
# 2. ACK 消息编解码
# 3. CRC 校验正确性
# 4. 时间戳精度（浮点数）
```

**成功标准**：
- ✓ 所有消息类型编解码正确
- ✓ CRC 校验无误
- ✓ 时间戳精度 ±1μs

---

### 1.2 LatencyCalculator 类（四时间戳延迟计算）
**目标**：实现高精度延迟测量

**文件**：`logic/latency_calculator.py`

**功能**：
- 记录发送时间（t1）
- 记录接收 ACK 时间（t4）
- 计算 RTT、时钟偏移、单向延迟
- 异常值过滤（3σ 规则）
- 超时检测（5s 未回复清理）

**测试方法**：
```bash
# 单元测试
python -m pytest logic/test_latency_calculator.py -v

# 测试内容：
# 1. 正常延迟计算
# 2. 异常值过滤
# 3. 超时清理
# 4. 统计精度
```

**成功标准**：
- ✓ RTT 计算正确
- ✓ 异常值被正确过滤
- ✓ 统计数据准确

---

### 1.3 UDP 基础收发
**目标**：实现 UDP Socket 的基本收发

**文件**：`network/udp_socket.py`

**功能**：
- UDP Socket 创建/绑定/关闭
- 非阻塞接收
- 异常处理

**测试方法**：
```bash
# 本地回环测试
python network/test_udp_loopback.py

# 测试内容：
# 1. 发送 100 个包，接收 100 个包
# 2. 验证数据完整性
# 3. 验证非阻塞行为
```

**成功标准**：
- ✓ 100% 包接收率（本地回环）
- ✓ 数据无损
- ✓ 非阻塞正常工作

---

## 阶段 2：网络线程框架

### 2.1 ServiceDiscovery（mDNS 发现）
**目标**：实现 mDNS 服务发现

**文件**：`network/service_discovery.py`

**功能**：
- 查询 mDNS 服务
- 获取 IP、video_port、control_port
- 回调给 SessionManager

**测试方法**：
```bash
# 需要机载端运行
# 在机载端（Ubuntu 22.04）运行：
ros2 run video_transmitter video_transmitter_node

# 在客户端（Windows）运行：
python network/test_service_discovery.py --service-name air_unit_01

# 测试内容：
# 1. 发现机载端服务
# 2. 获取正确的 IP 和端口
# 3. 超时处理
```

**成功标准**：
- ✓ 成功发现机载端
- ✓ 获取正确的 IP 和端口
- ✓ 超时后正确返回

---

### 2.2 ControlSender（TX 线程）
**目标**：实现控制指令发送线程

**文件**：`network/control_sender.py`

**功能**：
- 从命令队列读取指令
- 打包消息（含 t1 时间戳）
- UDP 发送
- 等待 ACK（100ms 超时）
- 超时重传（最多 3 次）

**测试方法**：
```bash
# 需要机载端运行
# 在机载端（Ubuntu 22.04）运行：
ros2 run video_transmitter video_transmitter_node

# 在客户端（Windows）运行：
python network/test_control_sender.py \
  --air-unit-ip 192.168.1.23 \
  --control-port 6000

# 测试内容：
# 1. 发送 10 个控制指令
# 2. 验证 ACK 接收
# 3. 验证延迟计算
# 4. 验证重传机制

# 在机载端查看接收：
ros2 topic echo /video_transmitter/control_cmd
```

**成功标准**：
- ✓ 所有指令成功发送
- ✓ ACK 正确接收
- ✓ 延迟计算准确（±10ms）
- ✓ 重传机制正常

---

### 2.3 VideoReceiver（RX 线程）
**目标**：实现视频接收线程

**文件**：`network/video_receiver.py`

**功能**：
- UDP 接收分片包
- 按 FrameID 缓冲
- 重组完整帧
- 放入 RenderQueue
- 发送 ACK

**测试方法**：
```bash
# 需要机载端运行
# 在机载端（Ubuntu 22.04）运行：
ros2 run video_transmitter video_transmitter_node

# 在客户端（Windows）运行：
python network/test_video_receiver.py \
  --air-unit-ip 192.168.1.23 \
  --video-port 5000 \
  --duration 10

# 测试内容：
# 1. 接收 10 秒视频
# 2. 统计接收帧数
# 3. 统计丢包率
# 4. 验证 ACK 发送

# 输出示例：
# Received: 300 frames
# Packet loss: 0.5%
# Average frame size: 50KB
```

**成功标准**：
- ✓ 接收帧数 > 0
- ✓ 丢包率 < 5%（局域网）
- ✓ ACK 正确发送

---

### 2.4 HeartbeatManager（心跳线程）
**目标**：实现心跳检测

**文件**：`network/heartbeat_manager.py`

**功能**：
- 定期发送心跳包（100ms）
- 监测连接状态
- 超时检测（3 次超时触发重连）

**测试方法**：
```bash
# 需要机载端运行
# 在客户端（Windows）运行：
python network/test_heartbeat_manager.py \
  --air-unit-ip 192.168.1.23 \
  --control-port 6000 \
  --duration 30

# 测试内容：
# 1. 发送 300 个心跳（30秒）
# 2. 验证 ACK 接收率
# 3. 模拟网络中断，验证重连

# 输出示例：
# Heartbeat sent: 300
# ACK received: 298
# Timeout count: 0
```

**成功标准**：
- ✓ ACK 接收率 > 95%
- ✓ 超时检测正常
- ✓ 重连机制正常

---

## 阶段 3：业务逻辑层

### 3.1 SessionManager（连接状态机）
**目标**：管理连接生命周期

**文件**：`logic/session_manager.py`

**功能**：
- 状态机：Idle → Discovering → Connecting → Connected
- 启动/停止网络线程
- 处理连接事件
- 自动重连

**测试方法**：
```bash
# 集成测试
python logic/test_session_manager.py

# 测试内容：
# 1. 正常连接流程
# 2. 断网重连
# 3. 用户主动断开
# 4. 状态转换正确性
```

**成功标准**：
- ✓ 状态转换正确
- ✓ 重连机制正常
- ✓ 事件回调正确

---

### 3.2 InputMapper（输入映射）
**目标**：将键盘输入映射到控制指令

**文件**：`logic/input_mapper.py`

**功能**：
- 使用 keyboard_encoder.py 获取键盘状态
- 映射到控制指令（forward、turn、action 等）
- 放入命令队列

**测试方法**：
```bash
# 单元测试
python logic/test_input_mapper.py

# 测试内容：
# 1. W 键 → forward=1.0
# 2. S 键 → forward=-1.0
# 3. A 键 → turn=-1.0
# 4. D 键 → turn=1.0
# 5. 多键同时按下
```

**成功标准**：
- ✓ 所有按键映射正确
- ✓ 多键同时按下正确
- ✓ 命令队列正确填充

---

### 3.3 StatusMonitor（统计监控）
**目标**：实时统计 FPS、延迟、丢包率

**文件**：`logic/status_monitor.py`

**功能**：
- 统计渲染 FPS
- 统计网络延迟（RTT、单向延迟）
- 统计丢包率
- 提供实时数据给 UI

**测试方法**：
```bash
# 集成测试
python logic/test_status_monitor.py

# 测试内容：
# 1. FPS 统计准确性
# 2. 延迟统计准确性
# 3. 丢包率统计准确性
```

**成功标准**：
- ✓ FPS 统计误差 < 1%
- ✓ 延迟统计误差 < 5%
- ✓ 丢包率统计准确

---

## 阶段 4：UI 集成

### 4.1 主循环框架
**目标**：实现 Pygame 主循环

**文件**：`main.py`

**功能**：
- 初始化 Pygame
- 主事件循环
- 帧率控制（60fps）
- 优雅关闭

**测试方法**：
```bash
# 功能测试
python main.py

# 测试内容：
# 1. 窗口正常打开
# 2. 帧率稳定在 60fps
# 3. ESC 键关闭应用
```

**成功标准**：
- ✓ 窗口正常显示
- ✓ 帧率稳定
- ✓ 正常关闭

---

### 4.2 视频渲染
**目标**：实现 OpenGL 视频渲染

**文件**：`ui/renderer.py`

**功能**：
- 创建 OpenGL 纹理
- 渲染视频帧
- 全屏显示

**测试方法**：
```bash
# 功能测试（需要机载端运行）
python main.py

# 测试内容：
# 1. 视频正常显示
# 2. 帧率 > 25fps
# 3. 无画面撕裂
```

**成功标准**：
- ✓ 视频正常显示
- ✓ 帧率稳定
- ✓ 无明显延迟

---

### 4.3 ImGui UI
**目标**：实现 ImGui 菜单和状态栏

**文件**：`ui/imgui_ui.py`

**功能**：
- ESC 菜单
- 右下角状态栏（FPS、延迟、丢包率）
- 参数面板

**测试方法**：
```bash
# 功能测试
python main.py

# 测试内容：
# 1. ESC 打开/关闭菜单
# 2. 状态栏显示正确
# 3. 参数面板可交互
```

**成功标准**：
- ✓ UI 正常显示
- ✓ 交互正常
- ✓ 数据更新及时

---

## 阶段 5：优化与完善

### 5.1 FEC 解码
**目标**：实现 Reed-Solomon 前向纠错

**文件**：`network/fec_decoder.py`

**功能**：
- FEC 解码
- 丢失分片恢复

**测试方法**：
```bash
# 单元测试
python network/test_fec_decoder.py

# 测试内容：
# 1. 无丢包情况
# 2. 丢包 10% 恢复
# 3. 丢包 20% 恢复
```

**成功标准**：
- ✓ 丢包恢复率 > 95%

---

### 5.2 自适应码率
**目标**：根据网络状况自动调整码率

**文件**：`logic/adaptive_bitrate.py`

**功能**：
- 监测丢包率
- 自动调整码率
- 网络恢复后恢复质量

**测试方法**：
```bash
# 集成测试
python logic/test_adaptive_bitrate.py

# 测试内容：
# 1. 丢包率上升时降低码率
# 2. 丢包率下降时提升码率
```

**成功标准**：
- ✓ 自动调整正常
- ✓ 稳定性提高

---

## 测试环境

### 机载端（Ubuntu 22.04）
```bash
# 启动机载端
ros2 run video_transmitter video_transmitter_node

# 查看话题
ros2 topic list
ros2 topic echo /video_transmitter/control_cmd
```

### 客户端（Windows 11）
```bash
# 激活 conda 环境
conda activate PIP_Link

# 运行测试
python -m pytest network/test_protocol.py -v

# 运行应用
python main.py
```

### 网络诊断
```bash
# 在 Ubuntu 上查看 UDP 流量
sudo tcpdump -i eth0 -n udp port 5000 or udp port 6000

# 在 Windows 上查看 UDP 流量
netstat -an | findstr UDP
```

---

## 进度跟踪

| 阶段 | 功能 | 状态 | 完成度 |
|------|------|------|--------|
| 1 | Protocol | Completed | 100% ✓ |
| 1 | LatencyCalculator | Completed | 100% ✓ |
| 1 | UDP 基础 | Not Started | 0% |
| 2 | ServiceDiscovery | Not Started | 0% |
| 2 | ControlSender | Not Started | 0% |
| 2 | VideoReceiver | Not Started | 0% |
| 2 | HeartbeatManager | Not Started | 0% |
| 3 | SessionManager | Not Started | 0% |
| 3 | InputMapper | Not Started | 0% |
| 3 | StatusMonitor | Not Started | 0% |
| 4 | 主循环框架 | Not Started | 0% |
| 4 | 视频渲染 | Not Started | 0% |
| 4 | ImGui UI | Not Started | 0% |
| 5 | FEC 解码 | Not Started | 0% |
| 5 | 自适应码率 | Not Started | 0% |

---

## 关键注意事项

1. **跨平台兼容性**：所有代码需要支持 Windows、Linux、MacOS
2. **线程安全**：使用 `queue.Queue` 和 `threading.Lock`
3. **时间精度**：使用 `time.perf_counter()` 而非 `time.time()`
4. **错误处理**：所有网络操作需要异常处理
5. **日志记录**：使用 `logging` 模块记录关键事件
6. **测试优先**：每个功能先写测试，再实现代码
