@echo off
setlocal
set "BIN=%~dp0rise_esp"
set "REMOTE=/data/local/tmp/rise_esp"
set "PKG=com.standrise.game"

if not exist "%BIN%" (
    echo [!] rise_esp not found next to this script
    pause
    exit /b 1
)

echo [*] waiting for the phone (USB debugging on, root for su)
adb wait-for-device
adb push "%BIN%" "%REMOTE%"
if errorlevel 1 (
    echo [!] adb push failed
    pause
    exit /b 1
)
adb shell chmod 755 "%REMOTE%"
adb shell "su -c 'killall rise_esp 2>/dev/null; nohup %REMOTE% %PKG% > /data/local/tmp/rise_esp.log 2>&1 &'"
echo [*] started in background
echo [*] log: adb shell "su -c 'cat /data/local/tmp/rise_esp.log'"
pause
