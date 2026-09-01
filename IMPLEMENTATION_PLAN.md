## Stage 1: 控制安全与会话存活
**Goal**: 传输真实 READY 状态，只接受匹配 ACK，并在视频停流时清屏及退出 READY。
**Success Criteria**: 机载端发布正确 `is_ready`；陈旧 ACK 不建链；视频超时后不保留旧画面或 READY。
**Tests**: 协议单元测试、后端 loopback 停流/陈旧 ACK 测试。
**Status**: Complete

## Stage 2: 图传参数与数据面
**Goal**: 让 MTU、JPEG 质量、FEC 和低延迟设置具有真实效果，并限制异常分片资源占用。
**Success Criteria**: 分片大小服从 MTU；JPEG 更新立即生效；FEC 设置与能力一致；接收端拒绝超限帧。
**Tests**: 协议边界测试、源码契约测试、loopback 小分片测试。
**Status**: In Progress

## Stage 3: 媒体与录像生命周期
**Goal**: 消除 FFmpeg 管道退出卡死、并发停止和无有效视频仍显示录像的问题。
**Success Criteria**: 停止可取消且有界；录像启动要求有效码流；编码切换明确失败。
**Tests**: 媒体管线测试、录像状态回归测试。
**Status**: Not Started

## Stage 4: 配置、线程与交互完整性
**Goal**: 校验持久化配置、修复 ROS 参数数据竞争，完成手柄入口和手动端口配置。
**Success Criteria**: 坏配置安全回退；参数访问同步；手柄状态不再伪报；手动连接支持独立视频端口。
**Tests**: 配置回归测试、ROS 源码契约测试、输入测试。
**Status**: Not Started

## Stage 5: UI 规范与完整验证
**Goal**: 修复无视频 HUD、滚动/Combo 规范和干净 Release 构建。
**Success Criteria**: HUD 状态始终可见；设置页交互符合项目规范；Debug/Release 构建与全量测试通过。
**Tests**: UI 回归测试、全量 CTest、静态检查、干净 Release 配置。
**Status**: Not Started
