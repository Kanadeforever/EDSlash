# 宽高比、世界视野与 HUD 研究说明

## 设计原则

DisplayFix 不是只支持若干“宽屏分辨率”，而是固定逻辑 Y、根据输出宽高比自动求 X：

```text
TargetHeight = BaseHeight
TargetWidth = round(BaseHeight * AspectWidth / AspectHeight)
```

因此 5:4、4:3、3:2、16:10、16:9、21:9、32:9 都是同一套算法。

## BaseHeight 的含义

游戏本身是“逻辑分辨率越高，看到的世界越多”，所以 BaseHeight 也是类 FOV / Zoom 参数。

最初围绕 480 与 600，是因为原版有 640x480 与 800x600 两个正式档位；现在用户可自由填写正整数。

## GUI 两套模板

已确认：

- `JMMDL.txt`：480 系 / 640 参考宽
- `JMMDL800.txt`：600 系 / 800 参考宽

DisplayFix 当前：

- BaseHeight < 600 -> NativeBaseWidth=640
- BaseHeight >= 600 -> NativeBaseWidth=800

世界高度仍然保持用户设定值。

## 底部 HUD 居中

只移动底部主 HUD 根对象：

```text
MainHUDCenterDelta = (TargetWidth - NativeBaseWidth) / 2
```

小地图、右侧按钮等边缘 UI 保持原锚点，这是设计目标。

极少数窄于 4:3 的比例允许主 HUD 少量裁切，不做缩放或复杂重排。

## 主界面与游戏内是两套系统

原始主界面固定逻辑 640x480；进入游戏后才真正接受游戏内分辨率设置。因此未来不能把主界面跟 BaseHeight 一起扩展。

目标主界面行为：

- 保持 640x480 / 4:3 内容；
- 按当前输出窗口等比放大到能完整容纳的最大尺寸；
- 居中；
- 不拉伸；
- 多余区域留黑。

当前尚未实现。

## 已知输入特殊性

主 HUD 大部分按钮随根布局移动后命中正常，但 0x0B / 0x0E 两个顶部按钮的 UI 路由特殊，需要 DisplayFix 在全局 release 层补窗口动作；点击穿透则在真正世界输入 `0x473F10` callsite 上单独拦截。详见《逆向工程知识库.md》。

## v0.3-test8a 同步说明

- `v0.3-test8` 实机确认已经修复 test7 的普通地图左键 / Alt+F4 回归，并保留属性、道具两个顶部按钮响应；当前输入实现继续沿用 test8。
- test8 日志显示用户把 `BaseHeight` 改为 1080 后插件仍读取成 480。复核发行 INI 后确认 `[Display]` 原本正好位于 UTF-8 BOM 后的第一行；Win32 `GetPrivateProfile*A` 对 UTF-8 BOM 不可靠，第一节可能读不到，于是 `BaseHeight` 静默使用默认 480。test8a 将 INI 改为 UTF-8 无 BOM，并在 `[Display]` 前增加 ASCII 保护行，使以后编辑器即使重新加入 BOM，也不会再破坏第一节。
- test8a 日志新增 `ConfigPath=`，用于直接确认插件实际读取的是哪一份 `DisplayFix.ini`。
- `build.bat` 已按仓库位置重新整理：同时自动查找 PATH、`LLVM_HOME`、`LLVM_PATH`、`Program Files\LLVM` 和 Visual Studio 自带 LLVM，兼容本地与 Windows GitHub Actions；发行目录仍严格只有 ASI 与 INI。
- 主界面固定 640x480 的等比放大居中仍是后续任务，本轮没有处理。

