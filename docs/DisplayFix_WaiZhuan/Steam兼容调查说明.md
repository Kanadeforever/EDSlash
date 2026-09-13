# Steam 兼容调查说明

本文件只登记，v0.1-test1 不处理 Steam 专项。

当前用户实机结论：

- Steam 版 + cnc-ddraw D3D9：可运行；
- Steam 版 + cnc-ddraw OpenGL：开场动画结束后闪退。

Steam 包中存在额外 `ComeOn.dll`，并改变启动加载链。后续非 Steam 稳定后，需要重点检查：

1. `ComeOn.dll` 的 DirectShow 播放接管；
2. 动画结束时窗口和 DirectDraw Surface 的恢复顺序；
3. cnc-ddraw OpenGL 后端在动画结束/显示模式切换时的资源重建；
4. 是否存在重复 Hook 或设备生命周期冲突；
5. Steam GUI/JMM 时序是否也像本传 Steam 版一样存在差异。

当前 ASI 检测到 `ComeOn.dll` 时，会明确记录 Steam 环境，但不会启用本传继承来的 Steam delayed JMM 特殊逻辑。
