# remote_link 节点设计文档

> 基于 `air_unit_server.py` 逆向分析，用于指导 rclcpp 实现。
> 技术选型：avahi C API (mDNS)、cm256 (FEC，K=1 XOR + K>1 全支持)、FFmpeg libavcodec (H.264)
> 两节点部署于同一 rclcpp_components container，Image topic 启用 Loaned Message 零拷贝。
> **Python 客户端 `network/fec.py` 需同步替换为 cm256 解码**（见 §7.6）。

---

## 1. 协议逆向分析

### 1.1 传输层

| 通道     | 协议 | 默认端口 | 方向       |
|--------|------|------|----------|
| 控制通道  | UDP | 6000 | 地面站 → 机载 |
| 视频通道  | UDP | 5000 | 机载 → 地面站 |
| 服务发现  | mDNS | multicast | 双向 |

mDNS 服务类型：`_pip-link._udp.local.`  
Properties：`video_port`, `control_port`, `version="1.0"`, `device_type="air_unit"`

---

### 1.2 控制通道报文格式（机载端接收）

**通用消息头（9 字节，little-endian）：**

```
[Magic:2=0xABCD][Version:1=0x01][MsgType:1][Reserved:1][Seq:4]
```

**CRC32 尾（4 字节）：**
```
[CRC32:4]  zlib.crc32(msg[:-4]) & 0xFFFFFFFF
```

**最小合法消息长度：13 字节**（头 9 + CRC 4）

#### 消息类型枚举

| 值    | 名称              | 方向             |
|------|-----------------|----------------|
| 0x01 | CONTROL_COMMAND | 地面站 → 机载      |
| 0x02 | PARAM_UPDATE    | 双向（机载回复用同值）  |
| 0x03 | PARAM_QUERY     | 地面站 → 机载      |
| 0x04 | HEARTBEAT       | 地面站 → 机载      |
| 0x05 | ACK             | 机载 → 地面站      |
| 0x06 | VIDEO_ACK       | 地面站 → 机载（视频通道）|
| 0x07 | VIDEO_NACK      | 地面站 → 机载（视频通道）|

---

#### CONTROL_COMMAND (0x01) — 37 字节

```
[Header:9][t1:8 double][KeyboardState:10][MouseDX:2 int16][MouseDY:2 int16]
[MouseButtons:1 uint8][ScrollDelta:1 int8][CRC32:4]
```

| 字段             | 说明                                       |
|----------------|------------------------------------------|
| `t1`           | 发送时刻（perf_counter），供 RTT 计算，转发到 ROS msg |
| `KeyboardState`| 10 字节位图，bit_index = byte_idx×8 + bit    |
| `MouseDX/DY`   | 帧间累积位移（int16，px），需转换为速度写入 msg           |
| `MouseButtons` | bit0=左键, bit1=右键, bit2=中键, bit3=鼠标4, bit4=鼠标5 |
| `ScrollDelta`  | int8，正值向上，逐帧触发 scroll_up/scroll_down    |

**MouseDX/DY → mouse_vx/vy 转换：**  
利用 CONTROL_COMMAND 包自带的 `t1` 字段（发送端 `perf_counter` 时刻）动态计算 dt，避免固定 50 Hz 假设引入误差：
```
dt   = t1_current - t1_prev          (发送端时钟差，单位 s)
dt   = clamp(dt, 0.001, 0.200)       (防零除；>200ms 视为重连，dt 回退到 0.02)
vx   = mouse_dx / dt                 (px/s)
vy   = mouse_dy / dt
```
`t1_prev` 在 `RemoteLinkNode` 中作为成员变量保存，首包时无 prev，输出 vx=vy=0。
`pressed_keys_count` = popcount(keyboard_state 10 字节中所有置 1 的 bit)。

#### ACK (0x05) — 29 字节（机载发出）

```
[Header:9][t2:8 double][t3:8 double][CRC32:4]
```

- Seq 字段沿用被确认消息的 Seq
- `t2/t3` 为机载侧收/发时刻，地面站用于四时间戳 RTT 计算

#### PARAM_UPDATE (0x02)

```
[Header:9][t1:8 double][JSON payload (UTF-8)][CRC32:4]
```

JSON 键值（机载端需支持）：

| 键                | 类型    | 说明           | ROS2 参数名              |
|-----------------|-------|--------------|------------------------|
| `bitrate`       | int   | kbps         | `target_bitrate_kbps`  |
| `target_fps`    | int   | 目标帧率         | `target_fps`           |
| `encoder`       | str   | "h264"/"jpeg"| `encoder`              |
| `fec_enabled`   | bool  | FEC 开关        | `fec_enabled`          |
| `fec_redundancy`| float | FEC 冗余比      | `fec_redundancy`       |
| `brightness`    | int   | -100~100     | `brightness`           |
| `contrast`      | int   | -100~100     | `contrast`             |
| `sharpness`     | int   | 0~100        | `sharpness`            |
| `denoise`       | int   | 0~100        | `denoise`              |

机载端收到后需：① 更新 ROS2 Parameter Server → ② 回复 ACK → ③ 以 PARAM_UPDATE (0x02) 回发当前完整参数集。

#### HEARTBEAT (0x04)

```
[Header:9][t1:8 double][CRC32:4]
```

机载端回复 ACK。5 秒内未收到任何消息则认为客户端断连。

---

### 1.3 键盘位图（10 字节 = 80 bits，使用 0~70）

```
bit_index = byte_index * 8 + bit_offset  (bit_offset 0 = LSB)
```

