chcp 65001 >nul
@echo off
setlocal
cd /d "%~dp0"

REM BladeSwordQOL v0.1-dev1 一键离线构建。  
REM 所有 C 源码统一编译进一个 Win32/x86 ASI；本体和外传不再分别生成 DLL。  
REM 包根固定为 docs\source\release 三目录。  
REM 本脚本直接位于 source\，因此项目包根就是 source 的上一级目录。  
set "PROJECT_DIR=%~dp0"
for %%I in ("%~dp0..") do set "PACKAGE_ROOT=%%~fI\"
set "BUILD_DIR=%PROJECT_DIR%_build"
set "RELEASE_DIR=%PACKAGE_ROOT%release"
set "LLVM_BIN="

if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
if exist "%RELEASE_DIR%" rmdir /s /q "%RELEASE_DIR%"
mkdir "%BUILD_DIR%"
mkdir "%RELEASE_DIR%"

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
"%CLANG_EXE%" --version >nul 2>nul
if errorlevel 1 goto :llvm_wrong_arch
"%LLD_LINK_EXE%" --version >nul 2>nul
if errorlevel 1 goto :llvm_wrong_arch

set "PYTHON_EXE=python.exe"
set "PYTHON_ARGS="
where python.exe >nul 2>nul
if not errorlevel 1 goto :python_ready
set "PYTHON_EXE=py.exe"
set "PYTHON_ARGS=-3"
where py.exe >nul 2>nul
if errorlevel 1 goto :python_missing

:python_ready
echo [1/11] 编译唯一入口 Main.c...  
"%CLANG_EXE%" -target i686-pc-windows-msvc -finput-charset=UTF-8 -fexec-charset=UTF-8 -ffreestanding -fno-stack-protector -fno-builtin -O2 -Wall -Wextra -Werror -Isrc -c src\Main.c -o "%BUILD_DIR%\Main.obj" || goto :build_failed

echo [2/11] 编译 Runtime...  
"%CLANG_EXE%" -target i686-pc-windows-msvc -finput-charset=UTF-8 -fexec-charset=UTF-8 -ffreestanding -fno-stack-protector -fno-builtin -O2 -Wall -Wextra -Werror -Isrc -c src\Runtime\GameProfile.c -o "%BUILD_DIR%\GameProfile.obj" || goto :build_failed
"%CLANG_EXE%" -target i686-pc-windows-msvc -finput-charset=UTF-8 -fexec-charset=UTF-8 -ffreestanding -fno-stack-protector -fno-builtin -O2 -Wall -Wextra -Werror -Isrc -c src\Runtime\EventBus.c -o "%BUILD_DIR%\EventBus.obj" || goto :build_failed
"%CLANG_EXE%" -target i686-pc-windows-msvc -finput-charset=UTF-8 -fexec-charset=UTF-8 -ffreestanding -fno-stack-protector -fno-builtin -O2 -Wall -Wextra -Werror -Isrc -c src\Runtime\HookManager.c -o "%BUILD_DIR%\HookManager.obj" || goto :build_failed
"%CLANG_EXE%" -target i686-pc-windows-msvc -finput-charset=UTF-8 -fexec-charset=UTF-8 -ffreestanding -fno-stack-protector -fno-builtin -O2 -Wall -Wextra -Werror -Isrc -c src\Runtime\ModuleRegistry.c -o "%BUILD_DIR%\ModuleRegistry.obj" || goto :build_failed
"%CLANG_EXE%" -target i686-pc-windows-msvc -finput-charset=UTF-8 -fexec-charset=UTF-8 -ffreestanding -fno-stack-protector -fno-builtin -O2 -Wall -Wextra -Werror -Isrc -c src\Runtime\Runtime.c -o "%BUILD_DIR%\Runtime.obj" || goto :build_failed

