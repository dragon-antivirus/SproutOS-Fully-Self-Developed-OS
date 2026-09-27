@echo off
REM ============================================================
REM  把 SproutOS IDE 打包成单文件 Windows exe（SproutIDE.exe）
REM  前置：已安装 PyInstaller：  pip install pyinstaller
REM  用法：在本 ide\ 目录双击本文件，或命令行执行 build_exe.bat
REM  产物：dist\SproutIDE.exe  （可单独分发，无需 Python 环境）
REM  说明：用 SproutIDE.spec 显式收集 scripts/ 下的编译器/模拟器，
REM        避免 --add-data 相对路径在某些环境下解析异常导致找不到 terrac.py
REM ============================================================
setlocal
cd /d "%~dp0"

echo [1/2] 确保 PyInstaller 已安装...
python -m pip install --quiet pyinstaller >nul 2>&1 || pip install pyinstaller || (
    echo [错误] PyInstaller 安装失败，请手动：pip install pyinstaller
    pause
    exit /b 1
)

echo [2/2] 通过 SproutIDE.spec 构建单文件 exe...
pyinstaller --noconfirm --clean SproutIDE.spec

if errorlevel 1 (
    echo [错误] PyInstaller 构建失败
    pause
    exit /b 1
)

echo.
echo 完成！产物在: %~dp0dist\SproutIDE.exe
echo 双击 SproutIDE.exe 即可启动，无需 Python 环境。
echo.
echo 提示：把 .seed 部署进 disk.img 需要 mtools
echo   Ubuntu:   sudo apt install mtools
echo   Windows: choco install mtools  （或用 WSL / MSYS2 跑 IDE）
pause