| bit   | key    | bit   | key    | bit   | key    |
|-------|--------|-------|--------|-------|--------|
| 0     | ESC    | 27    | TAB    | 54    | LSHIFT |
| 1–12  | F1-F12 | 28-37 | Q-P    | 55-64 | Z-/    |
| 13    | \`     | 38    | [      | 65    | RSHIFT |
| 14-25 | 1-=    | 39    | ]      | 66    | LCTRL  |
| 26    | BS     | 40    | \\     | 67    | LALT   |
|       |        | 41    | CAPS   | 68    | SPACE  |
|       |        | 42-52 | A-;    | 69    | RALT   |
|       |        | 53    | ENTER  | 70    | RCTRL  |

完整映射见 `air_unit_server.py:196–206`（`BIT_TO_KEY`）。

---

### 1.4 视频通道报文格式（机载端发送）

#### 握手

客户端向机载视频端口发裸字节 `b"REGISTER"` → 机载记录 `client_video_addr` 开始推流。  
客户端超过 3 秒无新帧时重发 REGISTER。

#### 数据包头（20 字节，little-endian）

```
[frame_id:4 uint32][total_chunks:2 uint16][chunk_idx:2 uint16]
[chunk_size:4 uint32][fec_flag:1 uint8][orig_chunks:2 uint16]
[codec_flag:1 uint8][encode_ms:4 float32]
```

| 字段             | 说明                              |
|----------------|----------------------------------|
| `frame_id`     | 单调递增帧序号（从 1 开始）           |
| `total_chunks` | 含 FEC parity 的总分片数           |
| `orig_chunks`  | 纯数据分片数（不含 parity）          |
| `fec_flag`     | 0=数据分片，1=parity 分片          |
| `codec_flag`   | 0=JPEG，1=H.264                   |
| `encode_ms`    | 编码耗时（参考用）                   |
| `chunk_size`   | 紧跟头之后的 payload 字节数         |

**分片大小：CHUNK_SIZE = 60 000 字节**

#### FEC — cm256（全路径）

两端统一使用 cm256（Cauchy Matrix GF(2^8)）：
- K=1：退化为 XOR parity（cm256 仍可处理，也可手写 XOR 更快）
- K>1：cm256 encode/decode，支持任意 K 个 parity chunk 恢复任意 K 个缺失数据 chunk

**K 值计算**（与 Python 原版一致）：
```
K = max(1, ceil(N × redundancy))
```

**编码约束**：cm256 要求所有 block 等长，需将各 chunk 零填充到 `max_chunk_size`，接收端按 `chunk_size` 字段裁剪。cm256 最大支持 `N+K ≤ 256`。

**Python 客户端需同步替换**：`network/fec.py` 中 `FECEncoder`/`FECDecoder` 改用 cm256 Python 绑定（见 §7.6）。

#### VIDEO_ACK / VIDEO_NACK

- `VIDEO_ACK (0x06)`：Header 的 Seq 字段填 frame_id，无额外 payload
- `VIDEO_NACK (0x07)`：`[Header:9][NumChunks:2][ChunkIdx×N:2N][CRC32:4]`，机载端从帧缓存重传

---

## 2. 节点架构设计

### 2.1 节点拆分

`remote_link` 只负责通信，测试图像由独立节点提供。

```
┌────────────────────────────────────────┐
│         test_frame_publisher           │
│  (测试用，生成并发布测试图像)              │
│  Publisher<Image>: borrow_loaned_msg() │
└────────────────┬───────────────────────┘
                 │ /sending_frame
                 │ sensor_msgs/Image
                 │ [零拷贝 Loaned Message]
                 ▼
┌────────────────────────────────────────┐
│              remote_link               │
│  Subscriber<Image>: unique_ptr 回调    │
│                                        │
│  UDP 控制通道 ◄── 地面站 PIP-Link       │
│  UDP 视频通道 ──► 地面站 PIP-Link       │
└────────────────┬───────────────────────┘
                 │ /remote_command
                 │ pip_vision_interfaces/RemoteCommand
                 ▼
            下游节点（飞控等）
```

---

### 2.2 `remote_link` 内部模块划分

```
remote_link/
├── CMakeLists.txt
├── package.xml
├── include/remote_link/
│   ├── protocol_codec.hpp      # 纯协议编解码，无 ROS/socket 依赖
│   ├── mdns_service.hpp        # mDNS 注册（avahi C API 封装）
│   ├── control_receiver.hpp    # UDP 控制通道接收线程
│   ├── video_sender.hpp        # 视频分片 + FEC(K=1 XOR) + UDP 发送
│   ├── frame_encoder.hpp       # JPEG 自适应编码 + H.264（FFmpeg）
│   └── remote_link_node.hpp    # ROS2 节点主类
└── src/
    ├── protocol_codec.cpp
    ├── mdns_service.cpp
    ├── control_receiver.cpp
    ├── video_sender.cpp
    ├── frame_encoder.cpp
    └── remote_link_node.cpp
```

---

### 2.3 `test_frame_publisher` 内部模块划分

```
test_frame_publisher/
├── include/test_frame_publisher/
│   ├── test_frame_generator.hpp     # 测试卡生成（纯 OpenCV，无 ROS）
│   └── test_frame_publisher_node.hpp
└── src/
    ├── test_frame_generator.cpp
    └── test_frame_publisher_node.cpp
```

---

## 3. 接口定义

### 3.1 RemoteCommand.msg（最终定义）

路径：`src/pip_vision_interfaces/msg/RemoteCommand.msg`

```
# PIP-Link 地面站控制指令
std_msgs/Header header

# 客户端信息
string client_ip

# 传输元数据（用于 ACK 匹配和 RTT 计算）
uint32 seq
float64 t1

# READY 状态（F5 切换，false 时地面站发送全零）
bool is_ready

# 鼠标速度 (像素/秒，由 dx/dy ÷ dt 动态计算，dt=相邻包 t1 之差)
float32 mouse_vx
float32 mouse_vy

