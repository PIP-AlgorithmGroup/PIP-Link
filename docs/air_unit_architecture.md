# 机载端（Air Unit）实现方案

## 1. 系统定位

机载端是ROS2生态中的独立网络传输节点，负责：

- 订阅指定ROS2话题获取图像帧
- 编码、分片、FEC处理
- 通过UDP发送图传到客户端
- 接收客户端的控制指令、参数修改
- 发布控制指令到指定ROS2话题
- 通过mDNS进行服务发现

**关键原则**：网络层完全独立于ROS2，不依赖DDS，使用独立线程处理网络I/O。

---

## 2. 整体架构

```
┌─────────────────────────────────────────────────────────────┐
│ ROS2 生态                                                    │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  [其他节点]  →  [指定Topic]  →  VideoTransmitterNode       │
│                                                              │
│  VideoTransmitterNode                                        │
│  ├─ 订阅：指定Topic（用户配置）                              │
│  ├─ 发布：控制Topic（用户配置）                              │
│  └─ 发布：参数Topic（用户配置）                              │
│                                                              │
└────────────────┬──────────────────────────────────────────┘
                 │
┌────────────────▼──────────────────────────────────────────┐
│ 网络层（独立线程）                                          │
├────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ TX 线程                                              │  │
│  │ - 编码 → 分片 → FEC → UDP发送                        │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                             │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ RX 线程                                              │  │
│  │ - UDP接收 → 解析 → 验证 → 发布ROS2话题              │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                             │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ mDNS 线程                                            │  │
│  │ - 广播服务 → 响应查询                                │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                             │
└────────────────┬──────────────────────────────────────────┘
                 │
        ┌────────▼────────┐
        │   UDP Socket    │
        │   mDNS Socket   │
        └────────┬────────┘
                 │
        ┌────────▼────────┐
        │   局域网        │
        └─────────────────┘
```

---

## 3. 模块设计

### 3.1 VideoTransmitterNode（ROS2节点）

**职责**：
- 管理ROS2订阅/发布
- 缓冲最新帧
- 启动/停止网络线程
- 参数热更新

**接口**：
```cpp
class VideoTransmitterNode : public rclcpp::Node {
public:
  explicit VideoTransmitterNode(const rclcpp::NodeOptions& options);
  ~VideoTransmitterNode();

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
```

**配置参数**：
```yaml
video_transmitter:
  ros__parameters:
    # ROS2话题配置
    input_topic: "/annotated_frame"      # 输入图像话题
    control_output_topic: "/control_cmd" # 控制指令输出话题
    param_output_topic: "/param_update"  # 参数修改输出话题
    
    # 网络配置
    video_port: 5000                     # UDP图传端口
    control_port: 6000                  # UDP控制端口
    mdns_name: "air_unit_01"             # mDNS服务名
    
    # 编码配置
    encoder: "h264"                      # h264/h265/mjpeg
    bitrate: 5000                        # 码率(kbps)
    target_fps: 30                       # 目标帧率
    
    # FEC配置
    fec_enabled: true                    # 是否启用FEC
    fec_redundancy: 0.2                  # 冗余度(0.0-1.0)
    fec_block_size: 16                   # FEC块大小
    
    # 调试参数
    debug.show_stats: false              # 显示统计信息
    debug.show_fps: false                # 显示FPS
```

### 3.2 VideoTransmitter（TX线程）

**职责**：
- 从ROS2话题缓冲区读取最新帧
- 编码处理
- 分片打包
- FEC编码
- UDP发送

**数据流**：
```
ROS2话题 → FrameBuffer(最新帧) → Encoder → Packetizer → FEC → UDPSender → 客户端
```

**关键组件**：

| 组件 | 职责 |
|------|------|
| FrameBuffer | 缓冲最新帧，避免阻塞ROS2 |
| Encoder | H.264/H.265/MJPEG编码 |
| Packetizer | 分片：[Header][FrameID][PacketID][Total][Payload] |
| FECEncoder | Reed-Solomon前向纠错 |
| UDPSender | UDP发送到客户端 |

**消息格式**：
```
┌─────────────┬──────────┬──────────┬──────────┬──────────┬─────────┐
│ FrameHeader │ FrameID  │ PacketID │ Total    │ Payload  │ CRC32   │
│ (4 bytes)   │ (4 bytes)│ (2 bytes)│ (2 bytes)│ (var)    │ (4 bytes)│
└─────────────┴──────────┴──────────┴──────────┴──────────┴─────────┘
```

### 3.3 ControlReceiver（RX线程）

**职责**：
- UDP接收控制包
- 消息解析与验证
- 发布到ROS2话题
- 发送ACK

**数据流**：
```
客户端 → UDPReceiver → MessageParser → Validator → ROS2Publisher → 其他节点
```

**关键组件**：

