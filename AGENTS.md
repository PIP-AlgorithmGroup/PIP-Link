# PIP-Link UI Design Guidelines

## Animation Principles

### 帧率无关动画（Frame-Rate Independent）
**所有动画必须基于时间常数（tau），不得与帧时间强绑定。**

```python
# 在 draw 入口计算 dt（每帧一次）
now = time.time()
self._dt = min(now - self._last_frame_time, 0.1)  # cap 防止跳变
self._last_frame_time = now

# 指数衰减插值（替代固定系数 lerp）
t = 1.0 - math.exp(-self._dt / self.scroll_tau)
new_val = cur + diff * t
```

**参数：**
- `scroll_tau = 0.08`（秒）：滚动弹性时间常数，越小越快
- dt cap：0.1s，防止窗口失焦后恢复时跳变
- 收敛阈值：0.5px

**禁止：**
- ❌ `cur + diff * 0.25`（固定系数，帧率越高动画越快）
- ❌ 每帧固定增减像素值

**允许：**
- ✓ 基于 `time.time()` 的 elapsed/progress（如菜单淡入淡出）
- ✓ 指数衰减 `1 - exp(-dt/tau)`

## Scrolling Behavior

### Elastic Smooth Scrolling
所有滚动交互必须使用弹性平滑插值，避免生硬的直接跳转：

```python
# 标准模式：
# 1. 滚轮事件更新目标位置（大步进，如 100px）
# 2. 每帧用指数衰减插值向目标移动（基于 dt，帧率无关）
# 3. 接近目标时直接设置（避免无限逼近）

if mouse_wheel != 0:
    target_scroll -= mouse_wheel * 100

target_scroll = clamp(target_scroll, 0, max_scroll)

cur = get_scroll()
diff = target_scroll - cur
if abs(diff) > 0.5:
    t = 1.0 - math.exp(-dt / scroll_tau)
    set_scroll(cur + diff * t)  # 帧率无关弹性插值
else:
    set_scroll(target_scroll)  # 收敛
```

**关键参数：**
- 步进：100px（快速响应）
- 时间常数 tau：0.08s（平滑但不拖沓）
- 收敛阈值：0.5px（避免抖动）

### 应用场景
- Tab bar 横向滚动 ✓
- CONNECTION 页面设备列表滚动（待实现）
- 所有 child window 滚动区域

## Tab Bar Design

### 实现方式
- **不使用** ImGui 原生 `begin_tab_bar`（无法自定义滚轮行为）
- 使用 `invisible_button` + 手绘文本 + 底部高亮线

### 视觉风格
- Active tab：cyan 文本 + 底部 2px accent 线
- Hover tab：浅色文本（0.8, 0.85, 0.95）
- Inactive tab：次要文本色（Theme.TEXT_SECONDARY）
- 无边框、无背景矩形（避免"按钮感"）

### 交互
- 滚轮：横向滚动 tab 栏（不切换 tab）
- 点击：切换 tab
- 无左右箭头按钮
- 隐藏 scrollbar（alpha=0）

## Theme Principles

### CS2 风格
- 深色背景层次：0.08 → 0.05 → 0.10
- Accent 色：cyan (0.0, 0.85, 1.0)
- 字体三级：title(Bold 22), body(16), mono(18)

### 圆角设计（核心原则）
**所有 UI 元素必须带圆角，禁止纯直角矩形。**

| 元素 | 圆角值 | 设置方式 |
|------|--------|----------|
| 窗口 | 6px | `style.window_rounding` |
| Frame（输入框、slider） | 4px | `style.frame_rounding` |
| Tab | 4px | `style.tab_rounding` |
| Popup（下拉框弹出层） | 4px | push `STYLE_POPUP_ROUNDING` |
| Grab（slider 手柄） | 3px | `style.grab_rounding` |
| 自绘 hover 背景 | 4px | `draw_list.add_rect_filled(..., rounding)` |

