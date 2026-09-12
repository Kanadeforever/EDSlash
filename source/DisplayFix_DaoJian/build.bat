chcp 65001 >nul
setlocal
cls
@echo off
cd /d "%~dp0"

REM 仓库根目录：本脚本位于 source\DisplayFix_DaoJian，因此向上两级。  
set "PROJECT_ROOT=%~dp0..\.."
set "BUILD_DIR=%~dp0_build"
set "RELEASE_DIR=%PROJECT_ROOT%\release"

REM 先清理临时构建目录和发行目录，保证每次都是从零开始。  
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
if exist "%RELEASE_DIR%" rmdir /s /q "%RELEASE_DIR%"
mkdir "%BUILD_DIR%"
mkdir "%RELEASE_DIR%"

REM 编译器查找顺序：显式环境变量、PATH、LLVM 常见安装目录、Visual Studio 自带 LLVM。  
REM 这样同一份脚本既能在用户本机工作，也能在 Windows GitHub Actions Runner 上工作。  
set "CLANG_EXE="
set "LLD_LINK_EXE="
set "CLANG_DIR="

REM 如果调用者显式指定 DISPLAYFIX_CLANG，就优先使用该路径。  
if defined DISPLAYFIX_CLANG if exist "%DISPLAYFIX_CLANG%" set "CLANG_EXE=%DISPLAYFIX_CLANG%"
REM 如果调用者显式指定 DISPLAYFIX_LLD_LINK，就优先使用该路径。  
if defined DISPLAYFIX_LLD_LINK if exist "%DISPLAYFIX_LLD_LINK%" set "LLD_LINK_EXE=%DISPLAYFIX_LLD_LINK%"

REM 其次直接从 PATH 查找，GitHub Actions 安装 LLVM 后通常会落到这一层或标准目录。  
if not defined CLANG_EXE for /f "delims=" %%I in ('where clang.exe 2^>nul') do if not defined CLANG_EXE set "CLANG_EXE=%%I"
if not defined LLD_LINK_EXE for /f "delims=" %%I in ('where lld-link.exe 2^>nul') do if not defined LLD_LINK_EXE set "LLD_LINK_EXE=%%I"

REM 再检查 LLVM_HOME 和 LLVM_PATH；两者都同时兼容“指向根目录”和“直接指向 bin”。  
if not defined CLANG_EXE if defined LLVM_HOME if exist "%LLVM_HOME%\bin\clang.exe" set "CLANG_EXE=%LLVM_HOME%\bin\clang.exe"
if not defined CLANG_EXE if defined LLVM_HOME if exist "%LLVM_HOME%\clang.exe" set "CLANG_EXE=%LLVM_HOME%\clang.exe"
if not defined CLANG_EXE if defined LLVM_PATH if exist "%LLVM_PATH%\bin\clang.exe" set "CLANG_EXE=%LLVM_PATH%\bin\clang.exe"
if not defined CLANG_EXE if defined LLVM_PATH if exist "%LLVM_PATH%\clang.exe" set "CLANG_EXE=%LLVM_PATH%\clang.exe"

REM 检查 LLVM 官方安装器最常见的 64 位安装目录。  
if not defined CLANG_EXE if exist "%ProgramFiles%\LLVM\bin\clang.exe" set "CLANG_EXE=%ProgramFiles%\LLVM\bin\clang.exe"
if not defined LLD_LINK_EXE if exist "%ProgramFiles%\LLVM\bin\lld-link.exe" set "LLD_LINK_EXE=%ProgramFiles%\LLVM\bin\lld-link.exe"

REM 如果 clang 已找到，优先在 clang 同目录找 lld-link，避免误混用另一套 LLVM。  
if defined CLANG_EXE for %%I in ("%CLANG_EXE%") do set "CLANG_DIR=%%~dpI"
if not defined LLD_LINK_EXE if defined CLANG_DIR if exist "%CLANG_DIR%lld-link.exe" set "LLD_LINK_EXE=%CLANG_DIR%lld-link.exe"

REM 最后扫描 Visual Studio 安装目录里的 LLVM，兼容 VS 2022、VS 2026 和 Build Tools。  
if not defined CLANG_EXE for /f "delims=" %%I in ('where /r "%ProgramFiles%\Microsoft Visual Studio" clang.exe 2^>nul') do if not defined CLANG_EXE set "CLANG_EXE=%%I"
if defined CLANG_EXE for %%I in ("%CLANG_EXE%") do set "CLANG_DIR=%%~dpI"
if not defined LLD_LINK_EXE if defined CLANG_DIR if exist "%CLANG_DIR%lld-link.exe" set "LLD_LINK_EXE=%CLANG_DIR%lld-link.exe"
if not defined LLD_LINK_EXE for /f "delims=" %%I in ('where /r "%ProgramFiles%\Microsoft Visual Studio" lld-link.exe 2^>nul') do if not defined LLD_LINK_EXE set "LLD_LINK_EXE=%%I"

