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



## v0.3-test8b 构建同步说明

- test8a 的 `build.bat` 最后使用 `where /r` 递归扫描 Visual Studio，实机误选到 `VC\Tools\Llvm\ARM64\bin\clang.exe`，在 x64 Windows 上出现“映像文件无效 / 对另一种计算机类型有效”。该方案已判定失败。
- test8b 将编译器搜索精简为三层：PATH → `%ProgramFiles%\LLVM\bin` → `vswhere` 最新 Visual Studio 的 `VC\Tools\Llvm\x64\bin`；`clang.exe` 与 `lld-link.exe` 必须来自同一目录。
- 不再支持递归扫描、`LLVM_HOME`、`LLVM_PATH` 或任意 ARM64 LLVM 回退。找到工具后先运行 `--version`，无法在当前宿主执行时立即停止。
- `verify_build.py` 新增 build.bat 自检：禁止 `where /r`、禁止 ARM64 LLVM 路径、要求存在 x64 VS LLVM 回退，并检查 UTF-8 BOM + CRLF 以及所有有正文的 `REM` / `echo` 行末尾两个半角空格。
- test8b 后续实机日志已经确认 INI 第一节修复成立：Steam 与非 Steam 都能正确读取 `BaseHeight=720` 并计算 `1280x720`；这一项不再是阻塞。

## v0.3-test9 Steam 布局补充（历史失败）

Steam 与非 Steam 使用同一 fixed-Y / auto-X 公式，第一次 HUD 布局也实测相同。Steam 异常不是 `centerDelta` 公式不同，而是 ComeOn.dll 环境缺少非 Steam 随后自然发生的第二阶段顶层 UI 尺寸应用。

test9 因此保持世界分辨率、JMM 模板选择和底部 HUD 居中算法完全不变，只在 Steam 条件满足后补一次顶层 `vtable+0x14(0,W,H)` 广播。非 Steam 不执行。

主界面固定 640x480 的 4:3 等比放大居中仍属于另一套前端分辨率系统，test9 没有处理。


## v0.3-test10 Steam 布局补充

test9 实机确认“只广播顶层 `vtable+0x14(0,W,H)`”不会改变 0x0B/0x0E 以及其它 HUD 的第二阶段位置，即使 34 个对象都成功调用也无效。

重新反汇编 `0x004B35F0` 后确认，真正的第二阶段顺序是：先通过 `0x004EB9E0` 取得资源根并拼出 `mb\JMMDL*.txt`，由 `0x004D0500` 重新加载对应 JMM 布局资源，之后才广播 `vtable+0x14`。因此 Steam 的最终布局差异本质上是“缺少一次完整 JMM 应用”，不是单纯缺少 width/height 广播。

test10 只在 Steam 环境、主 HUD/顶层 UI/资源根目录都成熟后，补调用一次原版 `0x004B35F0(0,TargetWidth,TargetHeight)`；非 Steam 不执行。主界面固定 640x480 的 4:3 等比放大居中仍属于另一套前端系统，本轮继续不处理。
