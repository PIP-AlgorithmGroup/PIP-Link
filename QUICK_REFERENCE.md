# PIP-Link 快速参考

## 项目结构

```
PIP-Link/
├── network/              # 网络通信层
│   ├── protocol.py       # 消息编解码
│   ├── udp_socket.py     # UDP 基础
│   ├── service_discovery.py  # mDNS 发现
│   ├── control_sender.py # 控制发送
│   ├── video_receiver.py # 视频接收
│   ├── heartbeat.py      # 心跳管理
│   └── session.py        # 会话管理
├── logic/                # 业务逻辑层
│   ├── latency_calculator.py  # 延迟计算
│   ├── input_mapper.py   # 输入映射
│   └── status_monitor.py # 状态监控
├── ui/                   # UI 层
│   ├── renderer.py       # 视频渲染
│   ├── imgui_ui.py       # ImGui UI
│   └── input_handler.py  # 输入处理
├── core/
│   └── app.py            # 主应用
├── tests/                # 测试脚本
│   ├── air_unit_simulator.py  # 本地模拟器
│   ├── run_test.py       # 一键测试
│   └── test_*.py         # 集成测试
├── air_unit_server.py    # 机载端服务器
├── main.py               # 入口
├── config.py             # 配置
└── 文档
    ├── USER_GUIDE.md     # 客户端使用指南
    ├── TESTING_GUIDE.md  # 测试指南
    ├── AIR_UNIT_SERVER_GUIDE.md  # 机载端指南
    ├── BACKEND_SUMMARY.md        # 后端总结
    └── IMPLEMENTATION_PLAN.md    # 实现计划
```

## 快速开始

### 1. 本地测试（无需硬件）

```bash
# 终端 1：启动模拟器
python tests/air_unit_simulator.py

# 终端 2：启动客户端
python main.py
```

### 2. 远程测试（需要 Ubuntu 机载端）

```bash
# 在 Ubuntu 上运行
python3 air_unit_server.py

# 在 Windows 上运行
python main.py
```

### 3. 一键测试

```bash
python tests/run_test.py
```

## 客户端操作

| 快捷键 | 功能 |
|--------|------|
| ESC | 打开/关闭菜单 |
| ~ | 打开开发者控制台 |
| WASD | 控制移动 |
| Space | 动作按钮 |
| Shift | 冲刺 |

## 菜单操作

1. **SCAN** - 发现机载端
2. **Connect** - 连接到机载端
3. **Disconnect** - 断开连接
4. **Settings** - 调整参数
5. **Quit** - 退出应用

## 性能指标

| 指标 | 值 |
|------|-----|
| 控制指令发送率 | 50 Hz |
| 视频帧发送率 | ~30 fps |
| 主循环帧率 | 60 fps |
| RTT 测量精度 | 微秒级 |
| 平均丢包率 | < 1% |

## 配置文件

### config.py

主要配置项：
- `RENDER_WIDTH`: 1600
- `RENDER_HEIGHT`: 900
- `TARGET_FPS`: 120
- `TX_SEND_RATE`: 50 Hz
- `HEARTBEAT_INTERVAL`: 100ms

### config.json

保存的用户设置：
- 按键绑定
- 鼠标灵敏度
- 视频质量

## 网络协议

### 消息格式

```
[Magic:2][Version:1][MsgType:1][Reserved:1][Seq:4][Timestamp:8][Payload:var][CRC32:4]
```

### 消息类型

- 0x01: 控制指令
- 0x04: 心跳
- 0x05: ACK 响应

### 延迟测量

四时间戳方案：
- t1: 客户端发送时间
- t2: 机载端接收时间
- t3: 机载端发送 ACK 时间
- t4: 客户端接收 ACK 时间

RTT = (t4 - t1) - (t3 - t2)

## 故障排除

### 问题：无法发现服务

**解决：**
1. 检查防火墙（允许 UDP 5353）
2. 检查网络连接
3. 确保服务器已启动

### 问题：连接后无视频

**解决：**
1. 检查视频端口配置
2. 检查防火墙（允许 UDP 5000）
3. 查看开发者控制台日志

### 问题：UI 冻结

**解决：**
1. 等待服务发现完成（最多 10 秒）
2. 检查网络连接

## 文档导航

| 文档 | 内容 |
|------|------|
| USER_GUIDE.md | 客户端使用指南 |
| TESTING_GUIDE.md | 测试方法和场景 |
| AIR_UNIT_SERVER_GUIDE.md | 机载端服务器使用 |
| BACKEND_SUMMARY.md | 后端实现总结 |
| IMPLEMENTATION_PLAN.md | 详细实现计划 |

## 常用命令

```bash
# 启动客户端
python main.py

# 启动本地模拟器
python tests/air_unit_simulator.py

# 启动机载端服务器（Ubuntu）
python3 air_unit_server.py

# 一键测试
python tests/run_test.py

# 运行集成测试
python tests/test_backend_integration.py

# 启用详细日志
python3 air_unit_server.py --verbose
```

## 开发者信息

### 后端架构

- **Phase 1**: 协议与基础通信 ✓ 100%
- **Phase 2**: 网络线程框架 ✓ 100%
- **Phase 3**: 业务逻辑层 ✓ 100%
- **Phase 4**: UI 集成 ✓ 20%
- **Phase 5**: 优化与完善 ⏳ 0%

### 关键特性

- ✓ 微秒级 RTT 测量
- ✓ 消息 CRC32 校验
- ✓ ACK 等待和重传
- ✓ 心跳检测和自动重连
- ✓ mDNS 服务发现
- ✓ 线程安全设计
- ✓ 跨平台兼容性

### 依赖

- pygame: UI 框架
- PyOpenGL: 3D 渲染
- imgui: UI 库
- zeroconf: mDNS 服务发现
- pynput: 键盘输入

## 支持

遇到问题？
1. 查看相关文档
2. 检查开发者控制台日志
3. 查看 IMPLEMENTATION_PLAN.md 了解系统架构
