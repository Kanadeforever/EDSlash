# DisplayFix_DaoJian 构建说明

## 仓库目录

本项目从 test11 起固定使用以下目录结构，后续版本不得自行改名或增加根目录散文件：

```text
source\
  DisplayFix_DaoJian\
    src\DisplayFix.c
    template\DisplayFix.ini
    tools\verify_build.py
    tools\verify_compatibility.py
    tools\verify_compatibility.bat
    tools\工具详细说明.md
    build.bat
    readme.md
  FontFix_DaoJian\
    apply_dpi_font_fix.py
    apply_fix.bat

docs\
  DisplayFix_DaoJian\
    完整接档说明.md
    逆向工程知识库.md
    使用与测试说明.md
    前端动画与主菜单研究说明.md
    Steam兼容调查说明.md
    宽高比与HUD研究说明.md
    字体修复工具说明.md

release\
  DisplayFix.asi
  DisplayFix.ini
```

`readme.md` 是用户明确要求保留的文档文件名例外。`release\` 必须严格只有 ASI + INI，不能复制 docs、工具或源码进去。

## 工具链

需要 Windows 可执行的 x64 宿主 LLVM/Clang、`lld-link` 和 Python 3；构建不联网。

`build.bat` 搜索顺序：

1. PATH 中的 `clang.exe`；
2. `%ProgramFiles%\LLVM\bin`；
3. `vswhere` 返回的最新 Visual Studio `VC\Tools\Llvm\x64\bin`。

禁止 `where /r` 递归搜索，禁止误选 ARM64 LLVM。`clang.exe` 与 `lld-link.exe` 必须来自同一目录，并在编译前运行 `--version`。

BAT 固定 UTF-8 BOM + CRLF；所有有正文的 `REM` / `echo` 行末尾保留两个半角空格。

## 从零构建

`source\DisplayFix_DaoJian\build.bat` 会：

1. 删除旧 `_build` 与仓库 `release`；
2. `clang -target i686-pc-windows-msvc` 编译 `src\DisplayFix.c`；
3. `lld-link /nodefaultlib /machine:x86` 生成 `DisplayFix.asi`；
4. 复制唯一配置模板；
5. 运行 `tools\verify_build.py`；
6. 强制确认 `release` 最终只有两个文件。

## v0.3-test15 当前架构

- 字体：继续使用已实机通过的 96 DPI 运行时修复；
- 前端：保持原版 mode 4 / 640x480；
- gameplay gate：使用 `0x00404A00` Strategy state=3，不再使用 HUD/world 对象；
- 进入游戏：临时把 `0x00407000` 的 force 0 改为 1，让原版 `0x00404D30` 真正重建目标宽高；
- 退出游戏：先执行原版 Strategy 清理，再恢复 FRONTEND profile，并以 `mode=4, force=1` 重建原版前端 Surface；
- Steam：继续使用 test10 delayed full `0x004B35F0`；
- HUD/输入：继续使用已经通过的根节点居中、0x0B/0x0E fallback 和 world press 防穿透。

## 关键回归红线

不得破坏：96 DPI、BaseHeight 读取、普通地图左键、Alt+F4、右侧鼠标技能、0x0B/0x0E 点击与防穿透、主 HUD 居中、Steam test10 JMM 路径。

## v0.3-test15 当前成品验证

- ASI SHA-256：`2f4b982148d3f110fb3508e33e666efed9b22e3f097308a5233d4c874d492831`
- PE32 / i386 / DLL / `InitializeASI` / Import Directory=0：通过；
- 当前 DPI 修复 EXE 与历史 480P/540P/720P/768P/900P/1080P EXE：全部通过兼容验证；
- 新增验证：原版前端 `0x004053D8 -> mode 4 -> 0x00404D30` 与 Strategy enter `0x0040700F -> 0x00404D30` 交叉一致；
- test15 尚未实机，不能写成稳定版。

详细累计历史见 `docs\DisplayFix_DaoJian\完整接档说明.md` 和 `逆向工程知识库.md`。