**注意事项：**
- ImGui 部分元素默认无圆角（如 selectable hover 背景、combo popup），需要自绘覆盖
- 自绘矩形统一用 `draw_list.add_rect_filled` 并传入 `rounding` 参数
- 新增任何 UI 组件时，检查是否有直角矩形残留

### Scrollbar 处理
- 所有 scrollbar 颜色 alpha 设为 0.0（隐藏但保留功能）
- `scrollbar_size` 保持默认（不能设为 0，会 assert）
- 滚动行为由自定义弹性插值接管

## Animated Combo Dropdown

### 设计思路
不使用 ImGui 原生 `imgui.combo`（无法控制动画和圆角），改用 `begin_combo` + 自绘 item。

### 实现要点
- **展开/收起动画**：popup 高度用指数衰减插值（`combo_tau = 0.06s`）
- **popup 尺寸锁定**：`set_next_window_size_constraints` 锁死宽高，防止 ImGui 预留 scrollbar 宽度
- **popup padding 清零**：push `STYLE_WINDOW_PADDING = (0, 0)` 让框体精确匹配内容
- **高度自适应**：`step_h * min(len(items), max_visible)`，超出 `max_visible` 行则内部弹性滚动
- **item 间距归零**：combo 内部 push `STYLE_ITEM_SPACING = (0, 0)`

### 自绘 Item 风格
- 用 `invisible_button` 作为点击区域，不用原生 `selectable`（直角 hover 背景）
- hover 背景：`draw_list.add_rect_filled` + `rounding=4.0` + 左右对称 `pad_x` 内缩
- hover 动画：每个 item 独立追踪 alpha，指数衰减渐入渐出（`tau=0.06s`）
- 选中项：cyan 文字 + 常驻半透明背景
- 文字左边距：`pad_x + 6px`，垂直居中

### 高度计算
```python
text_h = imgui.get_text_line_height()       # 18px
item_pad_y = 4                               # 上下各 4px
step_h = text_h + item_pad_y * 2             # 26px per item
full_h = step_h * n + 2 * pad_top            # 总高度含上下留白
```
**不要用** `get_text_line_height_with_spacing()`（含全局 item_spacing，不准确）

## Hover & Interaction Design

### 通用原则
- **所有 hover 效果必须有动画过渡**，禁止瞬间切换
- hover alpha 用指数衰减插值（`tau=0.06s`），渐入渐出
- 每个可交互元素独立追踪 hover 状态

### 自绘交互元素模式
```python
# 1. invisible_button 作为点击/hover 检测区域
clicked = imgui.invisible_button(id, w, h)
hovered = imgui.is_item_hovered()

# 2. 动画 hover alpha
target = 1.0 if hovered else 0.0
alpha += (target - alpha) * (1 - exp(-dt / 0.06))

# 3. 自绘背景（圆角）+ 文字
draw_list.add_rect_filled(x0, y0, x1, y1, color, rounding=4.0)
draw_list.add_text(tx, ty, text_color, text)
```

### 适用场景
- Tab bar items ✓
- Combo dropdown items ✓
- 未来所有自定义按钮、列表项

## Development Environment

- conda 环境名：`PIP_Link`
- 运行 Python 脚本：`conda run -n PIP_Link python <script>`
- 检查 imgui 常量等：`conda run -n PIP_Link python -c "import imgui; ..."`

## Code Style

### 避免
- ❌ 按钮式 tab（圆角矩形边框很丑）
- ❌ 直接 `set_scroll` 无插值（卡顿）
- ❌ 过小步进（<50px，滚动太慢）
- ❌ 固定系数 lerp（`diff * 0.25`，帧率绑定）

### 推荐
- ✓ 纯文本 + 底部线条式 tab
- ✓ 指数衰减滚动（100px step, tau=0.08s）
- ✓ `invisible_button` 作为点击区域
- ✓ `draw_list.add_text` 手绘文本
