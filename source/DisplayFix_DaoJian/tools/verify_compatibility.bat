@echo off
REM ==================================================================================================  
REM verify_compatibility.bat  
REM
REM DisplayFix 的 EXE 兼容性只读检查入口。  
REM
REM 用法：  
REM   1. 把要检查的 ComeOn.exe 或其他 ComeOn 改版 EXE 拖到这个 BAT 上；  
REM   2. BAT 会调用同目录的 verify_compatibility.py；  
REM   3. Python 工具只读取 EXE，不会改任何字节；  
REM   4. 只有字体路径、分辨率路径、JMM 布局和主 HUD 根类结构全部能唯一确认时才报告通过。  
REM
REM 为什么不直接检查 SHA-256：  
REM   宽屏改版、字体永久补丁等安全修改都会改变整个 EXE 的 SHA-256，  
REM   但 DisplayFix 真正关心的是“自己准备修改的代码结构是否仍然存在且唯一”。  
REM   所以 SHA-256 只打印作版本记录，不作为兼容门槛。  
REM ==================================================================================================  
chcp 65001 >nul
setlocal

REM %~dp0 是这个 BAT 自己所在的目录。  
REM 先切换到这里，保证无论用户从哪个文件夹拖拽，都能找到旁边的 Python 脚本。  
cd /d "%~dp0"

REM %~1 是拖到 BAT 上的第一个文件。  
REM 没有参数时不给 Python 一个空路径，而是直接告诉用户正确用法。  
if "%~1"=="" (
    echo [用法] 请把要检查的 ComeOn.exe 或兼容改版 EXE 拖到 verify_compatibility.bat 上。  
    echo.
    pause
    exit /b 1
)

REM 检查 Python 是否存在。  
REM 这个工具只用 Python 标准库，不需要 pip 安装任何第三方模块。  
where python >nul 2>nul
if errorlevel 1 (
    echo [失败] 找不到 python.exe。请安装 Python 3 或把 Python 加入 PATH。  
    echo.
    pause
    exit /b 1
)

REM 把所有拖入的参数原样交给 Python。  
REM %* 可以一次检查多个 EXE；Python 会逐个给出通过/失败结果。  
python "verify_compatibility.py" %*
set "RESULT=%ERRORLEVEL%"

echo.
if "%RESULT%"=="0" (
    echo [完成] 所有目标都通过当前 DisplayFix 结构检查。  
) else (
    echo [注意] 至少有一个目标没有通过。不要在未知结构上强行套用运行时补丁。  
)

pause
exit /b %RESULT%
