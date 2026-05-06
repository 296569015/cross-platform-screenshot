---
feature_ids: [F001]
topics: [ui, ux, screenshot, annotation, design-system]
doc_kind: design
created: 2026-05-06
status: proposal
---

# UI Redesign V1: Focus Frame

## 定位

这套 UI 面向一个“启动快、干扰少、标注准”的截图工具。它不是完整桌面软件窗口，而是叠在屏幕上的临时专业工作台：默认隐藏，热键出现，完成后立刻消失。

参考方向：PixPin 的效率、Snipaste 的低干扰、QQ 截图的易理解。但视觉上更现代，减少黑灰塑料感，让工具栏像一块精密的玻璃控制条。

## 视觉关键词

- **Focus**：选区是主角，UI 只作为边缘工具。
- **Glass Utility**：半透明深色工具栏，轻微描边和阴影，贴近系统级浮层。
- **Precise Craft**：像素尺寸、十字线、控制点、颜色和线宽反馈要明确。
- **Fast Commit**：复制/确认是绿色主动作，取消是红色危险动作，保存是中性动作。

## 设计产物

- 高保真原型：[ui-redesign-v1.html](ui-redesign-v1.html)
- 应用图标：[assets/app-icon.svg](assets/app-icon.svg)

## 信息架构

### 1. 截图选择态

- 全屏冻结背景。
- 选区外 66% 深色遮罩。
- 选区边框为 cyan 主色，四角短线加强边界感。
- 选区左上浮动尺寸标签：`832 x 468`。
- 鼠标附近可扩展放大镜：7x7 像素格、中心像素高亮、坐标显示。

### 2. 标注态

工具栏分为三组：

- **绘制工具**：矩形、箭头、直线、画笔、文字。
- **样式工具**：颜色、线宽。
- **操作工具**：长截图、撤销、保存、复制、取消、完成。

建议把原来的 9 个等权按钮改成“主工具 + 样式胶囊 + 操作区”，降低误触。

### 3. 长截图态

- 源区域保持高亮，边框改为 coral，提示当前是滚动采集。
- 左侧出现竖向缩略图轨道，显示已拼接长图和当前视窗位置。
- 顶部或底部浮动轻提示：`滚动页面继续采集`。
- 工具栏变为长截图专用：编辑、自动滚动、保存、取消、完成。

## Design Tokens

| Token | Value | 用途 |
|---|---:|---|
| `overlay.dim` | `rgba(4, 8, 12, 0.66)` | 选区外遮罩 |
| `surface.glass` | `rgba(18, 24, 31, 0.82)` | 主工具栏 |
| `surface.glass.light` | `rgba(246, 249, 250, 0.94)` | 长截图浅色工具栏 |
| `stroke.hairline` | `rgba(255,255,255,0.12)` | 玻璃描边 |
| `accent.capture` | `#38BDF8` | 选区、激活态 |
| `accent.capture.strong` | `#5EEAD4` | 高亮细节 |
| `accent.annotation` | `#FF5C5C` | 默认标注色 |
| `accent.success` | `#22C55E` | 完成/复制 |
| `accent.warning` | `#F59E0B` | 长截图/状态 |
| `text.primary` | `#F7FAFC` | 深色浮层文字 |
| `text.secondary` | `#A7B0BC` | 辅助文字 |
| `radius.toolbar` | `8px` | 工具栏 |
| `radius.control` | `6px` | 按钮/标签 |
| `shadow.float` | `0 18px 48px rgba(0,0,0,.35)` | 浮层阴影 |

## 图标体系

图标使用 20px 线性图标，2px 线宽，圆端点。实现时可继续用 GLES/GDI 几何绘制，也可以加入一个小型 icon path 表。

必需图标：

- 选框：矩形四角。
- 箭头：斜向箭头。
- 直线：斜线。
- 画笔：压感曲线。
- 文字：`T` 或文本光标。
- 颜色：实心圆点/色板。
- 线宽：三条不同粗细的线。
- 长截图：向下箭头穿过分页。
- 自动滚动：播放/暂停双态。
- 撤销：回转箭头。
- 保存：下载到托盘。
- 复制：叠放矩形。
- 取消：X。
- 完成：check。

## 实现建议

1. 先只替换颜色、尺寸、工具栏分组和图标，不改截图/标注逻辑。
2. `ShapeRenderer` 增加 rounded rect 和 soft shadow 的近似绘制；当前直角矩形是“挫感”的主要来源。
3. `ToolButton` 增加 `group`、`label`、`isPrimary`、`isDanger`，便于绘制分隔和状态。
4. 增加 `TextRenderer` 后再落地尺寸标签、提示文字、颜色/线宽面板。
5. 软件 GDI 和 GLES 两条渲染路径要共享同一套 tokens，避免两边视觉漂移。

## 验收标准

- 选区、工具栏、尺寸标签在 100%、125%、150% DPI 下不重叠。
- 32px 和 40px 图标按钮都能在截图背景上清晰识别。
- 复制/保存/完成/取消四个动作颜色语义明确。
- 长截图态用户无需读文档即可知道“滚动继续、点击完成”。
- 所有控件都可以用现有平台自绘能力渐进实现。