# 鼠标按键状态
bool mouse_left
bool mouse_right
bool mouse_middle
bool mouse4
bool mouse5
bool scroll_up
bool scroll_down

# 键盘状态 (10字节位图，每位代表一个键)
uint8[10] keyboard_state

# 按下的键数量（popcount of keyboard_state）
uint8 pressed_keys_count
```

**is_ready 语义**：地面站 F5 切换。`is_ready=false` 时，地面站发送全零键盘/鼠标（但仍发包维持心跳）。机载端收到包后无论 is_ready 值均发布到 `/remote_command`，由下游节点决定是否响应。

**mouse_vx/vy 转换规则**：
```cpp
// on_command() 内，RemoteLinkNode 成员：double prev_t1_{-1.0};
constexpr double DT_MIN = 0.001;   // 1ms，防零除
constexpr double DT_MAX = 0.200;   // 200ms，超过视为重连

if (prev_t1_ < 0.0) {
    // 首包：无法计算 dt，速度置零
    msg->mouse_vx = 0.0f;
    msg->mouse_vy = 0.0f;
} else {
    double dt = t1 - prev_t1_;
    if (dt > DT_MAX) {
        // 间隔过长（断连后重连），速度置零，避免跳变
        msg->mouse_vx = 0.0f;
        msg->mouse_vy = 0.0f;
    } else {
        dt = std::max(dt, DT_MIN);
        msg->mouse_vx = static_cast<float>(mouse_dx) / static_cast<float>(dt);
        msg->mouse_vy = static_cast<float>(mouse_dy) / static_cast<float>(dt);
    }
}
prev_t1_ = t1;
```

**scroll 转换规则**：
```cpp
msg->scroll_up   = (scroll_delta > 0);
msg->scroll_down = (scroll_delta < 0);
```

**pressed_keys_count 计算**：
```cpp
uint8_t count = 0;
for (int i = 0; i < 10; ++i) count += __builtin_popcount(keyboard_state[i]);
msg->pressed_keys_count = count;
```

---

### 3.2 节点参数（remote_link）

所有参数均在 ROS2 Parameter Server 中声明，支持运行时通过 `ros2 param set` 或 UDP PARAM_UPDATE 包动态修改（见 §6.4）。

| 参数名                   | 类型    | 默认值          | 说明                        |
|------------------------|-------|--------------|---------------------------|
| `air_unit_name`        | str   | `air_unit_01`| mDNS 服务名                  |
| `control_port`         | int   | `6000`       | 控制 UDP 端口                  |
| `video_port`           | int   | `5000`       | 视频 UDP 端口                  |
| `target_fps`           | int   | `30`         | 推流帧率                       |
| `target_bitrate_kbps`  | int   | `2000`       | 目标码率                       |
| `jpeg_quality`         | int   | `80`         | JPEG 初始质量（15~85）          |
| `encoder`              | str   | `"jpeg"`     | `"jpeg"` 或 `"h264"`       |
| `fec_enabled`          | bool  | `false`      | FEC 开关（仅支持 K=1 XOR）     |
| `fec_redundancy`       | double| `0.2`        | FEC 冗余比（实际只用到是否 ≥ 1/N）|
| `client_timeout_s`     | double| `5.0`        | 客户端无消息超时时间               |

### 3.3 节点参数（test_frame_publisher）

| 参数名           | 类型  | 默认值              | 说明       |
|---------------|-----|-----------------|----------|
| `width`       | int | `1280`          | 图像宽度     |
| `height`      | int | `720`           | 图像高度     |
| `fps`         | int | `30`            | 发布帧率     |
| `frame_topic` | str | `/sending_frame`| 发布 topic |

---

## 4. 线程模型

### 4.1 remote_link 线程设计

```
主线程（rclcpp::spin / MultiThreadedExecutor）
 ├── /sending_frame 订阅回调（Loaned Message）
 │     └── push_frame() 到 VideoSender → 条件变量通知，不阻塞
 ├── Parameter 回调（on_parameter_event）
 │     └── 验证 → 更新 VideoSender config → 可选发 UDP PARAM_UPDATE 回地面站
 └── Watchdog Timer（2s）
       └── 检查 last_client_time → 超时则 clear_client_addr()

VideoSender 线程（std::thread，优先级可提升）
 ├── 等待 frame_cv_（条件变量）
 ├── 取最新帧（旧帧丢弃）
 ├── FrameEncoder::encode()（JPEG 或 H.264）
 ├── 分片 + 可选 XOR FEC
 ├── UDP sendmsg()
 └── drain 视频 socket 接收队列（REGISTER / VIDEO_ACK / VIDEO_NACK）

ControlReceiver 线程（std::thread）
 ├── UDP recvfrom()（blocking, SO_RCVTIMEO=1s）
 ├── ProtocolCodec::verify_crc() + parse
 ├── send_ack()（在本线程内同步发送）
 └── cmd_callback_()（→ RemoteLinkNode::on_command → publisher_->publish()）
     注：rclcpp Publisher::publish() 线程安全，可直接在非 ROS 线程调用
```

**帧队列大小 = 1**：`push_frame()` 直接覆盖 `latest_frame_`，配合 `has_new_frame_` 标志，VideoSender 每次只取最新帧，彻底避免延迟堆积。

### 4.2 test_frame_publisher 线程设计

```
主线程（rclcpp::spin）
 └── WallTimer（period = 1000/fps ms）
       ├── TestFrameGenerator::generate(frame_id++)
       ├── pub_->borrow_loaned_message()
       ├── 填充 Image 字段（零拷贝写入 loaned buffer）
       └── pub_->publish(std::move(loaned_msg))
