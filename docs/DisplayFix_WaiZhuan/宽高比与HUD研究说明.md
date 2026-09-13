# 宽高比与 HUD 研究说明

## fixed-Y / auto-X

目标宽度使用：

`TargetWidth = round(BaseHeight × AspectWidth / AspectHeight)`

并向上调整为偶数宽度。

推荐：

- 480 + 16:9 → 854×480
- 600 + 16:9 → 1068×600

BaseHeight 越高，真正看到的世界范围越大，内部 Surface 和对象处理负担也越高。

## HUD

外传主 HUD 的构造、vtable、通用布局结构已经与本传 v0.3 对应上：

- 构造 `0x004D70A3`
- vtable `0x00553B34`
- 布局 `vtable+0x58 -> 0x004C5F00`

当前插件继承本传稳定算法：只移动底部主 HUD 根节点，边缘顶层 UI 不整体平移。

顶部按钮 0x0B / 0x0E 的事件函数、目标窗口全局槽和 world/release callsite 也已完成静态验证，但外传尚未实机测试；因此第一轮重点观察点击位置、角色移动是否被误触发，以及 Alt+F4 等系统输入是否有回归。
