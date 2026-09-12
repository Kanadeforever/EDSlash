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

v0.3-test8a 沿用 test8 已实机通过的 `0x4060EB -> 0x473F10` 顶部按钮穿透保护；本轮只修 INI 第一节读取和构建工具定位，不改变输入 hook。

## v0.3-test8a 同步说明

- `v0.3-test8` 实机确认已经修复 test7 的普通地图左键 / Alt+F4 回归，并保留属性、道具两个顶部按钮响应；当前输入实现继续沿用 test8。
- test8 日志显示用户把 `BaseHeight` 改为 1080 后插件仍读取成 480。复核发行 INI 后确认 `[Display]` 原本正好位于 UTF-8 BOM 后的第一行；Win32 `GetPrivateProfile*A` 对 UTF-8 BOM 不可靠，第一节可能读不到，于是 `BaseHeight` 静默使用默认 480。test8a 将 INI 改为 UTF-8 无 BOM，并在 `[Display]` 前增加 ASCII 保护行，使以后编辑器即使重新加入 BOM，也不会再破坏第一节。
- test8a 日志新增 `ConfigPath=`，用于直接确认插件实际读取的是哪一份 `DisplayFix.ini`。
- `build.bat` 已按仓库位置重新整理：同时自动查找 PATH、`LLVM_HOME`、`LLVM_PATH`、`Program Files\LLVM` 和 Visual Studio 自带 LLVM，兼容本地与 Windows GitHub Actions；发行目录仍严格只有 ASI 与 INI。
- 主界面固定 640x480 的等比放大居中仍是后续任务，本轮没有处理。



## 编译器自动查找

`build.bat` 会按以下顺序寻找 `clang.exe` / `lld-link.exe`：显式 `DISPLAYFIX_CLANG` / `DISPLAYFIX_LLD_LINK`、当前 `PATH`、`LLVM_HOME`、`LLVM_PATH`、`%ProgramFiles%\LLVM\bin`、Visual Studio 安装目录中的 LLVM。这样同一脚本可直接用于用户本机和 Windows GitHub Actions。

构建脚本开头固定使用 UTF-8 控制台、脚本目录作为工作目录，并自动计算仓库根目录。所有带正文的 `REM` 与 `echo` 行末尾都保留两个半角空格，后续修改时不要删掉。

`DisplayFix.ini` 的 `[Display]` 前固定保留一行 ASCII 保护说明。原因是 `GetPrivateProfile*A` 对 UTF-8 BOM 不可靠；即使编辑器重新加 BOM，也必须让 BOM 只影响保护行，不能直接贴在 `[Display]` 前。