```

---

## 5. 零拷贝 Loaned Message 设计

### 5.1 适用范围

**仅限** `/sending_frame`（Image 消息，1280×720 BGR8 = ~2.8 MB/帧 × 30 fps = ~84 MB/s）。  
其他消息（RemoteCommand、参数、心跳等）带宽小，不使用 Loaned Message，避免内存池碎片化。

### 5.2 Publisher 侧（test_frame_publisher）

```cpp
// 节点初始化
pub_ = create_publisher<sensor_msgs::msg::Image>(
    "/sending_frame",
    rclcpp::QoS(1).best_effort().durability_volatile());

// 定时回调中
void timer_callback() {
    auto loaned = pub_->borrow_loaned_message();
    auto & msg = loaned.get();

    // 填充 header
    msg.header.stamp = now();
    msg.header.frame_id = "camera";
    msg.width  = width_;
    msg.height = height_;
    msg.encoding = "bgr8";
    msg.step = width_ * 3;
    msg.data.resize(width_ * height_ * 3);

    // 直接写入 loaned buffer（零拷贝）
    cv::Mat view(height_, width_, CV_8UC3, msg.data.data());
    generator_->generate_into(frame_id_++, view);  // 直接绘制到 msg.data

    pub_->publish(std::move(loaned));
}
```

`TestFrameGenerator::generate_into(frame_id, cv::Mat& dst)` 直接绘制到外部 Mat，避免额外 copy。

### 5.3 Subscriber 侧（remote_link）

```cpp
// 节点初始化：使用 unique_ptr 回调以启用零拷贝路径
frame_sub_ = create_subscription<sensor_msgs::msg::Image>(
    "/sending_frame",
    rclcpp::QoS(1).best_effort().durability_volatile(),
    [this](std::unique_ptr<sensor_msgs::msg::Image> msg) {
        this->on_frame(std::move(msg));
    });

void on_frame(std::unique_ptr<sensor_msgs::msg::Image> msg) {
    // 构造 cv::Mat，不复制数据（data 生命周期与 msg 绑定）
    cv::Mat frame(msg->height, msg->width, CV_8UC3,
                  const_cast<uint8_t*>(msg->data.data()));
    video_tx_->push_frame(frame);
    // msg 在此析构，loaned buffer 归还内存池
}
```

### 5.4 零拷贝生效条件

两节点**确定**部署于同一 rclcpp_components container，intra-process 零拷贝**必然生效**，无需依赖特定 RMW。

| 条件                        | 状态    |
|---------------------------|-------|
| Publisher/Subscriber 同进程  | ✓ 确定  |
| `use_intra_process_comms(true)` | ✓ 节点初始化时设置 |
| QoS 双端匹配（深度=1, best_effort）| ✓ 需保证 |
| RMW 支持 loaned message      | 不依赖（intra-process 路径绕过 RMW） |

**Launch 文件（确定形式）：**
```xml
<!-- launch/pip_link_container.launch.xml -->
<node pkg="rclcpp_components" exec="component_container_mt" name="pip_container">
  <param name="use_intra_process_comms" value="true"/>
</node>
<load_composable_node target="/pip_container">
  <composable_node pkg="test_frame_publisher"
                   plugin="test_frame_publisher::TestFramePublisherNode"
                   extra_arguments="[{'use_intra_process_comms': true}]"/>
  <composable_node pkg="remote_link"
                   plugin="remote_link::RemoteLinkNode"
                   extra_arguments="[{'use_intra_process_comms': true}]"/>
</load_composable_node>
```

节点构造时必须传入 `NodeOptions`：
```cpp
// 两个节点均如此声明
explicit RemoteLinkNode(const rclcpp::NodeOptions& options)
: rclcpp::Node("remote_link", options) { ... }
```

---

## 6. 核心类详细接口

### 6.1 ProtocolCodec

```cpp
class ProtocolCodec {
public:
    static constexpr uint16_t MAGIC          = 0xABCD;
    static constexpr uint8_t  VERSION        = 0x01;
    static constexpr size_t   HEADER_SIZE    = 9;
    static constexpr size_t   VIDEO_HDR_SIZE = 20;
    static constexpr size_t   CHUNK_SIZE     = 60'000;

    // 返回 msg_type（0x01~0x07）；Magic/CRC 校验失败返回 -1
    static int  parse_type(const uint8_t* data, size_t len);
    static bool verify_crc(const uint8_t* data, size_t len);

    static bool parse_control_command(
        const uint8_t* data, size_t len,
        uint32_t& seq, double& t1,
        uint8_t  keyboard_state[10],
        int16_t& mouse_dx, int16_t& mouse_dy,
        uint8_t& mouse_buttons, int8_t& scroll_delta);

    static bool parse_param_update(
        const uint8_t* data, size_t len,
        uint32_t& seq, std::string& json_payload);

    // 写入 buf，返回字节数（buf 至少 29 字节）
    static size_t build_ack(uint8_t* buf, size_t buf_size,
                             uint32_t seq, double t2, double t3);

    // 写入 buf，返回字节数（buf 至少 VIDEO_HDR_SIZE 字节）
    static size_t build_video_chunk_header(
        uint8_t* buf,
        uint32_t frame_id, uint16_t total_chunks, uint16_t chunk_idx,
        uint32_t chunk_size, uint8_t fec_flag, uint16_t orig_chunks,
        uint8_t codec_flag, float encode_ms);

