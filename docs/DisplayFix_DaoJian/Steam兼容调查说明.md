# Steam 兼容调查说明（截至 v0.3.2）

## 2026-09-14 日志中文化同步

- 当前正式源码版本：`v0.3.2`；上一稳定逻辑基线：`v0.3.1`。
- 本轮只把 DisplayFix.log 全面改为简体中文；v0.3.1 已实机稳定的 Strategy 生命周期、宽屏、HUD、输入、Steam delayed JMM、96-DPI 与配置解耦逻辑全部不变。
- `DisplayFix.log` 现统一使用 `[成功] / [信息] / [警告] / [失败] / [运行]`；关键技术名、地址、配置键仍保留英文，方便与逆向记录对应。
- 日志以 UTF-8 无 BOM 写出；`build.bat` 显式使用 `-finput-charset=UTF-8 -fexec-charset=UTF-8`，避免受 Windows ANSI 代码页影响。
- 本文后续如保留 `[OK]/[INFO]/[WARN]/[FAIL]/[RUNTIME]`，均属于历史版本的原始实机证据，不代表当前版本仍输出英文。

当前日志示例：

```text
[成功] 动态分辨率代码点已解析；启动/前端代码保持原样
[信息] 已检测到Steam/ComeOn.dll环境
[运行] Strategy进入 原版应用完成 实际=854x480 目标=854x480 强制=1
```


## v0.3.1 同步说明

v0.3.1 只调整 `FixDPI` 与 `Display.Enable` 的初始化顺序，不修改 Steam delayed full JMM apply、Strategy gate、HUD 或输入链。因此 Steam test10 与 v0.3/test15 的所有既有结论保持不变。

---

## v0.3 封版生命周期结论（Steam test10 稳定路径不变）
Steam 专项的稳定结论仍然是 test10：`ComeOn.dll` 环境缺少非 Steam 自然发生的后续完整 JMM apply，因此在 HUD、顶层 UI 与资源根成熟后 one-shot 调用原版 `0x004B35F0(0,W,H)`。

从 test11 到 test15 修改的都是**什么时候进入/离开 GAMEPLAY profile**，不是重新设计 Steam JMM：

- test11 HUD gate：失败；
- test12 world gate：失败；
- test13 Strategy gate：进入时机正确；
- test14 force=1：实机确认进入游戏 live 能真正达到目标；
- test15：Strategy exit 后恢复原版 mode 4 Surface；**BaseHeight=480 实机通过，v0.3 以此封版**。

`g_strategy_transition_in_progress` 会在进入/退出显示设备重建调用栈中禁止 HUD 平移和 Steam delayed JMM，避免对半析构/半重建 UI 操作。真正进入 GAMEPLAY 后仍沿用 test10 one-shot。

注意：test10 历史通过样本曾得到 `child_layout_changed=1`；在较低 BaseHeight 或已经处于正确坐标时，完整 JMM apply 也可能返回成功但 before/after 不变。判断重点应是 `result=1 / done=1` 与最终 GUI 正确，而不是把 `child_layout_changed=1` 当所有配置的硬性条件。

---

## test11 基线正文（历史保留，当前结论以上方增补为准）

## 文件关系

Steam `ComeOnSteam.exe` 与非 Steam `ComeOn.exe` 大小相同、主体代码几乎一致。Steam EXE 只加入一个很小的启动桩：

```text
LoadLibraryA("ComeOn.dll")
-> 恢复现场
-> 继续原版启动函数
```

已记录指纹：

- 非 Steam `ComeOn.exe`：`c8d1aa33272a2c28e94f0d18eda2d6a8fcb14796644d8f8bed467b744ddc94b5`
- Steam `ComeOnSteam.exe`：`0887ceae7589999a389ec271d690e1c55204d59575f8f2adb5ef46a1e605b3f5`
- `ComeOn.dll`：`9f8f71ad958c5c5d33e1b5aeb78603b970fd16d7e0b674aa7629d497ae96e2d0`

DisplayFix 不用这些 SHA-256 锁兼容性；它们只作为研究样本指纹。运行时修改仍以机器码/上下文签名判断。

