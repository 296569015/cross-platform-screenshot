---
feature_ids: [F001]
topics: [screenshot, capture, annotation, mvp]
doc_kind: feature
created: 2026-04-02
status: in-progress
---

# F001: 截图工具 MVP — 截图 + 标注 + 保存

## 愿景

基于 C++ 和 OpenGL ES 的跨平台截图工具，对标 PixPin/QQ截图/Snipaste。
先做 Windows 版本，架构上保证平台抽象，为后续 macOS/Linux 扩展做好准备。

## 验收标准 (Acceptance Criteria)

### P0 — 必须达成
- [ ] AC1: 全局快捷键触发截图（默认 Ctrl+Shift+A）
- [ ] AC2: 全屏冻结 + 区域选择（拖拽框选）
- [ ] AC3: 选区外暗化（60-70% 透明度）
- [ ] AC4: 窗口识别 + 鼠标悬停高亮 + 单击吸附
- [ ] AC5: 标注工具 — 矩形（填充/描边）
- [ ] AC6: 标注工具 — 箭头
- [ ] AC7: 标注工具 — 文字输入
- [ ] AC8: 撤销/重做（Ctrl+Z / Ctrl+Shift+Z）
- [ ] AC9: 复制到剪贴板（CF_DIB + PNG 格式）
- [ ] AC10: 保存到文件（PNG，通过文件对话框）
- [ ] AC11: 系统托盘常驻 + 右键菜单

### P1 — 应该达成
- [ ] AC12: 十字线准星 + 像素坐标显示
- [ ] AC13: 放大镜（光标附近 7x7 像素网格）
- [ ] AC14: 选区尺寸标签（W x H）
- [ ] AC15: 颜色选择器（8-12 预设色）
- [ ] AC16: 线条粗细调节

### P2 — 可以延后
- [ ] AC17: 椭圆标注
- [ ] AC18: 自由画笔
- [ ] AC19: 马赛克/高斯模糊
- [ ] AC20: 序号标注（步骤标记）

## 技术决策

| 决策 | 选型 | 理由 |
|------|------|------|
| 屏幕捕获 | DXGI Desktop Duplication | GPU 纹理直出，零 CPU 拷贝 |
| GL 运行时 | ANGLE (D3D11 backend) | Chrome 同款，D3D11 纹理零拷贝互通 |
| UI 框架 | 纯 OpenGL ES 自绘 | 跨平台一致性 |
| 文字渲染 | SDF 字体 (stb_truetype) | 分辨率无关，GPU 友好 |
| 图片编码 | stb_image_write | 最小依赖 |
| 构建系统 | CMake + vcpkg | C++ 行业标准 |
| 最低兼容 | Windows 10+ | 可使用全部现代 API |

## 架构约束

1. **平台抽象**：所有平台特定代码隔离在 `src/platform/{os}/` 下，通过 `platform_interfaces` 抽象层暴露。应用层和业务层零平台代码。
2. **零拷贝管线**：DXGI → 共享 D3D11 Device → ANGLE EGL 导入 → GL 纹理。
3. **覆盖层策略**：不用真透明窗口，全屏不透明窗口显示冻结截图模拟透明。

## 实施阶段

1. Phase 1: 项目骨架 + ANGLE 集成 + 截屏显示
2. Phase 2: 区域选择覆盖层（暗化 + 选区）
3. Phase 3: 标注引擎（矩形/箭头/文字 + 撤销重做）
4. Phase 4: 工具栏 UI（自绘按钮/颜色/粗细）
5. Phase 5: 保存/剪贴板/快捷键集成
6. Phase 6: 窗口识别 + 智能吸附
7. Phase 7: 打磨（多显示器 DPI、动画、错误处理）

## 关联

- ADR-001: ANGLE over native GL
- ADR-002: Zero-copy pipeline
- ADR-003: SDF text rendering
- UX 设计稿: 待 @gemini 交付