    // 构建参数回复（序列化当前参数 JSON + Header + CRC）
    static size_t build_param_response(
        uint8_t* buf, size_t buf_size,
        uint32_t seq, const std::string& json_params);

private:
    static uint32_t calc_crc(const uint8_t* data, size_t len);
};
```

### 6.2 FrameEncoder

```cpp
struct EncoderConfig {
    int     quality         = 80;    // JPEG 质量
    int     target_bitrate  = 2000;  // kbps（H.264）
    int     fps             = 30;
    bool    use_h264        = false;
    int     width           = 1280;
    int     height          = 720;
    // 图像增强
    int     brightness      = 0;     // -100~100
    int     contrast        = 0;     // -100~100
    int     sharpness       = 0;     // 0~100
    int     denoise         = 0;     // 0~100
};

class FrameEncoder {
public:
    explicit FrameEncoder(const EncoderConfig& cfg);

    // 编码 BGR Mat，返回编码字节流；codec_flag: 0=JPEG, 1=H.264
    std::vector<uint8_t> encode(const cv::Mat& frame,
                                 bool force_keyframe,
                                 uint8_t& codec_flag);

    void update_config(const EncoderConfig& cfg);

    float last_encode_ms() const { return last_encode_ms_; }
    int   current_quality() const { return quality_; }  // JPEG 自适应当前值

private:
    void adjust_quality(size_t encoded_size);
    cv::Mat apply_enhancements(const cv::Mat& frame) const;

    // JPEG 自适应状态
    int    quality_;
    float  ema_size_;          // 指数移动平均帧大小
    float  target_frame_bytes_;// target_bitrate_kbps * 1000 / 8 / fps

    // H.264（FFmpeg libavcodec）
    struct AVCodecContext* codec_ctx_{nullptr};
    struct AVFrame*        av_frame_{nullptr};
    int64_t                pts_{0};

    EncoderConfig cfg_;
    float         last_encode_ms_{0.0f};

    static constexpr float EMA_ALPHA  = 0.3f;
    static constexpr int   QUALITY_MIN = 15;
    static constexpr int   QUALITY_MAX = 85;
};
```

### 6.3 VideoSender

```cpp
class VideoSender {
public:
    struct Config {
        uint16_t    port         = 5000;
        bool        fec_enabled  = false;
        float       fec_redundancy = 0.2f;  // 仅 K=1 XOR，实际只判断 enabled
        int         fps          = 30;
        EncoderConfig encoder_cfg;
    };

    explicit VideoSender(const Config& cfg);
    ~VideoSender();

    void start();
    void stop();

    // 线程安全，覆盖式投递（只保留最新帧）
    void push_frame(const cv::Mat& frame);

    // ControlReceiver 检测到 REGISTER 包时调用
    void set_client_addr(const std::string& ip, uint16_t port);
    void clear_client_addr();
    bool has_client() const;

    void update_config(const Config& cfg);

    // 统计（供诊断 topic 使用）
    struct Stats {
        uint64_t frames_sent;
        uint64_t frames_acked;
        uint64_t bytes_sent;
        float    actual_bitrate_kbps;
        int      current_jpeg_quality;
    };
    Stats get_stats() const;

private:
    void sender_loop();
    void drain_recv_queue();    // 处理 REGISTER / VIDEO_ACK / VIDEO_NACK
    void send_frame(const cv::Mat& frame);
    void retransmit_nack(uint32_t frame_id,
                          const std::vector<uint16_t>& missing);

    // 帧队列（深度=1）
    std::mutex              frame_mutex_;
    cv::Mat                 latest_frame_;
    bool                    has_new_frame_{false};
    std::condition_variable frame_cv_;

    // 客户端地址
    mutable std::mutex      client_mutex_;
    struct sockaddr_in      client_addr_{};
    bool                    has_client_{false};
    std::atomic<double>     last_client_time_{0.0};

    // NACK 帧缓存
    std::mutex              cache_mutex_;
    std::map<uint32_t, std::map<uint16_t, std::vector<uint8_t>>> frame_cache_;
    static constexpr int    FRAME_CACHE_MAX = 10;

    int                     video_fd_{-1};
    uint32_t                frame_id_{0};
    std::atomic<bool>       running_{false};
    std::thread             sender_thread_;

    Config                  cfg_;
    std::unique_ptr<FrameEncoder> encoder_;
    mutable std::mutex      cfg_mutex_;

    // 码率统计
    std::atomic<uint64_t>   bytes_sent_window_{0};
    std::chrono::steady_clock::time_point window_start_;
};
```

### 6.4 ControlReceiver

```cpp
class ControlReceiver {
public:
    // 收到完整 CONTROL_COMMAND 包时触发
    using CommandCallback = std::function<void(
        const std::string& client_ip,
        uint32_t seq, double t1,
        const uint8_t kb[10],
        int16_t mouse_dx, int16_t mouse_dy,
        uint8_t mouse_buttons, int8_t scroll_delta)>;

    // 收到 PARAM_UPDATE 包时触发（JSON 字符串）
    using ParamUpdateCallback = std::function<void(
        uint32_t seq, const std::string& json)>;

    explicit ControlReceiver(uint16_t port);
    ~ControlReceiver();

    void set_command_callback(CommandCallback cb);
    void set_param_update_callback(ParamUpdateCallback cb);

    void start();
    void stop();

    double last_client_time() const { return last_client_time_.load(); }

private:
    void recv_loop();
    void handle_control_command(const uint8_t* data, size_t len,
                                 const struct sockaddr_in& from);
    void handle_heartbeat(const uint8_t* data, size_t len,
                          const struct sockaddr_in& from);
    void handle_param_update(const uint8_t* data, size_t len,
                              const struct sockaddr_in& from);
    void handle_param_query(const uint8_t* data, size_t len,
                             const struct sockaddr_in& from);
    void send_ack(int fd, const struct sockaddr_in& to,
                  uint32_t seq, double t2, double t3);

    uint16_t              port_;
    int                   control_fd_{-1};
    std::atomic<bool>     running_{false};
    std::thread           recv_thread_;