## ComeOn.dll 当前静态结论

确认包含 Steam API 初始化、额外线程、窗口/消息/GDI 相关 API、模式扫描和进程内写内存能力。`QueryPerformanceCounter/GetTickCount` 的已追踪引用属于 CRT/security-cookie 初始化，目前没有证据证明它们直接构成帧率限制器。

Steam 版性能/游戏速度偏低仍是独立待办，不能在没有证据时直接 patch ComeOn.dll 计时器。

## Steam GUI 根因：缺失后续完整 JMM apply

`v0.3-test8b` Steam/非 Steam 对照日志确认：

- 两边第一次 HUD 布局一致；
- 非 Steam 随后自然发生第二阶段 JMM/UI 应用；
- Steam/ComeOn.dll 环境缺少这一阶段，因此停留在错误位置。

### test9：只广播 `vtable+0x14`，失败

Steam test9：`visited=34 / applied=34 / done=1`，但 0x0B/0x0E before/after 坐标不变，GUI 仍错误。证明第二阶段不是单纯尺寸广播。

### 0x004B35F0 完整语义

```text
0x004B35F0
 -> 0x004EB9E0：取得资源根并拼 mb -> 0x004B363B：按 width 选择 JMMDL.txt / JMMDL800.txt
 -> 0x004D0500：真正加载 JMM
 -> manager+0x1C：遍历顶层 UI
 -> vtable+0x14(mode,width,height)
```

资源根缓冲区可从 `0x004EB9E0` 的 `BF imm32` 解析；当前样本为 `0x00560168`。

### test4：完整重放太早，失败

ASI 初始化阶段直接调用 `0x004B35F0` 会弹 `MB\JMMDL.txt / MB\JMMDL800.txt`。说明完整入口可以复用，但调用时机必须等资源根与 UI 生命周期成熟。

### test10：延迟完整原版 JMM apply，实机通过

触发条件：

1. 进程已加载 `ComeOn.dll`；
2. 主 HUD 的真实 0x0B/0x0E child 已建立；
3. `manager+0x1C` 顶层 UI 链非空；
4. 游戏资源根字符串非空；
5. one-shot 尚未完成。

然后只调用一次原版：

```text
0x004B35F0(mode=0, TargetWidth, TargetHeight)
```

用户 Steam 实机 `BaseHeight=1080` 已确认：

```text
before 0x0B: 1132,993...
before 0x0E: 1160,994...
[RUNTIME] Steam delayed JMM apply end result=1 done=1 child_layout_changed=1
after 0x0B: 1288,992...
after 0x0E: 1316,993...
```

截图中 GUI 位置正确，与非 Steam 第二阶段一致。因此 **v0.3-test10 是 Steam 游戏内 GUI 稳定基线**。

## v0.3-test11 与 Steam

本版新增的是前端/动画与游戏内动态分辨率生命周期分离，不替换 test10 的 Steam JMM 修复。

进入游戏时：

1. 主 HUD 出现；
2. DisplayFix 切 GAMEPLAY profile；
3. 若 live display 仍是前端尺寸，复用原版 `0x00404D30` 重应用当前模式；
4. 重建后的 Steam HUD 成熟后，再执行 test10 delayed full JMM apply。

代码通过 `g_gameplay_mode_switch_in_progress` / `g_steam_ui_sync_in_progress` 防止两套重建互相递归。

## 当前 Steam 状态与后续项

- v0.3 的两阶段生命周期已在 Steam/ComeOn.dll 环境、`BaseHeight=480` 实机闭合：进入 `854x480`，退出强制回原版 `640x480`；
- test10 游戏内 GUI 路径保持有效；本次 480 日志为 `result=1 / done=1 / child_layout_changed=0`，用户确认整体可用，因此 `child_layout_changed` 不作为硬性通过条件；
- Steam 帧数/游戏速度偏低仍是独立后续专项，v0.3 不在没有证据时 patch ComeOn.dll 计时器；
- `AspectRatio=Auto` 仍读取显示器比例，不跟随 cnc-ddraw 自定义客户区，继续暂缓。