| 组件 | 职责 |
|------|------|
| UDPReceiver | UDP接收 |
| MessageParser | 解析消息头、负载 |
| Validator | CRC校验、消息类型验证 |
| ROS2Publisher | 发布到指定话题 |
| ACKSender | 发送ACK确认 |

**消息格式**：
```
┌──────────┬─────────┬──────────┬──────────┬──────────┬─────────┐
│ Magic    │ Version │ MsgType  │ Seq      │ Payload  │ CRC32   │
│ (2 bytes)│ (1 byte)│ (1 byte) │ (4 bytes)│ (var)    │ (4 bytes)│
└──────────┴─────────┴──────────┴──────────┴──────────┴─────────┘
```

**消息类型**：
- 0x01: 控制指令（ControlCommand）
- 0x02: 参数修改（ParamUpdate）
- 0x03: 参数查询（ParamQuery）
- 0x04: 心跳（Heartbeat）

### 3.4 ServiceDiscovery（mDNS线程）

**职责**：
- 广播节点信息
- 响应客户端查询
- 定期更新服务状态

**广播内容**：
```json
{
  "id": "air_unit_01",
  "role": "air_unit",
  "video_port": 5000,
  "control_port": 6000,
  "ip": "192.168.1.23",
  "status": "ready"
}
```

---

## 4. 线程模型

```
主线程（ROS2 spin）
├─ 处理ROS2回调
├─ 订阅输入话题
└─ 发布输出话题

TX线程（VideoTransmitter）
├─ 读取FrameBuffer
├─ 编码 → 分片 → FEC
└─ UDP发送

RX线程（ControlReceiver）
├─ UDP接收
├─ 解析 → 验证
└─ 发布ROS2话题

mDNS线程（ServiceDiscovery）
├─ 定期广播
└─ 响应查询
```

**线程安全**：
- FrameBuffer：`std::mutex` + `std::lock_guard`
- 消息队列：`std::queue` + `std::mutex`
- 原子操作：`std::atomic<bool>` 用于停止信号

---

## 5. 与ROS2的集成

### 5.1 话题订阅

```cpp
// 用户配置input_topic，节点自动订阅
frame_sub_ = node_->create_subscription<sensor_msgs::msg::Image>(
  input_topic,
  rclcpp::SensorDataQoS(),
  [this](const sensor_msgs::msg::Image::SharedPtr msg) {
    // 缓冲最新帧到FrameBuffer
    frame_buffer_->update(msg);
  });
```

### 5.2 话题发布

```cpp
// 发布控制指令到用户指定话题
control_pub_ = node_->create_publisher<pip_vision_interfaces::msg::ControlCommand>(
  control_output_topic, 10);

// 发布参数修改到用户指定话题
param_pub_ = node_->create_publisher<pip_vision_interfaces::msg::ParamUpdate>(
  param_output_topic, 10);
```

### 5.3 参数管理

```cpp
// 声明参数
node_->declare_parameter("video_port", 5000);
node_->declare_parameter("encoder", "h264");
node_->declare_parameter("fec_enabled", true);

// 参数热更新回调
param_callback_handle_ = node_->add_on_set_parameters_callback(
  [this](const std::vector<rclcpp::Parameter>& params) {
    for (const auto& param : params) {
      if (param.get_name() == "encoder") {
        // 更新编码器
      }
    }
    return rcl_interfaces::msg::SetParametersResult{true, ""};
  });
```

---

## 6. 数据流详解

### 6.1 图传流（TX）

```
1. ROS2话题发布帧
   ↓
2. VideoTransmitterNode订阅，缓冲到FrameBuffer
   ↓
3. TX线程读取FrameBuffer最新帧
   ↓
4. Encoder编码（H.264/H.265/MJPEG）
   ↓
5. Packetizer分片
   ├─ 计算分片数量
   ├─ 添加Header（FrameID、PacketID、Total）
   └─ 添加Payload
   ↓
6. FECEncoder编码（可选）
   ├─ 计算冗余包
   └─ 混合原始包+冗余包
   ↓
7. UDPSender发送到客户端
   ├─ 遍历所有分片
   └─ 逐个发送UDP包
   ↓
8. 客户端接收
```

**性能考虑**：
- FrameBuffer只保留最新帧，丢弃过期帧
- 编码在独立线程，不阻塞ROS2
- FEC冗余度可配置，权衡延迟vs可靠性

### 6.2 控制流（RX）

```
1. 客户端发送控制包（UDP）
   ↓
2. RX线程UDPReceiver接收
   ↓
3. MessageParser解析
   ├─ 读取Header
   ├─ 验证Magic、Version
   └─ 提取Payload
   ↓
4. Validator验证
   ├─ CRC32校验
   ├─ 消息类型检查
   └─ 序列号检查
   ↓
5. 根据消息类型处理
   ├─ ControlCommand → 发布到control_output_topic
   ├─ ParamUpdate → 发布到param_output_topic
   └─ ParamQuery → 查询并回复
   ↓
6. ACKSender发送ACK到客户端
   ↓
7. 其他ROS2节点订阅话题，处理控制指令
```

