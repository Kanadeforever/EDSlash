# 宽高比与 HUD 研究说明

## fixed-Y / auto-X

目标宽度：

```text
TargetWidth = round(BaseHeight × AspectWidth / AspectHeight)
```

并向上调整为偶数宽度。

典型值：

- 480 + 16:9 → 854×480
- 600 + 16:9 → 1068×600

`BaseHeight>600` 会真正扩大内部世界/FOV与 Surface，不只是提高最终输出像素，因此可能明显降低性能。

## HUD 已确认结构

- 主 HUD 构造 `0x004D70A3`
- vtable `0x00553B34`
- layout `vtable+0x58 -> 0x004C5F00`
- active query `0x004C5100`
- world press `0x0040CFE6 -> 0x00482790`
- global release `0x0040CF80 -> 0x004C7930`
- 顶部真实 control ID：`0x0B / 0x0E`

当前插件只移动底部主 HUD 根节点；小地图和边缘顶层 UI 保持贴边。用户在 v0.1-test1 已实机确认 GUI/HUD 居中、右侧 6 个菜单按钮和返回标题行为正常。

## clean1 边界

clean1 的 HUD、输入、Strategy、JMM、fixed-Y 分辨率代码完全回到 test2。唯一新增运行逻辑是 Steam `ResJM.Lib` 文件打开兜底，与 HUD/宽屏无关。

因此如果 clean1 出现 HUD/输入回归，应视为打包/移植错误，而不是 Steam OpenGL 调查的预期变化。
