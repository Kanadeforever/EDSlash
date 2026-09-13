# DisplayFix_DaoJian 构建说明

## 仓库目录

```text
source\DisplayFix_DaoJian  src\DisplayFix.c
  template\DisplayFix.ini
  toolserify_build.py
  toolserify_compatibility.py
  toolserify_compatibility.bat
  tools\工具详细说明.md
  build.bat
  readme.md
```

独立永久字体修复位于 `source\FontFix_DaoJian\`；项目文档位于 `docs\DisplayFix_DaoJian\`。

发行 `release\` 严格只有：

```text
DisplayFix.asi
DisplayFix.ini
```

## 工具链

需要 Windows 下可执行的 x64 宿主 LLVM/Clang、`lld-link` 和 Python 3。构建不联网。

`build.bat` 编译器搜索严格精简为：

1. PATH 中的 `clang.exe`；
2. `%ProgramFiles%\LLVMin`；
3. `vswhere` 返回的最新 Visual Studio `VC\Tools\Llvmdin`。

不递归搜索 Visual Studio，不使用 ARM64 LLVM，不使用 `LLVM_HOME/LLVM_PATH`。`clang.exe` 与 `lld-link.exe` 必须来自同一目录，并在编译前执行 `--version` 验证当前宿主可以运行。

BAT 固定 UTF-8 BOM + CRLF；所有有正文的 `REM`/`echo` 行末尾保留两个半角空格。

## 从零构建

双击 `build.bat`。脚本会：

1. 清理 `_build` 和仓库 `release`；
2. 用 `clang -target i686-pc-windows-msvc` 编译 `src\DisplayFix.c`；
3. 用 `lld-link /nodefaultlib /machine:x86` 链接 ASI；
4. 复制 `template\DisplayFix.ini`；
5. 调 `toolserify_build.py` 检查 PE32/i386、DLL、入口、`InitializeASI`、Import Directory=0、INI、BAT规则；
6. 强制检查 `release` 只有 ASI + INI。

## v0.3-test11 当前代码架构

本版保留已经通过的字体、输入、HUD、Steam test10 JMM 修复，并新增 FRONTEND/GAMEPLAY 两套运行时代码 profile：

- 前端/启动动画/主菜单保持原版固定 640x480 4:3 生命周期；
- 主 HUD 真正出现后才写入 `TargetWidth x BaseHeight` 并必要时调用原版 `0x00404D30` 重应用当前显示模式；
- HUD 析构时只恢复 FRONTEND profile，不在析构里粗暴强制 640x480 Reset；
- Steam 进入游戏后仍保留 test10 one-shot 完整 `0x004B35F0`。

这是为了解决旧版高 BaseHeight 时“主菜单只在左上角、其余黑屏并可能出现旧 surface 黄色残影”的问题。

**test11 的前端/动画 profile 尚未实机通过，不能视为稳定基线。**

## 关键回归红线

后续任何修改不得破坏：

- 96 DPI 字体修复；
- BaseHeight 正确读取；
- 普通地图左键；
- Alt+F4；
- 右侧鼠标技能设置按钮；
- 属性/道具 0x0B/0x0E 点击与防穿透；
- 主 HUD 居中；
- Steam test10 `child_layout_changed=1` 的最终 GUI。

详见 `docs\DisplayFix_DaoJian\完整接档说明.md` 与 `逆向工程知识库.md`。

## v0.3-test11 当前成品验证

- ASI SHA-256：`945ed2958f1c2ddafc416049ce69859f95abb80ea72dd8e043ffa213a7702ccb`
- PE32 / i386 / DLL / `InitializeASI` / Import Directory=0 全通过。
- 非 Steam、Steam、永久 DPI 字体版、480P~1080P 历史改版全部通过当前内容签名检查。
- test11 新增的 `0x00404D30` 显示模式函数和 HUD `+0x00` destructor 结构均通过。
- 前端/动画双 profile 尚待实机，不得把静态通过写成实机通过。
