# 宽高比与 HUD 研究说明

## fixed-Y / auto-X

目标宽度：

`TargetWidth = round(BaseHeight × AspectWidth / AspectHeight)`

并向上调整为偶数宽度。

推荐：

- 480 + 16:9 → 854×480
- 600 + 16:9 → 1068×600

BaseHeight 越高，真正看到的世界范围越大，内部 Surface 和对象处理负担也越高。因此 1080 这类值是实际内部世界放大，不只是“输出 1080p”。

## HUD

外传主 HUD 的构造、vtable、通用布局结构已经与本传 v0.3 对应：

- 构造 `0x004D70A3`
- vtable `0x00553B34`
- 布局 `vtable+0x58 -> 0x004C5F00`

当前插件只移动底部主 HUD 根节点；小地图和边缘顶层 UI 继续贴边。

v0.1-test1 用户实机确认：

- 游戏内 GUI 居中正常；
- 右侧 6 个菜单按钮正常；
- Steam 日志显示 0x0B / 0x0E 的 world-press 防穿透与 global-release 路径正常触发。

因此 HUD / 输入迁移已经从“静态闭合”升级为“实机通过基线”。v0.1-test2 不修改这些路径，只修配置开关之间的耦合。
