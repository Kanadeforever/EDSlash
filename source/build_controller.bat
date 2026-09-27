chcp 65001 >nul
@echo off
setlocal
cd /d "%~dp0"
REM 独立手柄目标沿用主项目 source、docs、release，不清空其它模块产物。  
python tools\controller\build_controller.py
if errorlevel 1 goto :failed
echo [成功] 独立手柄基础版已构建，两作实机测试仍需分别完成。  
if /i not "%~1"=="--no-pause" pause
exit /b 0
:failed
echo [失败] 构建未完成，保留 _build_controller 用于诊断。  
if /i not "%~1"=="--no-pause" pause
exit /b 1
