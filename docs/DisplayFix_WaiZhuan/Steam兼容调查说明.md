# Steam 兼容调查说明

Steam 仍不是当前非 Steam 主线的正式目标，但 v0.1-test1 用户顺带测试得到了新的实机信息。

## 当前实机结论

- Steam 版 + cnc-ddraw D3D9：可运行；
- Steam 版上标题 4:3、进入游戏宽屏、GUI/HUD 居中、右侧 6 个菜单按钮、高 DPI 字体等绝大多数 DisplayFix 功能正常；
- 返回主菜单可恢复 4:3；
- Steam 版 + cnc-ddraw OpenGL：开场动画结束后闪退。

## test1 日志发现

启动日志最初会识别 `ComeOn.dll`，而成熟 HUD 阶段的 late-detection 又会把 Steam 环境重新置为有效，并实际进入继承自本传的 delayed JMM 路径：

```text
[RUNTIME] Steam environment detected late: ComeOn.dll is now loaded
[RUNTIME] Steam delayed JMM apply begin ...
[RUNTIME] Steam delayed JMM apply end result=1 done=1 ...
```

用户实机没有发现这条路径破坏当前功能，所以 v0.1-test2 不为了配置修复去改变 Steam 运行行为。它仍属于“暂时可用但未正式专项验证”的状态。

## 后续 OpenGL 闪退调查重点

Steam 包中存在额外 `ComeOn.dll`，并改变启动 / 多媒体链。非 Steam 主线稳定后，重点检查：

1. `ComeOn.dll` 的 DirectShow 播放接管；
2. 开场动画结束时窗口和 DirectDraw Surface 的恢复顺序；
3. cnc-ddraw OpenGL 后端在动画结束 / 显示模式切换时的资源重建；
4. 是否存在重复 Hook 或设备生命周期冲突；
5. Steam GUI/JMM 时序是否还有外传独有差异。

当前不因为 OpenGL 闪退去改变非 Steam 主线。
