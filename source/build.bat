@echo off
"%SystemRoot%\System32\chcp.com" 65001 >nul
setlocal
cd /d "%~dp0"
REM 一次编译生成发行件与_debug完整版；两者优化相同。  
REM 只对发行副本去符号，UPX可选，完整调试件不压缩。  
set "BUILD_ARGS="
set "NO_PAUSE="
:parse_args
if "%~1"=="" goto :find_python
if /i "%~1"=="--no-pause" goto :no_pause
if /i "%~1"=="--upx" goto :upx
if /i "%~1"=="--checks-only" goto :checks_only
echo [失败] 不支持的参数；可用--no-pause、--upx、--checks-only。  
goto :failed
:no_pause
set "NO_PAUSE=1"
shift
goto :parse_args
:upx
set "BUILD_ARGS=%BUILD_ARGS% --upx"
shift
goto :parse_args
:checks_only
set "BUILD_ARGS=%BUILD_ARGS% --checks-only"
shift
goto :parse_args
:find_python
REM 使用完整可执行文件路径，避免cmd的短命令解析受PATH/PATHEXT影响。  
set "PYTHON_EXE="
set "PYTHON_ARGS="
for /f "delims=" %%P in ('%SystemRoot%\System32\where.exe python.exe 2^>nul') do if not defined PYTHON_EXE set "PYTHON_EXE=%%P"
if defined PYTHON_EXE goto :python_ready
for /f "delims=" %%P in ('%SystemRoot%\System32\where.exe py.exe 2^>nul') do if not defined PYTHON_EXE set "PYTHON_EXE=%%P"
if not defined PYTHON_EXE goto :python_missing
set "PYTHON_ARGS=-3"
:python_ready
"%PYTHON_EXE%" %PYTHON_ARGS% tools\build.py %BUILD_ARGS%
if errorlevel 1 goto :failed
echo [成功] 发行件和_debug完整版已构建验证；输出状态详见上方信息。  
if not defined NO_PAUSE pause
exit /b 0
:python_missing
echo [失败] 找不到Python 3，请安装并加入PATH。  
goto :failed
:failed
echo [失败] 构建或测试未通过；诊断保留在source\.build\Unified。  
if not defined NO_PAUSE pause
exit /b 1
