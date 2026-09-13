# Steam 兼容调查说明

## 已确认文件关系

Steam `ComeOnSteam.exe` 与非 Steam `ComeOn.exe` 大小相同，主体代码几乎一致，只加入一个非常小的启动桩。

Steam EXE 会在启动时：

```text
LoadLibraryA("ComeOn.dll")
-> 恢复现场
-> 继续原版启动函数
```

Steam EXE SHA-256：`0887ceae7589999a389ec271d690e1c55204d59575f8f2adb5ef46a1e605b3f5`

非 Steam SHA-256：`c8d1aa33272a2c28e94f0d18eda2d6a8fcb14796644d8f8bed467b744ddc94b5`

`ComeOn.dll` SHA-256：`9f8f71ad958c5c5d33e1b5aeb78603b970fd16d7e0b674aa7629d497ae96e2d0`

## Steam / 非 Steam 对照已经闭合的差异

`v0.3-test8b` 的两份实机日志给出了稳定对照：

- Steam 和非 Steam **第一次主 HUD 布局完全一致**；BaseHeight=720 时顶部真实按钮最初都位于大约 `0x0B x=812`、`0x0E x=840`。
- 非 Steam 随后还会自然发生一轮后续 UI/JMM 尺寸应用，按钮进一步移动到大约 `0x0B x=968`、`0x0E x=996`，其它 HUD 元素也一起进入最终布局。
- Steam/ComeOn.dll 环境没有这轮第二阶段应用，因此停在第一阶段 GUI 状态。
- `test8a` 与 `test8b` 的 ASI `.text/.data/.reloc` 已逐节比对为相同，Steam 下两版帧数体感差异不能归因于 build.bat 或运行时代码改变。

由此，当前 Steam GUI 问题不再泛泛归因于“宽屏公式错误”，而是明确收敛为：**ComeOn.dll 启动环境缺少非 Steam 自然发生的后续 UI 尺寸应用。**

## v0.3-test10 Steam 专用修复

### test9 的实机反证

Steam test9 日志已经证明：

```text
[RUNTIME] Steam delayed UI sync begin ...
[RUNTIME] Steam delayed UI sync end visited=34 applied=34 done=1
```

但 `id0B_before/id0B_after` 与 `id0E_before/id0E_after` 完全相同，截图中的 GUI 也仍然错误。也就是说，34 个顶层 UI 的 `vtable+0x14` 虽然都执行了，**纯尺寸广播本身并不会把 Steam 带到非 Steam 的第二阶段布局**。

### 重新反汇编 0x004B35F0 的完整语义

`0x004B35F0` 的关键顺序已经闭合：

```text
0x004B35F0
  -> 0x004EB9E0：取得游戏资源根目录并追加 "mb\"
  -> 根据 width 选择 JMMDL.txt / JMMDL800.txt
  -> 0x004D0500：真正加载对应 JMM 布局资源
  -> 遍历 manager+0x1C 顶层链
  -> 每个对象 vtable+0x14(mode,width,height)
```

因此非 Steam 第二阶段的关键不是单独的 `vtable+0x14`，而是**先重新加载 JMM 布局资源，再广播尺寸**。

### test10 的处理

- 仍然只在检测到 `ComeOn.dll` 时启用；非 Steam 不进入。
- 不改 `0x004087B5` 自然 callsite。
- 等待主 HUD 0x0B/0x0E、`manager+0x1C` 顶层链和游戏资源根字符串全部成熟。
- 资源根缓冲区不是硬编码：从 `0x4B35F0 +0x1E` 的 `call 0x4EB9E0` 解码目标，再从其 `56 57 BF <imm32>` 函数头解析。
- 条件满足后只额外调用一次原版 `0x4B35F0(0,TargetWidth,TargetHeight)`；资源加载和后续广播都继续走游戏自己的代码。
- `in_progress` 防递归；原版返回成功后 `done=1`，不重复。

### 与 test4 的区别

test4 也曾直接调用完整 `0x4B35F0`，但当时是在 ASI 初始化阶段，资源路径和 GUI 生命周期尚未准备好，所以弹出 `MB\JMMDL.txt` / `MB\JMMDL800.txt`。test10 明确增加“资源根字符串非空 + HUD/顶层 UI 已成熟”门槛，不能把两者视为同一个时序实验。

### test10 预期日志

```text
[OK] Steam delayed JMM apply armed; waits for mature HUD + top-level UI + resource root
[RUNTIME] Steam delayed JMM apply begin ... resource_root=ready ...
[RUNTIME] Steam delayed JMM apply end result=1 done=1 child_layout_changed=1 ...
```

若出现 `MB\JMMDL*.txt` 弹窗，立即退出并保留日志，该结果应判定为 test10 时序仍不安全。

## v0.3-test9 Steam 专用修复（历史失败方案）

`v0.3-test9` 只在运行时检测到 `ComeOn.dll` 时启用一条条件化 one-shot 同步；非 Steam 路径不执行。

触发条件全部满足后才同步：

1. 主 HUD 已经实际进入 `vtable+0x58` 自动布局；
2. 已确认真实顶部 child `0x0B / 0x0E` 都已经建立；
3. 从 `0x004087A0` 内容签名解析出的 UI manager 存在；
4. `manager+0x1C` 顶层 UI 链已经非空；
5. 本进程此前没有成功执行过 Steam 同步。

同步内容仅等价于 `0x004B35F0` 的后半段：遍历当前顶层 UI，并调用各自的 `vtable+0x14(0, TargetWidth, TargetHeight)`。

明确**不会**：

- 调用完整 `0x004B35F0`；
- 重载 `MB\JMMDL.txt` / `MB\JMMDL800.txt`；
- 改写 `0x004087B5` 自然 JMM apply callsite；
- 对非 Steam 执行任何补广播。

为避免递归，广播前先设置 `in_progress`；若广播内部再次触发主 HUD layout，内层不会重复同步。只有实际成功应用至少一个顶层 UI 后才设置 `done=1`。

测试日志应出现一次：

```text
[RUNTIME] Steam delayed UI sync begin ...
[RUNTIME] Steam delayed UI sync end visited=... applied=... done=1 ...
```

`id0B_before/id0E_before` 与 `id0B_after/id0E_after` 会直接记录同步前后真实矩形，用于确认 Steam 是否进入与非 Steam 相同的第二阶段布局。

## ComeOn.dll 当前静态结论

已观察到它包含 Steam API 初始化、额外线程、窗口/消息相关 API、进程内模式扫描/写内存能力。`QueryPerformanceCounter/GetTickCount` 的已追踪引用位于 CRT/security-cookie 初始化，目前没有证据证明它们直接是游戏 FPS 限制器。

因此 test10 仍不修改 ComeOn.dll，也不做未经证实的计时器 patch；当前只验证缺失的完整 JMM 第二阶段能否由原版 0x4B35F0 在正确时机安全补回。

## 仍待实机确认

- Steam test10 是否出现 `Steam delayed JMM apply begin/end`，且 `result=1 / done=1 / child_layout_changed=1`。
- Steam GUI 是否真正进入与非 Steam 相同的最终布局，而不是 test9 那样 applied 成功但 child 坐标不变。
- Steam 是否完全不再弹 `MB\JMMDL*.txt`；若弹窗，说明完整 JMM 调用仍过早。
- Steam 帧数/游戏速度是否维持 test9 已恢复到 50+ 的水平。
- 非 Steam 必须保持 test8b 已有行为，不应出现任何 `Steam delayed JMM apply` 日志。

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