if not defined CLANG_EXE goto :compiler_missing
if not defined LLD_LINK_EXE goto :linker_missing

REM Python 只用于构建后静态验证；优先使用 PATH 中的 python.exe。  
set "PYTHON_EXE="
set "PYTHON_ARGS="
for /f "delims=" %%I in ('where python.exe 2^>nul') do if not defined PYTHON_EXE set "PYTHON_EXE=%%I"
if defined PYTHON_EXE goto :python_ready
for /f "delims=" %%I in ('where py.exe 2^>nul') do if not defined PYTHON_EXE set "PYTHON_EXE=%%I"
if defined PYTHON_EXE set "PYTHON_ARGS=-3"
if not defined PYTHON_EXE goto :python_missing

:python_ready
REM 把最终实际使用的工具路径打印出来，方便本地和 GitHub Actions 日志直接定位环境差异。  
echo [工具] clang=%CLANG_EXE%  
echo [工具] lld-link=%LLD_LINK_EXE%  
echo [工具] python=%PYTHON_EXE% %PYTHON_ARGS%  
echo.  

REM 第一步只编译一个 32 位 COFF 对象；不链接 C 运行库，也不生成任何额外依赖。  
echo [1/5] 编译 src\DisplayFix.c...  
"%CLANG_EXE%" -target i686-pc-windows-msvc -ffreestanding -fno-stack-protector -fno-builtin -O2 -Wall -Wextra -Werror -c "src\DisplayFix.c" -o "%BUILD_DIR%\DisplayFix.obj"
if errorlevel 1 goto :build_failed

REM 第二步直接用 lld-link 生成 PE32 ASI，并保持零 Import Directory 架构。  
echo [2/5] 链接 DisplayFix.asi...  
"%LLD_LINK_EXE%" /dll /entry:DllMain@12 /nodefaultlib /machine:x86 /subsystem:windows /dynamicbase:no /nxcompat /implib:"%BUILD_DIR%\DisplayFix.lib" /out:"%RELEASE_DIR%\DisplayFix.asi" "%BUILD_DIR%\DisplayFix.obj"
if errorlevel 1 goto :build_failed

REM 第三步只复制发行所需的唯一配置文件。  
echo [3/5] 复制唯一发行配置...  
copy /y "template\DisplayFix.ini" "%RELEASE_DIR%\DisplayFix.ini" >nul
if errorlevel 1 goto :build_failed

REM 第四步验证 ASI 结构和 INI 布局，防止再次把不可读取的第一节配置打进发行包。  
echo [4/5] 验证 ASI 与配置文件...  
"%PYTHON_EXE%" %PYTHON_ARGS% "tools\verify_build.py" "%RELEASE_DIR%\DisplayFix.asi"
if errorlevel 1 goto :build_failed

REM release 必须严格只有 ASI 和 INI，任何源码、工具、日志、文档都不允许混入。  
for /f %%N in ('dir /b /a-d "%RELEASE_DIR%" ^| find /c /v ""') do set "RELEASE_FILE_COUNT=%%N"
if not "%RELEASE_FILE_COUNT%"=="2" goto :release_invalid
if not exist "%RELEASE_DIR%\DisplayFix.asi" goto :release_invalid
if not exist "%RELEASE_DIR%\DisplayFix.ini" goto :release_invalid

rmdir /s /q "%BUILD_DIR%"

echo [5/5] 构建完成。  
echo.  
echo [成功] release 严格只包含 DisplayFix.asi 和 DisplayFix.ini。  
echo [成功] v0.3-test8a 保留 test8 的输入修复，并修正 INI 第一节读取与构建器自动定位。  
pause
exit /b 0

:compiler_missing
echo [失败] 找不到 clang.exe。已检查显式环境变量、PATH、LLVM 常见目录和 Visual Studio LLVM。  
echo [提示] 也可以设置 DISPLAYFIX_CLANG 为 clang.exe 的完整路径后重新运行。  
pause
exit /b 1

:linker_missing
echo [失败] 找不到 lld-link.exe。建议让 clang.exe 与 lld-link.exe 来自同一套 LLVM。  
echo [提示] 也可以设置 DISPLAYFIX_LLD_LINK 为 lld-link.exe 的完整路径后重新运行。  
pause
exit /b 1

:python_missing
echo [失败] 找不到 python.exe 或 py.exe。构建后验证必须使用 Python。  
pause
exit /b 1

:release_invalid
echo [失败] release 内容不符合规范，必须严格只有 DisplayFix.asi 和 DisplayFix.ini。  
goto :build_failed

:build_failed
echo.  
echo [失败] 构建或验证未通过；保留 _build 目录方便检查错误。  
pause
exit /b 1
