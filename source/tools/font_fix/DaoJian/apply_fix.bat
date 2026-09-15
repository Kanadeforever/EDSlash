@echo off
REM ============================================================================  
REM 刀剑封魔录 DPI 字体修复 v1.0：拖拽入口。  
REM
REM 使用方法：  
REM   1. 把 ComeOn.exe 或宽屏等兼容改版 EXE 拖到这个 BAT 文件上；  
REM   2. BAT 会调用同目录的 apply_dpi_font_fix.py；  
REM   3. 原 EXE 不会被覆盖，脚本会生成 *_DPI_FontFix.exe。  
REM
REM 这里使用 UTF-8 代码页，确保下面的中文提示在现代 Windows 终端里正常显示。  
REM ============================================================================  
chcp 65001 >nul

REM %~1 是用户拖到 BAT 上的第一个文件完整路径。  
REM 如果没有 %~1，说明用户只是双击 BAT，没有提供目标 EXE。  
if "%~1"=="" (
    echo [提示] 请把 ComeOn.exe 或兼容改版 EXE 拖到这个 BAT 文件上。  
    echo.
    pause
    exit /b 1
)

REM %~dp0 表示当前 BAT 所在目录。  
REM 这样无论用户从哪个工作目录启动 BAT，都能找到同目录的 Python 脚本。  
REM 路径外面必须加双引号，避免目录名里有空格时解析失败。  
python "%~dp0apply_dpi_font_fix.py" "%~1"

REM 保存 Python 脚本的退出码。  
REM 0 通常表示成功，非 0 表示脚本拒绝补丁或发生错误。  
set "EXIT_CODE=%ERRORLEVEL%"

echo.
REM 暂停窗口，方便用户看清成功信息、输出路径或者失败原因。  
pause

REM 把原 Python 退出码传回给调用者。  
exit /b %EXIT_CODE%
