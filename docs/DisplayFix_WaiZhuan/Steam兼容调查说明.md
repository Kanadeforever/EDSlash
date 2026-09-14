# Steam兼容调查说明

## 当前状态：v0.1-clean1

当前代码基线已经回滚到 `v0.1-test2`，只单独移植 `v0.1-test4` 已由用户实机确认成功的 Steam `ResJM.Lib` 多语言最终兜底。`test3~test9` 的 DirectShow、ActiveMovie、MovieManager、teardown、worker/export 隔离等实验运行代码全部不在 clean1 中。

当前已确认：

- 非 Steam 主线在 `v0.1-test1` 已实机通过：标题/主菜单 4:3、进入 Strategy 后 fixed-Y 宽屏、GUI/HUD 居中、右侧 6 个菜单按钮、高 DPI 字体、退出 Strategy 后恢复 4:3；
- `v0.1-test2` 修复 `Display.Enable=0` 时错误连带关闭 `Font.FixDPI=1`；
- Steam 多语言目录只有 `ResJM.Lib.chs / .cht / .eng / .jpn`，没有裸 `ResJM.Lib`；
- `v0.1-test4` 的窄 CreateFileA callsite shim 已实机确认解决裸 `ResJM.Lib` 缺失弹窗；
- Steam + cnc-ddraw D3D9 可运行；Steam + cnc-ddraw OpenGL 仍会崩溃；
- Steam 开场动画仍存在比例拉伸问题。

## Steam 主程序与 ComeOn.dll

当前登记：

- Steam `ComeOn.exe` SHA-256：`97ae4c2350618f38a74c3d02bf315c749fc448f705e860129372ebd592ed66e5`
- Steam `ComeOn.dll` SHA-256：`5d5ac2b97d47726b18b42fd3f94c72499b36ac6ccfe569fc8203dd3ef40fd131`
- `ComeOn.dll` 首选 ImageBase：`0x10000000`
- 导入 `steam_api.dll`
- 导出 `dll_DirectShow_play_media`

Steam 版对游戏内容和运行链的改动比最初估计更大，因此当前明确**不采用“非 Steam EXE 替换 Steam EXE”方案**。

## 多语言 `.lib` 机制：已确认并保留

`ComeOn.dll` 中已确认：

- `local_config`：VA `0x10015394`
- `current_language`：VA `0x100153B0`
- 默认 `chs`：VA `0x100153D4`
- `.`：VA `0x1001528C`
- `.lib`：VA `0x10015290`
- 配置初始化函数约 `0x10005310`
- `.lib` 文件打开 Hook 约 `0x10003E30`

逻辑为：读取 `ComeOn.ini -> [local_config] current_language`，请求 `Xxx.Lib` 时先尝试 `Xxx.Lib.<language>`，失败才回退裸文件。

### clean1 保留的唯一 Steam 新代码

游戏 `CreateFileA` IAT 槽：`0x005511E4`。低层文件包装器中的唯一 CreateFileA CALL：`0x0052824E`，原始机器码：

```text
FF 15 E4 11 55 00
```

clean1 不覆盖 IAT，只把这一条 6 字节 CALL 等长改成：

```text
call steam_create_file_a_shim
nop
```

shim 只有 basename **精确等于** `ResJM.Lib` 时才处理：读取 `ComeOn.ini` 当前语言，规范化到 `chs/cht/eng/jpn`，先打开对应语言文件，失败才保留 raw fallback。其它文件不处理；非 Steam 不安装。

这是 `v0.1-test4` 已由用户实机确认成功的部分，也是 clean1 唯一从 test2 以后带回的运行逻辑。

## test3~test9 历史实验结论

这些实验全部保留为研究历史，但当前代码不执行：

- test3：仅禁 delayed full JMM，仍弹裸 `ResJM.Lib`，失败；
- test4：ResJM 兜底成功；影片 COM 4:3/child 实验失败；
- test5：ActiveMovie HWND watchdog 未命中实际影片，失败；
- test6：主 EXE MovieManager / Steam export A/B 没有得到有效运行时命中，失败；
- test7：teardown 固定绝对地址签名受 ASLR relocation 影响，实际上未安装；
- test8：改成 ASLR-safe RVA 后确认真正安装，OpenGL 仍崩；
- test9：在 ComeOn.exe 进程中硬隔离 `ComeOn.dll` DirectShow worker/export 后仍崩。

### 重要修正：不要再把 test4~test9 当成“实际开场动画进程已经被修改”的证据

后续调查表明，Steam 2.01 启动链很可能在 `ComeOn.exe` 启动前就由官方启动器阶段处理开场动画。因此 `ComeOn.exe` 内虽然同样可加载 `ComeOn.dll`，但 test4~test9 在游戏进程内对同名 DLL 的改动**不能再被用来证明实际开场动画实例已经被修改**。这解释了多个版本“安装日志成功、屏幕动画却完全不变”的异常。

该启动器阶段结论目前只作为**待独立验证的修正方向**；外部模组/仓库资料当前不纳入本项目固化参考。用户会先自行测试相关模组的 OpenGL 行为，再决定是否需要继续研究启动器阶段。

## 当前阻塞项

1. Steam + cnc-ddraw OpenGL 仍崩溃；
2. Steam 开场动画仍拉伸；
3. 需要先区分“启动器阶段动画问题”与“ComeOn.exe OpenGL 初始化/运行时问题”；
4. 在用户完成外部模组 A/B 前，不再继续把外部模组实现纳入本项目方案。
