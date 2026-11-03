#!/system/bin/sh
# debug_capture.sh — запуск чита с захватом полного лога
# закинь на устройство рядом с бинарником, запусти:
#   sh debug_capture.sh
# лог сохранится в eeeww_stdout.log — кинь его мне

DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="$DIR/eeeww_4"
LOG="$DIR/eeeww_stdout.log"

echo "[debug] $(date) starting capture" > "$LOG"
echo "[debug] binary=$BIN" >> "$LOG"
echo "[debug] device=$(getprop ro.product.model) android=$(getprop ro.build.version.release) sdk=$(getprop ro.build.version.sdk)" >> "$LOG"
echo "[debug] arch=$(getprop ro.product.cpu.abi)" >> "$LOG"
echo "[debug] selinux=$(getenforce 2>/dev/null || echo unknown)" >> "$LOG"
echo "---" >> "$LOG"

if [ ! -f "$BIN" ]; then
    echo "[debug] ERROR: binary not found at $BIN" >> "$LOG"
    echo "ERROR: binary not found at $BIN"
    cat "$LOG"
    exit 1
fi

chmod 755 "$BIN" 2>/dev/null

echo "[debug] launching..." >> "$LOG"
"$BIN" >> "$LOG" 2>&1 &
PID=$!
echo "[debug] pid=$PID" >> "$LOG"
echo "[capture] pid=$PID log=$LOG"
echo "[capture] ctrl+c to stop, log saves automatically"

trap "kill $PID 2>/dev/null; echo; echo '[debug] stopped at $(date)' >> '$LOG'; echo '[capture] done. log=$LOG'; exit 0" INT TERM

wait $PID 2>/dev/null
echo "[debug] exited code=$? at $(date)" >> "$LOG"
echo "[capture] binary exited. log=$LOG"
