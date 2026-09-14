# Steam兼容调查说明

## 当前状态：v0.2.0 正式封版

`v0.2.0` 以 `v0.1-clean1` 为稳定基线：test2 的显示/HUD/DPI/输入主线不动，test4 已实机通过的 `ResJM.Lib` 多语言最终兜底继续保留；OpenGL 只保留 test3 已实机闭环的 `CreateWindowExA` class-atom guard；test3~test9 的 DirectShow、ActiveMovie、MovieManager、teardown、worker/export 影片实验仍然不回到运行路径。

本轮是在 `v0.2-test1` **实机成功启动 OpenGL** 之后做的范围收缩。test1 的关键日志是：

```text
[WARN] Steam CreateWindowExA hook object not found; ComeOn.dll post-create callback is neutralized only
[INFO] OpenGL A/B is partial in this run; global USER32 hook still exists
```

但用户同时确认 OpenGL 已经成功启动。这个结果非常重要：**test1 并没有绕过全局 `CreateWindowExA` Hook，真正生效的是 callback 后处理 neutralize。** 因而全局 Hook 本身不再是最高嫌疑，根因进一步收敛到 callback 针对 `EDIT` 窗口执行的特殊处理。

## 当前已确认

- 非 Steam 主线在 `v0.1-test1` 已实机通过：标题/主菜单 4:3、进入 Strategy 后 fixed-Y 宽屏、GUI/HUD 居中、右侧 6 个菜单按钮、高 DPI 字体、退出 Strategy 后恢复 4:3；
- `v0.1-test2` 修复 `Display.Enable=0` 时错误连带关闭 `Font.FixDPI=1`；
- `v0.1-test4` 的窄 CreateFileA callsite shim 已实机确认解决裸 `ResJM.Lib` 缺失弹窗；
- 删除 `ComeOn.dll` 后 Steam 数据可用 cnc-ddraw OpenGL 启动，说明 DLL 是 OpenGL 冲突的必要条件之一；
- `v0.2-test1` 在**全局 CreateWindowExA Hook 仍存在**的情况下，仅 neutralize callback 后处理就能让 OpenGL 启动；
- `v0.2-test2` 进一步证明：只禁 EDIT WndProc 子类化仍会崩，因此 WndProc 本身不是唯一根因；问题位于 callback 更早的 class 参数处理。
- `v0.2-test3` 最终实机成功，进入游戏后记录 `class-atom guard hit count=3 last_atom=0x0000C1F2`，因此 ComeOn.dll 把合法 `MAKEINTATOM` 当字符串解引用的根因已经动态闭环；
- `v0.2.0` 原样固化该最小 guard，并把 OpenGL 问题视为已解决。

## ComeOn.dll 的两条已确认 Hook

初始化区约 `RVA 0x5310`：

- `RVA 0x54E8~0x54F9`：Hook 游戏 `CreateFileA`，callback `RVA 0x3E30`，覆盖长度 7；用于 `.lib` 多语言，保留；
- `RVA 0x54FE~0x550D`：Hook `USER32!CreateWindowExA`，callback `RVA 0x2830`，覆盖长度 12；test2 **保留这个全局 Hook**。

### CreateWindowExA callback

callback `RVA 0x2830` 先调用原始 `CreateWindowExA`，然后：

1. 保存新建窗口返回值；
2. 比较 class 名是否精确等于 `"EDIT"`；
3. 对 EDIT 窗口把 HWND 写入 `RVA 0x1F6A8`；
4. 如果旧 WndProc 全局 `RVA 0x1F6A4` 为 0，则调用 `SetWindowLongA(hwnd, GWL_WNDPROC=-4, ComeOn.dll+0x26A0)`；
5. 保存 `SetWindowLongA` 返回的旧 WndProc。

关键地址：

```text
callback                 RVA 0x2830
post-create 起点         RVA 0x2841
EDIT class 字符串        RVA 0x15208
保存 EDIT HWND           RVA 0x287E
旧 WndProc 判定分支      RVA 0x2889   原字节 75 16
自定义 WndProc 地址      RVA 0x26A0
SetWindowLongA 调用      RVA 0x2896
旧 WndProc 全局          RVA 0x1F6A4
EDIT HWND 全局           RVA 0x1F6A8
```

## v0.2-test2：失败的过度收缩（历史）

本轮不再扫描 ComeOn Hook 动态对象，不再修改 `USER32!CreateWindowExA` 入口，不再让整个 callback 提前返回，也不再主动恢复旧 WndProc。

唯一运行时改动是：

```text
ComeOn.dll + RVA 0x2889
75 16    JNE +0x16
   ↓
EB 16    JMP +0x16
```