---

## 7. 配置示例

### 7.1 YAML配置文件

```yaml
video_transmitter:
  ros__parameters:
    # 话题配置
    input_topic: "/face_visualizer/annotated_frame"
    control_output_topic: "/video_transmitter/control_cmd"
    param_output_topic: "/video_transmitter/param_update"
    
    # 网络配置
    video_port: 5000
    control_port: 6000
    mdns_name: "pip_vision_air_01"
    
    # 编码配置
    encoder: "h264"
    bitrate: 5000
    target_fps: 30
    
    # FEC配置
    fec_enabled: true
    fec_redundancy: 0.2
    fec_block_size: 16
    
    # 调试
    debug.show_stats: false
    debug.show_fps: false
```

### 7.2 启动命令

```bash
# 方式1：直接运行
ros2 run video_transmitter video_transmitter_node

# 方式2：使用配置文件
ros2 run video_transmitter video_transmitter_node \
  --ros-args --params-file config/video_transmitter.yaml

# 方式3：命令行参数
ros2 run video_transmitter video_transmitter_node \
  --ros-args \
  -p input_topic:=/annotated_frame \
  -p video_port:=5000 \
  -p encoder:=h264
```

---

## 8. 错误处理与恢复

### 8.1 网络故障

| 故障 | 处理 |
|------|------|
| UDP发送失败 | 记录日志，继续发送下一帧 |
| 客户端断连 | 继续编码发送，等待重连 |
| 端口被占用 | 启动失败，日志提示 |

### 8.2 编码故障

| 故障 | 处理 |
|------|------|
| 编码超时 | 跳过当前帧，处理下一帧 |
| 编码器初始化失败 | 启动失败，日志提示 |
| 内存不足 | 降低分辨率或码率 |

### 8.3 ROS2故障

| 故障 | 处理 |
|------|------|
| 话题订阅失败 | 启动失败，日志提示 |
| 话题发布失败 | 记录日志，继续运行 |
| 参数更新失败 | 保持旧值，日志提示 |

---

## 9. 性能指标

### 9.1 目标指标

| 指标 | 目标值 |
|------|--------|
| 端到端延迟 | < 100ms |
| 帧率 | 30 FPS |
| 丢包率 | < 1% |
| CPU占用 | < 30% |
| 内存占用 | < 200MB |

### 9.2 监测方式

```cpp
// FPS统计
tools::runtime::FpsCounter fps_counter(100);
while (running) {
  // ... 处理帧
  fps_counter.tick(1);
  if (show_fps_) {
    RCLCPP_INFO(logger, "TX FPS=%.1f", fps_counter.fps());
  }
}

// 丢包率统计
packet_loss_rate = (packets_lost / packets_sent) * 100;

// 延迟测量
auto send_time = std::chrono::high_resolution_clock::now();
// ... 发送
auto ack_time = std::chrono::high_resolution_clock::now();
auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(ack_time - send_time);
```

---

## 10. 扩展点

### 10.1 编码器扩展

支持插拔式编码器：
- H.264（默认）
- H.265（高压缩）
- MJPEG（极低延迟）
- RAW（无压缩）

### 10.2 FEC算法扩展

支持多种FEC算法：
- Reed-Solomon（默认）
- LDPC
- Turbo码

### 10.3 传输层扩展

支持多种传输协议：
- UDP（默认）
- QUIC（更平滑）
- TCP（可靠但延迟高）

---

## 11. 依赖项

- ROS2 Humble
- C++20
- OpenCV 4（编码）
- FFmpeg（可选，高级编码）
- zeroconf/mDNS库（服务发现）
- Eigen3（矩阵运算）

---

## 12. 文件结构

```
src/video_transmitter/
├── CMakeLists.txt
├── package.xml
├── src/
│   ├── main.cpp
│   └── video_transmitter_node.cpp
├── include/
│   └── video_transmitter/
│       └── video_transmitter_node.hpp
└── config/
    └── video_transmitter.yaml

tools/network/
├── include/
│   ├── video_tx.hpp
│   ├── control_rx.hpp
│   ├── service_discovery.hpp
│   ├── frame_buffer.hpp
│   ├── encoder.hpp
│   ├── packetizer.hpp
│   └── fec_encoder.hpp
└── src/
    ├── video_tx.cpp
    ├── control_rx.cpp
    ├── service_discovery.cpp
    └── ...

pip_vision_interfaces/msg/
├── ControlCommand.msg
├── ParamUpdate.msg
└── StatusReport.msg
```

