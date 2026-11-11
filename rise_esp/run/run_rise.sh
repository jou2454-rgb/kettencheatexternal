#!/bin/sh
# Pushes rise_esp to the phone and starts it as root in the background.
DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="$DIR/rise_esp"
REMOTE=/data/local/tmp/rise_esp
PKG=com.SKITLSE.StandRise
[ -f "$BIN" ] || { echo "rise_esp not found next to this script"; exit 1; }
adb wait-for-device || exit 1
adb push "$BIN" "$REMOTE" || exit 1
adb shell chmod 755 "$REMOTE"
adb shell "su -c 'killall rise_esp 2>/dev/null; nohup $REMOTE $PKG > /data/local/tmp/rise_esp.log 2>&1 &'"
echo "started in background; log: adb shell \"su -c 'cat /data/local/tmp/rise_esp.log'\""