原逻辑只有在“旧 WndProc 已经存在”时跳过子类化；改成无条件跳转后，每次 EDIT 窗口仍照常创建、class 判断和 HWND 记录，但**不会执行 SetWindowLongA 把 WndProc 换成 ComeOn.dll+0x26A0**。

用户实机确认 test2 仍然崩溃。因此“只禁 EDIT WndProc 子类化”不足以修复 OpenGL，必须继续检查 callback 在 SetWindowLongA 之前的路径。

## 多语言 `.lib` 机制：已确认并保留

Steam `ComeOn.dll`：

- `local_config` VA `0x10015394`；
- `current_language` VA `0x100153B0`；
- 默认 `chs` VA `0x100153D4`；
- 配置初始化约 `0x10005310`；
- `.lib` 打开 Hook 约 `0x10003E30`。

Steam 目录使用 `ResJM.Lib.chs/.cht/.eng/.jpn`。DisplayFix 继续保留 test4 已实机通过的最终兜底：不改 CreateFileA IAT `0x005511E4`，只改游戏低层 CALL `0x0052824E`；仅 basename 精确为 `ResJM.Lib` 时按 `ComeOn.ini` 当前语言尝试带后缀文件。

## test3~test9 影片/OpenGL失败历史

- test3：仅禁 delayed full JMM，语言错误仍在；
- test4：ResJM 兜底成功；影片 COM 4:3/child 实验失败；
- test5：ActiveMovie HWND watchdog 未解决；
- test6：MovieManager/export A/B 未解决；
- test7：teardown 固定绝对地址签名受 ASLR 影响，实际未安装；
- test8：RVA-safe teardown bypass 真正安装但 OpenGL 仍崩；
- test9：游戏进程内硬隔离 DirectShow worker/export 仍不能解决。

这些代码都不在 v0.2.0 的当前运行路径。

## PlugK 使用边界

用户已实机确认 PlugK 不解决 Steam + cnc-ddraw OpenGL 崩溃。后续只利用其逆向文档帮助核对 Steam 2.01 地址、启动链或结构；不采用其运行时代码作为 OpenGL 兼容方案，不把“支持 Steam”视为 OpenGL 兼容证据。

## 当前阻塞项

OpenGL 兼容问题已经闭环，没有遗留阻塞项：用户实机确认 `v0.2-test3` 可以启动并进入/退出游戏，且日志记录 `class-atom guard hit count=3 last_atom=0x0000C1F2`。因此正式版固定保留该 guard。

Steam 官方 launcher 控制的开场动画不属于 DisplayFix 主程序修复范围，本项目不再跟进其比例/播放行为。其余长期红线仍是：已通过的宽屏/HUD/字体/ResJM 多语言不得回归。


## v0.2-test3 → v0.2.0：CreateWindowExA class-atom 正式兼容修复

`v0.2-test2` 的失败使范围进一步收缩：test1 整段跳过 post-create callback 时 OpenGL 成功，而 test2 只跳过 `SetWindowLongA` 时仍崩，说明危险点发生在 WndProc 子类化之前。重新逐条检查 `RVA 0x2841~0x287C` 后确认，callback 会从 hook context `+0x2C` 取出 `lpClassName`，只检查 NULL，然后从 `0x2854` 开始直接按 C 字符串解引用。

Win32 的 `CreateWindowExA` 明确允许 `lpClassName` 使用 `MAKEINTATOM(atom)`：高 16 位为 0、低 16 位保存 class atom。这样的参数是合法值，但并不是字符串指针。ComeOn.dll 没有做这个 API 语义判断，因此一旦兼容层/渲染器用 atom 创建窗口，就可能把 `0x0000xxxx` 当地址读取。

本轮只在 `RVA 0x2841` 前置 guard：NULL 或 `<0x10000` 的 atom 直接走 callback 原收尾；普通字符串类名仍从 `RVA 0x284E` 回到官方 `"EDIT"` 比较、HWND 记录与 `SetWindowLongA` 子类化。全局 `CreateWindowExA` Hook、SteamAPI、多语言和正常 EDIT 功能全部保留。


## 封版实机证据

最终 A/B 链：test1 整段 neutralize callback 后 OpenGL 成功；test2 仅禁 EDIT WndProc 仍崩；test3 仅增加 NULL/MAKEINTATOM guard 后成功。进入游戏日志又记录到 `count=3`、`last_atom=0x0000C1F2`。因此可正式定性：ComeOn.dll 的 post-create callback 把合法 class atom 当作 `char*` 解引用，是 cnc-ddraw OpenGL 启动崩溃的根因。
