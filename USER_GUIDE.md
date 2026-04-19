# PIP-Link 客户端使用指南

## 系统要求

- Windows 11 / Linux / macOS
- Python 3.10+
- Conda 环境：`PIP_Link`

## 安装依赖

```bash
conda activate PIP_Link
pip install -r requirements.txt
```

## 运行客户端

```bash
python main.py
```

## 使用流程

### 1. 启动机载端（Ubuntu 22.04）

```bash
# 在机载端运行
ros2 run video_transmitter video_transmitter_node
```

### 2. 启动客户端（Windows）

```bash
python main.py
```

### 3. 连接到机载端

- 按 ESC 打开菜单
- 点击 "Connect" 按钮
- 系统会自动发现局域网内的机载端服务
- 连接成功后会显示视频和统计信息

## 菜单操作

- **ESC**: 打开/关闭菜单
- **~** (Tilde): 打开/关闭开发者控制台
- **WASD**: 控制移动（连接后）
- **Space**: 动作按钮
- **Shift**: 冲刺

## 状态栏信息

右下角显示实时统计：
- **FPS**: 渲染帧率
- **RTT**: 往返延迟（毫秒）
- **Loss**: 丢包率（百分比）
- **Frames**: 接收帧数

## 故障排除

### 服务发现超时

**问题**: "Service discovery timeout"

**原因**:
- 机载端未运行
- 网络不连通
- 防火墙阻止 mDNS

**解决**:
1. 确保机载端已启动
2. 检查网络连接
3. 检查防火墙设置

### UI 冻结

**问题**: 应用无响应

**原因**:
- 服务发现阻塞（已修复）
- 网络延迟过高

**解决**:
1. 等待服务发现完成（最多 10 秒）
2. 检查网络连接

### 无视频显示

**问题**: 黑屏，无视频

**原因**:
- 机载端未发送视频
- 视频端口配置错误

**解决**:
1. 检查机载端是否正常运行
2. 检查 config.py 中的端口配置

## 配置文件

### config.py

主要配置项：
- `RENDER_WIDTH`: 渲染宽度（默认 1600）
- `RENDER_HEIGHT`: 渲染高度（默认 900）
- `TARGET_FPS`: 目标帧率（默认 120）
- `TX_SEND_RATE`: 控制指令发送率（默认 50Hz）
- `HEARTBEAT_INTERVAL`: 心跳间隔（默认 100ms）

### config.json

保存的配置：
- 按键绑定
- 鼠标灵敏度
- 视频质量
- 其他用户设置

## 性能指标

- 控制指令延迟：< 10ms（局域网）
- 视频帧率：> 25fps（取决于网络）
- 丢包率：< 5%（局域网）

## 开发者模式

按 `~` 打开开发者控制台查看：
- 系统日志
- 网络统计
- 性能数据

## 常见问题

**Q: 如何修改分辨率？**
A: 编辑 config.py，修改 `RENDER_WIDTH` 和 `RENDER_HEIGHT`

**Q: 如何调整鼠标灵敏度？**
A: 在菜单中的 Settings 调整，或编辑 config.json

**Q: 支持录制吗？**
A: 目前不支持，可在菜单中启用录制选项（需要实现）

**Q: 如何连接多个机载端？**
A: 目前只支持单个连接，需要断开后重新连接

## 技术细节

### 网络协议

- 消息格式：[Magic:2][Version:1][MsgType:1][Reserved:1][Seq:4][Timestamp:8][Payload:var][CRC32:4]
- 控制指令：50Hz 发送率，100ms 超时重传
- 心跳：100ms 间隔，3 次超时触发重连
- 视频：UDP 分片接收，自动重组

### 延迟测量

使用四时间戳方案：
- t1: 客户端发送时间
- t2: 机载端接收时间
- t3: 机载端发送 ACK 时间
- t4: 客户端接收 ACK 时间

RTT = (t4 - t1) - (t3 - t2)

### 线程模型

- 主线程：UI 渲染和事件处理
- TX 线程：控制指令发送
- RX 线程：视频接收
- 心跳线程：连接监测
- 服务发现线程：mDNS 发现

## 支持

遇到问题？
1. 检查开发者控制台的日志
2. 查看 IMPLEMENTATION_PLAN.md 了解系统架构
3. 查看 BACKEND_SUMMARY.md 了解实现细节
