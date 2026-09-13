# DisplayFix_DaoJian 构建说明

## 目录

- `src\DisplayFix.c`：主 ASI 源码。
- `template\DisplayFix.ini`：发行配置模板。
- `tools\verify_build.py`：成品 PE/导出/零导入检查。
- `tools\verify_compatibility.py`：ComeOn.exe 内容签名兼容检查。
- `tools\verify_compatibility.bat`：拖拽 EXE 的兼容检查入口。
- `build.bat`：从零构建。

## 依赖

- LLVM/Clang
- lld-link
- Python 3

构建过程不联网。

## 构建

双击：

```text
build.bat
```

脚本会：

1. 编译 Win32/x86 COFF 对象；
2. 用 `/nodefaultlib` 链接 ASI；
3. 复制模板 INI；
4. 调 `verify_build.py` 检查 PE32/i386、DLL、入口、`InitializeASI`、Import Directory=0；
5. 强制检查 `release` 只有两个文件。

## 发行目录

严格只有：

```text
release\DisplayFix.asi
release\DisplayFix.ini
```

文档、工具、独立 FontFix 均不复制到 release。

## 当前版本

v0.3-test10 继续保留 test8 的输入链、test8a 的 INI 第一节修复和 test8b 的精简 x64 LLVM 查找。test9 的 Steam one-shot “只广播 vtable+0x14”已经由实机证明无效；test10 改为只在 Steam 环境、HUD/顶层 UI/资源根目录都成熟后，补调用一次游戏原版 `0x4B35F0(mode=0, TargetWidth, TargetHeight)`，让 JMMDL 资源加载和后续 UI 广播都走原版完整流程。非 Steam 不执行该额外调用。

## v0.3-test10 Steam 修复说明

- test9 实机日志：`visited=34 / applied=34 / done=1`，但 `0x0B / 0x0E` 的 before/after 坐标完全不变，GUI 仍错误。由此确认单独调用顶层 UI `vtable+0x14` 不是非 Steam 第二阶段布局的完整语义。
- 重新反汇编 `0x004B35F0`：它先调用 `0x004EB9E0` 取得资源根目录并拼 `mb\`，按宽度选择 `JMMDL.txt/JMMDL800.txt`，再调用 `0x004D0500` 加载 JMM，最后才遍历顶层 UI 调 `vtable+0x14`。
- test10 从 `0x004B35F0 + 0x1E` 的 `call 0x004EB9E0` 继续解析资源根缓冲区；只有该字符串已非空，且 HUD 0x0B/0x0E 与 `manager+0x1C` 都成熟时才允许 Steam one-shot。
- Steam one-shot 直接复用原版 `0x004B35F0(0, TargetWidth, TargetHeight)`，并用 `in_progress` 防递归、`done` 防重复。
- test4 的 `MB\JMMDL*.txt` 弹窗被记录为“调用太早”的失败方案；test10 通过资源根目录非空门槛避免在 ASI 初始化阶段重演。
- 非 Steam 不执行额外 JMM apply，保持 v0.2-test1/v0.3-test1 以来已实机正常的 GUI 主线。

## v0.3-test9 同步说明

- Steam/非 Steam test8b 日志确认第一次 HUD 布局一致，但 Steam 缺少后续第二阶段 UI 尺寸应用。
- test9 从 `0x4087A0` 内容签名只解析 UI manager，不安装 JMM callsite hook；检测到 `ComeOn.dll` 且 HUD/顶层链成熟后，只广播一次 `vtable+0x14(0,W,H)`。
- 不调用完整 `0x4B35F0`，不会主动重载 JMMDL；非 Steam 继续原路径。
- 普通地图 WORLD/GLOBAL 高频诊断已关闭，只在顶部按钮真实拦截/fallback 与 Steam one-shot 时记录，减少 Steam 性能测试干扰。

- `v0.3-test8` 实机确认已经修复 test7 的普通地图左键 / Alt+F4 回归，并保留属性、道具两个顶部按钮响应；当前输入实现继续沿用 test8。
- test8 日志显示用户把 `BaseHeight` 改为 1080 后插件仍读取成 480。复核发行 INI 后确认 `[Display]` 原本正好位于 UTF-8 BOM 后的第一行；Win32 `GetPrivateProfile*A` 对 UTF-8 BOM 不可靠，第一节可能读不到，于是 `BaseHeight` 静默使用默认 480。test8a 将 INI 改为 UTF-8 无 BOM，并在 `[Display]` 前增加 ASCII 保护行，使以后编辑器即使重新加入 BOM，也不会再破坏第一节。
- test8a 日志新增 `ConfigPath=`，用于直接确认插件实际读取的是哪一份 `DisplayFix.ini`。
- `build.bat` 在 test8b 进一步精简：只检查 PATH、`%ProgramFiles%\LLVM\bin`、以及 `vswhere` 返回的最新 Visual Studio `VC\Tools\Llvm\x64\bin`。不再递归扫描 Visual Studio，也不再检查 `LLVM_HOME` / `LLVM_PATH`，避免误选 ARM64 宿主编译器。发行目录仍严格只有 ASI 与 INI。
- 主界面固定 640x480 的等比放大居中仍是后续任务，本轮没有处理。



## 编译器自动查找

`build.bat` 现在只确定一个 `LLVM_BIN`，并要求 `clang.exe` 与 `lld-link.exe` 来自同一目录。查找顺序固定为：PATH → `%ProgramFiles%\LLVM\bin` → `vswhere` 找到的最新 Visual Studio `VC\Tools\Llvm\x64\bin`。找到后会实际执行一次 `--version`，宿主架构不匹配会立即报错。

构建脚本开头固定使用 UTF-8 控制台、脚本目录作为工作目录，并自动计算仓库根目录。所有带正文的 `REM` 与 `echo` 行末尾都保留两个半角空格，后续修改时不要删掉。

`DisplayFix.ini` 的 `[Display]` 前固定保留一行 ASCII 保护说明。原因是 `GetPrivateProfile*A` 对 UTF-8 BOM 不可靠；即使编辑器重新加 BOM，也必须让 BOM 只影响保护行，不能直接贴在 `[Display]` 前。

- test8b 修复 test8a 构建器误选 `VC\Tools\Llvm\ARM64\bin\clang.exe` 的问题；Visual Studio 回退现在只认 `Llvm\x64\bin`，并加入可执行性检查。
- `verify_build.py` 会同步检查 build.bat 的工具链搜索规则、编码/换行以及 REM/echo 行尾两个半角空格。

## v0.3-test9 成品验证（历史）

- ASI SHA-256：`a125c11fcd7dd0f193d78ac4bc3ba2cc41ca5c78083018d70d81e8366a330c96`
- PE32 / i386 / DLL / `InitializeASI` / Import Directory=0 全部通过。
- 非 Steam 原版、Steam EXE 与 480P~1080P 改版均通过当前内容签名兼容检查。


## v0.3-test10 成品验证

- 重新从源码编译为 PE32 / i386 ASI，Import Directory=0，导出 `InitializeASI`。
- `verify_compatibility.py` 新增：验证 `0x4B35F0 + 0x1E` 确实 call 到资源路径构造函数，并解析资源根目录缓冲区。
- 原版、Steam EXE、永久字体修复版和 480P~1080P 改版样本全部重新通过内容签名检查。
- ASI SHA-256：`1f010dbfbee987b71d8382e0e214306403206d223e1db9c36e54832742274e10`。
