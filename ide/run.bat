@echo off
chcp 65001 >nul
setlocal
REM ============================================================
REM  SproutOS 原生 IDE 启动器（真正运行在本机桌面的 Windows 程序）
REM  首次运行会自动用本机 Python 安装 PyQt6（仅需一次）。
REM ============================================================

REM 优先用 Windows Python Launcher，其次 python
set "PY=py"
where py >nul 2>nul
if errorlevel 1 (
    set "PY=python"
    where python >nul 2>nul
    if errorlevel 1 (
        echo [错误] 未检测到 Python。请先安装 Python 3.10+ 并勾选 "Add to PATH"。
        echo 下载地址：https://www.python.org/downloads/
        pause
        exit /b 1
    )
)

REM 缺少 PyQt6 则自动安装到用户目录（只需一次）
"%PY%" -c "import PyQt6" >nul 2>nul
if errorlevel 1 (
    echo [信息] 首次运行，正在安装 PyQt6（仅需一次，请保持联网）...
    "%PY%" -m pip install --user PyQt6
    if errorlevel 1 (
        echo [错误] PyQt6 安装失败，请检查网络后手动执行：
        echo     %PY% -m pip install PyQt6
        pause
        exit /b 1
    )
)

"%PY%" "%~dp0app.py"
if errorlevel 1 pause
endlocal