    CommandCallback       cmd_cb_;
    ParamUpdateCallback   param_cb_;

    std::atomic<double>   last_client_time_{0.0};
    std::string           client_ip_;
    std::mutex            client_ip_mutex_;
};
```

### 6.5 MdnsService

```cpp
class MdnsService {
public:
    struct Config {
        std::string service_name;  // e.g. "air_unit_01"
        uint16_t    control_port;
        uint16_t    video_port;
    };

    explicit MdnsService(const Config& cfg);
    ~MdnsService();

    // 阻塞直到 avahi 注册成功或超时
    bool start(std::chrono::seconds timeout = std::chrono::seconds(5));
    void stop();

private:
    void avahi_thread_loop();

    Config cfg_;
    // avahi client/group 指针（不透明，在 .cpp 中完整声明）
    struct AvahiClient*       avahi_client_{nullptr};
    struct AvahiEntryGroup*   avahi_group_{nullptr};
    struct AvahiSimplePoll*   avahi_poll_{nullptr};
    std::thread               avahi_thread_;
    std::atomic<bool>         running_{false};
    std::atomic<bool>         registered_{false};
};
```

### 6.6 RemoteLinkNode

```cpp
class RemoteLinkNode : public rclcpp::Node {
public:
    explicit RemoteLinkNode(const rclcpp::NodeOptions& options = {});
    ~RemoteLinkNode();

private:
    // --- ROS 回调 ---
    // 零拷贝路径：unique_ptr 接管 loaned message 所有权
    void on_frame(std::unique_ptr<sensor_msgs::msg::Image> msg);

    // ControlReceiver 回调（在 ControlReceiver 线程调用）
    void on_command(const std::string& client_ip,
                    uint32_t seq, double t1,
                    const uint8_t kb[10],
                    int16_t mouse_dx, int16_t mouse_dy,
                    uint8_t mouse_buttons, int8_t scroll_delta);

    // 收到 UDP PARAM_UPDATE 包时触发（在 ControlReceiver 线程调用）
    void on_param_update_from_udp(uint32_t seq, const std::string& json);

    // rclcpp Parameter 回调（参数被本地修改时触发）
    rcl_interfaces::msg::SetParametersResult
    on_parameter_change(const std::vector<rclcpp::Parameter>& params);

    void watchdog_tick();

    // 将当前 ROS 参数打包为 JSON，供 PARAM_QUERY 回复使用
    std::string params_to_json() const;

    // 应用参数变更到 VideoSender
    void apply_video_config();

    // --- ROS 接口 ---
    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr       frame_sub_;
    rclcpp::Publisher<pip_vision_interfaces::msg::RemoteCommand>::SharedPtr cmd_pub_;
    rclcpp::TimerBase::SharedPtr                                   watchdog_timer_;
    rclcpp::node_interfaces::OnSetParametersCallbackHandle::SharedPtr param_cb_handle_;

    // --- 通信组件 ---
    std::unique_ptr<ControlReceiver> control_rx_;
    std::unique_ptr<VideoSender>     video_tx_;
    std::unique_ptr<MdnsService>     mdns_;
};
```

---

## 7. 关键实现细节

### 7.1 参数更新流（双向）

```
【地面站下发参数】
地面站 UDP PARAM_UPDATE (0x02) 包
  → ControlReceiver::recv_loop() 接收
  → on_param_update_from_udp() 回调（ControlReceiver 线程）
  → nlohmann::json 解析 JSON
  → this->set_parameters({...})        // 更新 ROS2 Parameter Server
  → on_parameter_change() 触发         // Parameter 回调在调用线程同步执行
  → apply_video_config()               // 推给 VideoSender
  → ControlReceiver::send_ack()        // 回复 ACK
  → ControlReceiver 发 PARAM_UPDATE 回包（当前完整参数 JSON）

【本地参数修改（ros2 param set）】
ros2 param set /remote_link encoder h264
  → on_parameter_change() 触发（在 rclcpp 线程）
  → apply_video_config()               // 推给 VideoSender（编码器重建）
  注：本地修改不触发 UDP 发送，地面站下次 PARAM_QUERY 才能同步
```

### 7.2 CRC32 计算

使用 zlib（与 Python 端一致，保证互通）：

```cpp
#include <zlib.h>

// 封装 seal（追加 CRC32 到末尾）
static void seal(std::vector<uint8_t>& buf) {
    uint32_t crc = crc32(0, buf.data(), buf.size());
    // little-endian
    buf.push_back((crc >>  0) & 0xFF);
    buf.push_back((crc >>  8) & 0xFF);
    buf.push_back((crc >> 16) & 0xFF);
    buf.push_back((crc >> 24) & 0xFF);
}
```

### 7.3 H.264 编码器（FFmpeg libavcodec）

与 Python `av` 库等价的参数：

```cpp
codec_ctx->codec_id   = AV_CODEC_ID_H264;
codec_ctx->width      = width;
codec_ctx->height     = height;
codec_ctx->time_base  = {1, fps};
codec_ctx->bit_rate   = bitrate_kbps * 1000;
codec_ctx->pix_fmt    = AV_PIX_FMT_YUV420P;
codec_ctx->gop_size   = keyframe_interval;  // 30
codec_ctx->max_b_frames = 0;                // 无 B 帧，降低延迟
av_opt_set(codec_ctx->priv_data, "preset",  "ultrafast", 0);
av_opt_set(codec_ctx->priv_data, "tune",    "zerolatency", 0);
av_opt_set(codec_ctx->priv_data, "profile", "baseline", 0);
```

BGR → YUV420P 转换用 `sws_scale()`（libswscale）。

### 7.4 JPEG 自适应质量算法

```cpp
// FrameEncoder::adjust_quality()
void FrameEncoder::adjust_quality(size_t encoded_size) {
    ema_size_ = EMA_ALPHA * encoded_size + (1.0f - EMA_ALPHA) * ema_size_;
    float ratio = ema_size_ / target_frame_bytes_;
    if      (ratio > 1.1f) quality_ = std::max(QUALITY_MIN, quality_ - 2);
    else if (ratio < 0.8f) quality_ = std::min(QUALITY_MAX, quality_ + 1);
}
```

### 7.5 FEC cm256 实现

**编码端（C++ VideoSender）：**

```cpp
#include "cm256.h"

