chcp 65001 >nul
setlocal
cls
@echo off
cd /d "%~dp0"

REM 仓库根目录：本脚本位于 source\DisplayFix_WaiZhuan，因此向上两级。  
set "PROJECT_ROOT=%~dp0..\.."
set "BUILD_DIR=%~dp0_build"
set "RELEASE_DIR=%PROJECT_ROOT%\release\WaiZhuan"
set "LLVM_BIN="

REM 每次都从干净的临时目录和 release 开始，避免旧产物混入本次结果。  
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
if exist "%RELEASE_DIR%" rmdir /s /q "%RELEASE_DIR%"
mkdir "%BUILD_DIR%"
mkdir "%RELEASE_DIR%"

REM 编译器只按三条可靠路径查找：PATH、LLVM 官方目录、Visual Studio 最新实例的 x64 LLVM。  
REM 不再递归扫描 Visual Studio，因此绝不会误选 ARM64\bin\clang.exe。  
for /f "delims=" %%I in ('where clang.exe 2^>nul') do if not defined LLVM_BIN for %%J in ("%%I") do set "LLVM_BIN=%%~dpJ"
if defined LLVM_BIN goto :llvm_found

if exist "%ProgramFiles%\LLVM\bin\clang.exe" set "LLVM_BIN=%ProgramFiles%\LLVM\bin\"
if defined LLVM_BIN goto :llvm_found

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :llvm_missing
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VS_ROOT=%%I"
if defined VS_ROOT if exist "%VS_ROOT%\VC\Tools\Llvm\x64\bin\clang.exe" set "LLVM_BIN=%VS_ROOT%\VC\Tools\Llvm\x64\bin\"
if not defined LLVM_BIN goto :llvm_missing

:llvm_found
set "CLANG_EXE=%LLVM_BIN%clang.exe"
set "LLD_LINK_EXE=%LLVM_BIN%lld-link.exe"
if not exist "%CLANG_EXE%" goto :llvm_missing
if not exist "%LLD_LINK_EXE%" goto :llvm_missing

REM 真正执行一次 --version；宿主架构不匹配的 clang/lld-link 会在这里立即失败，而不是等到编译时才报错。  
"%CLANG_EXE%" --version >nul 2>nul
if errorlevel 1 goto :llvm_wrong_arch
"%LLD_LINK_EXE%" --version >nul 2>nul
if errorlevel 1 goto :llvm_wrong_arch

REM Python 只用于构建后的静态验证；优先 python.exe，没有时再使用 py -3。  
set "PYTHON_EXE=python.exe"
set "PYTHON_ARGS="
where python.exe >nul 2>nul
if not errorlevel 1 goto :python_ready
set "PYTHON_EXE=py.exe"
set "PYTHON_ARGS=-3"
where py.exe >nul 2>nul
if errorlevel 1 goto :python_missing

:python_ready
echo [工具] clang=%CLANG_EXE%  
echo [工具] lld-link=%LLD_LINK_EXE%  
echo [工具] python=%PYTHON_EXE% %PYTHON_ARGS%  
echo.

REM 编译 32 位 COFF 对象；DisplayFix 不链接 CRT，也不新增外部导入表。  
echo [1/5] 编译 src\DisplayFix.c...  
"%CLANG_EXE%" -target i686-pc-windows-msvc -ffreestanding -fno-stack-protector -fno-builtin -O2 -Wall -Wextra -Werror -c "src\DisplayFix.c" -o "%BUILD_DIR%\DisplayFix.obj"
if errorlevel 1 goto :build_failed

REM 用同一套 LLVM 目录里的 lld-link 生成 Win32/x86 ASI，避免混用不同工具链。  
echo [2/5] 链接 DisplayFix.asi...  
"%LLD_LINK_EXE%" /dll /entry:DllMain@12 /nodefaultlib /machine:x86 /subsystem:windows /dynamicbase:no /nxcompat /implib:"%BUILD_DIR%\DisplayFix.lib" /out:"%RELEASE_DIR%\DisplayFix.asi" "%BUILD_DIR%\DisplayFix.obj"
if errorlevel 1 goto :build_failed

REM release 只复制唯一的配置模板。  
echo [3/5] 复制 DisplayFix.ini...  
copy /y "template\DisplayFix.ini" "%RELEASE_DIR%\DisplayFix.ini" >nul
if errorlevel 1 goto :build_failed

REM 验证 ASI、INI 和 build.bat 自身约束，防止工具链搜索规则再次回归。  
echo [4/5] 验证构建结果...  
"%PYTHON_EXE%" %PYTHON_ARGS% "tools\verify_build.py" "%RELEASE_DIR%\DisplayFix.asi"
if errorlevel 1 goto :build_failed

REM release 必须严格只有 DisplayFix.asi 和 DisplayFix.ini。  
for /f %%N in ('dir /b /a-d "%RELEASE_DIR%" ^| find /c /v ""') do set "RELEASE_FILE_COUNT=%%N"
if not "%RELEASE_FILE_COUNT%"=="2" goto :release_invalid
if not exist "%RELEASE_DIR%\DisplayFix.asi" goto :release_invalid
if not exist "%RELEASE_DIR%\DisplayFix.ini" goto :release_invalid

rmdir /s /q "%BUILD_DIR%"

echo [5/5] 构建完成。  
echo.
echo [成功] release 严格只包含 DisplayFix.asi 和 DisplayFix.ini。  
echo [成功] 编译器搜索已精简，并且只会使用可执行的 x64 宿主 LLVM。  
pause
exit /b 0

:llvm_missing
echo [失败] 找不到可用的 LLVM。请确认 clang.exe 和 lld-link.exe 在 PATH、C:\Program Files\LLVM\bin，或 Visual Studio 的 VC\Tools\Llvm\x64\bin 中。  
pause
exit /b 1

:llvm_wrong_arch
echo [失败] 找到的 LLVM 不能在当前 Windows 宿主上运行。请检查是否误用了 ARM64 版 clang/lld-link。  
pause
exit /b 1

:python_missing
echo [失败] 找不到 python.exe 或 py.exe。构建后验证需要 Python 3。  
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