echo [3/11] 编译 DisplayFix 模块入口...  
"%CLANG_EXE%" -target i686-pc-windows-msvc -finput-charset=UTF-8 -fexec-charset=UTF-8 -ffreestanding -fno-stack-protector -fno-builtin -O2 -Wall -Wextra -Werror -Isrc -c src\Modules\DisplayFix\DisplayFixModule.c -o "%BUILD_DIR%\DisplayFixModule.obj" || goto :build_failed

echo [4/11] 编译本体 Profile 后端...  
"%CLANG_EXE%" -target i686-pc-windows-msvc -finput-charset=UTF-8 -fexec-charset=UTF-8 -ffreestanding -fno-stack-protector -fno-builtin -O2 -Wall -Wextra -Werror -Isrc -c src\Modules\DisplayFix\Backend_DaoJian.c -o "%BUILD_DIR%\Backend_DaoJian.obj" || goto :build_failed

echo [5/11] 编译外传 Profile 后端...  
"%CLANG_EXE%" -target i686-pc-windows-msvc -finput-charset=UTF-8 -fexec-charset=UTF-8 -ffreestanding -fno-stack-protector -fno-builtin -O2 -Wall -Wextra -Werror -Isrc -c src\Modules\DisplayFix\Backend_WaiZhuan.c -o "%BUILD_DIR%\Backend_WaiZhuan.obj" || goto :build_failed

echo [6/11] 链接单一 BladeSwordQOL.asi...  
"%LLD_LINK_EXE%" /dll /entry:DllMain@12 /nodefaultlib /machine:x86 /subsystem:windows /dynamicbase:no /nxcompat /implib:"%BUILD_DIR%\BladeSwordQOL.lib" /out:"%RELEASE_DIR%\BladeSwordQOL.asi" ^
  "%BUILD_DIR%\Main.obj" ^
  "%BUILD_DIR%\GameProfile.obj" ^
  "%BUILD_DIR%\EventBus.obj" ^
  "%BUILD_DIR%\HookManager.obj" ^
  "%BUILD_DIR%\ModuleRegistry.obj" ^
  "%BUILD_DIR%\Runtime.obj" ^
  "%BUILD_DIR%\DisplayFixModule.obj" ^
  "%BUILD_DIR%\Backend_DaoJian.obj" ^
  "%BUILD_DIR%\Backend_WaiZhuan.obj" || goto :build_failed

echo [7/11] 复制统一配置...  
copy /y template\BladeSwordQOL.ini "%RELEASE_DIR%\BladeSwordQOL.ini" >nul || goto :build_failed

echo [8/11] 运行结构与防回归验证...  
"%PYTHON_EXE%" %PYTHON_ARGS% tools\verify_build.py "%RELEASE_DIR%\BladeSwordQOL.asi" || goto :build_failed

echo [9/11] 检查 release 文件数量...  
for /f %%N in ('dir /b /a-d "%RELEASE_DIR%" ^| find /c /v ""') do set "RELEASE_FILE_COUNT=%%N"
if not "%RELEASE_FILE_COUNT%"=="2" goto :release_invalid

echo [10/11] 清理临时构建目录...  
rmdir /s /q "%BUILD_DIR%"

echo [11/11] 完成。  
echo [成功] 单一 ASI 已生成：%RELEASE_DIR%BladeSwordQOL.asi  
echo [成功] 本体/外传共用：%RELEASE_DIR%BladeSwordQOL.ini  
pause
exit /b 0

:llvm_missing
echo [失败] 找不到可用的 clang.exe / lld-link.exe。  
pause
exit /b 1

:llvm_wrong_arch
echo [失败] 找到的 LLVM 不能在当前 Windows 宿主运行。  
pause
exit /b 1

:python_missing
echo [失败] 找不到 Python 3。  
pause
exit /b 1

:release_invalid
echo [失败] release 必须严格只有 BladeSwordQOL.asi 和 BladeSwordQOL.ini。  
pause
exit /b 1

:build_failed
echo [失败] 构建或验证失败；保留 _build 便于诊断。  
pause
exit /b 1