// 调用一次 cm256_init()（全局初始化）

std::vector<std::vector<uint8_t>> fec_encode(
    const std::vector<std::vector<uint8_t>>& data_chunks,
    float redundancy)
{
    int N = static_cast<int>(data_chunks.size());
    int K = std::max(1, static_cast<int>(std::ceil(N * redundancy)));
    assert(N + K <= 256);  // cm256 硬性限制

    // 所有块等长，零填充
    size_t block_size = 0;
    for (auto& c : data_chunks) block_size = std::max(block_size, c.size());

    // 准备输入（每块指向 padded buffer）
    std::vector<std::vector<uint8_t>> padded(N,
        std::vector<uint8_t>(block_size, 0));
    for (int i = 0; i < N; ++i)
        memcpy(padded[i].data(), data_chunks[i].data(), data_chunks[i].size());

    // cm256 原地编码：parity 写入 recovery_blocks
    cm256_encoder_params params{N, K, (int)block_size};
    std::vector<cm256_block> blocks(N);
    for (int i = 0; i < N; ++i)
        blocks[i] = {padded[i].data(), (uint8_t)i};

    std::vector<std::vector<uint8_t>> parity(K,
        std::vector<uint8_t>(block_size, 0));
    std::vector<cm256_block> recovery(K);
    for (int i = 0; i < K; ++i)
        recovery[i] = {parity[i].data(), (uint8_t)(N + i)};

    cm256_encode(params, blocks.data(), recovery[0].Block);
    // 注意：C++ 侧 cm256_init() 是宏（展开为 cm256_init_(CM256_VERSION)）；
    // Python ctypes 须直接调用 cm256_init_，不能用宏名。

    // 返回 data + parity，parity 的 Index 字段即 wire 上的 chunk_idx
    auto result = padded;  // data chunks（已 padded，发送时按 chunk_size 截断）
    for (auto& p : parity) result.push_back(std::move(p));
    return result;
}
```

**解码端（Python 客户端 `network/fec.py` 替换方案，见 §7.6）**

---

### 7.6 Python 客户端 FEC 迁移（network/fec.py）

`network/fec.py` 中 `FECEncoder`/`FECDecoder` 需替换为 cm256 Python 绑定。

**安装**：
```bash
pip install cm256-python   # 或自行从 catid/cm256 编译 Python 扩展
```

**FECEncoder 替换思路**（保持对外接口不变，仅替换内部实现）：

```python
# network/fec.py — FECEncoder.encode() 新实现
import cm256  # cm256-python 绑定

def encode(self, chunks: List[bytes]) -> List[bytes]:
    n = len(chunks)
    k = max(1, math.ceil(n * self.redundancy))
    assert n + k <= 256, "cm256 limit: N+K <= 256"

    block_size = max(len(c) for c in chunks)
    # 零填充至等长
    padded = [c.ljust(block_size, b'\x00') for c in chunks]

    parity_blocks = cm256.encode(padded, k)  # 返回 k 个 parity bytes
    return chunks + parity_blocks            # 原始 chunks（未填充）+ parity
```

**FECDecoder 替换思路**：

```python
def decode(self, received: Dict[int, bytes],
           n_data: int, n_total: int,
           chunk_sizes: Optional[Dict[int, int]] = None) -> Optional[List[bytes]]:
    k = n_total - n_data
    if len(received) < n_data:
        return None

    # 已有全部 data chunks，直接返回
    if all(i in received for i in range(n_data)):
        result = []
        for i in range(n_data):
            data = received[i]
            if chunk_sizes and i in chunk_sizes:
                data = data[:chunk_sizes[i]]
            result.append(data)
        return result

    block_size = max(len(v) for v in received.values())
    # cm256 decode：传入已有块（含 parity），标注 Index
    cm256_blocks = []
    for idx, data in received.items():
        cm256_blocks.append({'index': idx, 'data': data.ljust(block_size, b'\x00')})

    try:
        recovered = cm256.decode(cm256_blocks, n_data, k, block_size)
        # recovered 是完整的 n_data 个块（按 index 排序）
        result = []
        for i in range(n_data):
            data = recovered[i]
            if chunk_sizes and i in chunk_sizes:
                data = data[:chunk_sizes[i]]
            result.append(data)
        return result
    except Exception:
        return None
