# 宽高比与 HUD 研究说明

## 2026-09-14 日志中文化同步

- 当前正式源码版本：`v0.2.1`；上一稳定逻辑基线：`v0.2.0`。
- 本轮只把 DisplayFix.log 全面改为简体中文；v0.2.0 已封版的非Steam显示/HUD/DPI/输入、Steam ResJM.Lib 多语言兜底、ComeOn.dll CreateWindowExA class-atom OpenGL 修复全部不变。
- `DisplayFix.log` 现统一使用 `[成功] / [信息] / [警告] / [失败] / [运行]`；关键技术名、地址、配置键仍保留英文，方便与逆向记录对应。
- 日志以 UTF-8 无 BOM 写出；`build.bat` 显式使用 `-finput-charset=UTF-8 -fexec-charset=UTF-8`，避免受 Windows ANSI 代码页影响。
- 本文后续如保留 `[OK]/[INFO]/[WARN]/[FAIL]/[RUNTIME]`，均属于历史版本的原始实机证据，不代表当前版本仍输出英文。

当前日志示例：

```text
[成功] Steam多语言 ResJM.Lib CreateFileA 兜底已安装
[成功] Steam ComeOn.dll CreateWindowExA 类Atom兼容修复已安装；官方EDIT处理保持完整
[运行] Steam CreateWindowExA 类Atom保护命中次数=3 最后Atom=0x0000C1F2
```


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

## clean1 / v0.2.0 边界

clean1 的 HUD、输入、Strategy、JMM、fixed-Y 分辨率代码完全保持不变。`v0.2-test1` 的 callback neutralize 已实机让 OpenGL 成功；`v0.2-test2` 只禁 EDIT WndProc 后仍崩；`v0.2-test3` 已实机闭环 class atom 根因；运行逻辑正式基线 `v0.2.0` 原样保留该修复；当前 `v0.2.1` 只做日志中文化，同样不改 HUD/分辨率链。

这项 Steam 专项不修改 HUD vtable、Strategy gate、JMM 选择器或宽高比算法。若出现 HUD/输入/Strategy 回归，应视为 ComeOn.dll EDIT 子类化依赖或打包错误，不能把它当成宽屏算法的预期变化。