```

**迁移注意事项**：
- `FEC_AVAILABLE` 改为检测 `cm256` 可用性
- 对外接口（`encode(chunks)` / `decode(received, n_data, n_total, chunk_sizes)`）**保持不变**，上层代码无需修改
- `air_unit_server.py` 中的 `FECEncoder` 也需同步替换（机载 Python 测试脚本，不影响 C++ 节点）

---

### 7.7 视频 Socket drain 循环（VideoSender 内部）

发送前 non-blocking drain，处理 REGISTER / ACK / NACK：

```cpp
void VideoSender::drain_recv_queue() {
    uint8_t buf[1024];
    struct sockaddr_in from{};
    socklen_t from_len = sizeof(from);

    // 非阻塞模式
    while (true) {
        ssize_t n = recvfrom(video_fd_, buf, sizeof(buf),
                             MSG_DONTWAIT,
                             (struct sockaddr*)&from, &from_len);
        if (n <= 0) break;

        if (n == 8 && memcmp(buf, "REGISTER", 8) == 0) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
            set_client_addr(ip, ntohs(from.sin_port));
        }
        else if (n >= 9 && ProtocolCodec::parse_type(buf, n) == 0x06) {
            // VIDEO_ACK：统计
            stats_.frames_acked++;
        }
        else if (n >= 9 && ProtocolCodec::parse_type(buf, n) == 0x07) {
            // VIDEO_NACK：解析缺失分片 → retransmit
            // ...（解析 chunk 索引列表，从 frame_cache_ 重发）
        }
    }
}
```

### 7.8 客户端超时（Watchdog）

```cpp
void RemoteLinkNode::watchdog_tick() {
    using namespace std::chrono;
    double now = now_sec();
    double last = control_rx_->last_client_time();
    if (last > 0.0 && (now - last) > client_timeout_s_) {
        RCLCPP_WARN(get_logger(),
            "Client timeout (%.0fs), stopping video stream", now - last);
        video_tx_->clear_client_addr();
    }
}
```

---

## 8. test_frame_publisher 实现要点

### 8.1 TestFrameGenerator

```cpp
class TestFrameGenerator {
public:
    TestFrameGenerator(int width, int height);

    // 直接绘制到 dst（零拷贝，配合 Loaned Message 使用）
    void generate_into(uint64_t frame_id, cv::Mat& dst);

private:
    void build_base_frame();  // 彩条 + 网格 + 角标，只构建一次

    cv::Mat base_frame_;
    int     width_, height_;
};
```

**动态内容**（每帧叠加到 `base_frame_` 副本，然后写入 `dst`）：
- 帧计数 + 时间戳（右下角）
- 旋转指针（`angle = frame_id * 12 % 360`）
- 水平滚动 cyan 色带（底部）
- 横向移动棋盘格块

### 8.2 TestFramePublisherNode

```cpp
class TestFramePublisherNode : public rclcpp::Node {
public:
    explicit TestFramePublisherNode(const rclcpp::NodeOptions& = {});

private:
    void timer_callback();

    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr pub_;
    rclcpp::TimerBase::SharedPtr    timer_;
    std::unique_ptr<TestFrameGenerator> generator_;
    uint64_t frame_id_{0};
    int width_, height_;
};
```

---

## 9. 依赖清单

| 依赖                       | 用途                           | Ubuntu 包名 / 来源             |
|--------------------------|------------------------------|------------------------------|
| `rclcpp`                 | ROS2 节点框架                    | ros-\*-rclcpp                |
| `rclcpp_components`      | Component 容器（零拷贝部署）         | ros-\*-rclcpp-components     |
| `sensor_msgs`            | Image 消息                     | ros-\*-sensor-msgs           |
| `cv_bridge`              | ROS Image ↔ cv::Mat           | ros-\*-cv-bridge             |
| `opencv4`                | 图像处理 / JPEG 编码                | libopencv-dev                |
| `libavcodec / libswscale`| H.264 编码（FFmpeg）              | libavcodec-dev libswscale-dev|
| `libavahi-client`        | mDNS 注册（avahi C API）         | libavahi-client-dev          |
| `zlib`                   | CRC32（与 Python 端互通）           | zlib1g-dev（通常已有）           |
| `cm256`                  | FEC 库（当前仅作依赖，K=1 用 XOR）    | GitHub: catid/cm256          |
| `nlohmann_json`          | PARAM_UPDATE JSON 解析         | nlohmann-json3-dev           |
| `pip_vision_interfaces`  | RemoteCommand.msg             | 本项目自定义                     |

---

## 10. 已决策事项汇总

| 事项 | 决策 |
|------|------|
| FEC K>1 兼容性 | C++ 使用 cm256 全路径（K=1 XOR + K>1 Cauchy）；Python `network/fec.py` 同步替换为 cm256 绑定（见 §7.6） |
| mouse_vx/vy 单位 | `px/s`，用相邻包 `t1` 动态计算 dt；dt 超 200ms 置零，dt < 1ms 截断；首包无 prev_t1 时输出零速度 |
| 节点部署方式 | 两节点确定在同一 rclcpp_components container，intra-process 零拷贝必然生效（见 §5.4） |

---

## 11. 实现优先级

```
Phase 1（最小可用）
  ✓ ProtocolCodec（CRC + CONTROL_COMMAND 解析 + ACK 构建）
  ✓ ControlReceiver（UDP 监听 + 解包 + ACK 发送 + HEARTBEAT 处理）
  ✓ RemoteLinkNode 基础骨架（订阅 /sending_frame，发布 /remote_command）
  ✓ VideoSender（JPEG only，无 FEC）
  ✓ FrameEncoder（JPEG + 自适应质量算法）
  ✓ MdnsService（avahi 注册）
  ✓ TestFramePublisherNode（含 Loaned Message 发布）

Phase 2（功能完整）
  ✓ FEC K=1 XOR parity（encode + NACK 重传）
  ✓ PARAM_UPDATE/PARAM_QUERY 处理（UDP → Parameter Server）
  ✓ on_parameter_change 回调（Parameter Server → VideoSender）
  ✓ Watchdog（客户端超时清理）
  ✓ 订阅侧 Loaned Message unique_ptr 回调

Phase 3（增强）
  ✓ H.264 编码器（FFmpeg libavcodec）
  ✓ 图像增强参数（brightness/contrast/sharpness/denoise）
  ✓ 统计 / 诊断 topic 发布（bitrate、encode_ms、quality）
  ✓ Component 容器 launch 文件
```
