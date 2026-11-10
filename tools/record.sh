#!/system/bin/sh
# record.sh — Screen recorder for Android (captures overlay)
# Push to device: adb push record.sh /data/local/tmp/
# Usage:
#   sh record.sh start        — start recording
#   sh record.sh stop         — stop and save
#   sh record.sh start 120    — record for 120 seconds max
#   sh record.sh list         — list saved recordings

DIR="/sdcard/CheatRecords"
PID_FILE="/data/local/tmp/.rec_pid"
CURRENT="/data/local/tmp/.rec_current"

mkdir -p "$DIR" 2>/dev/null

case "$1" in
start)
    if [ -f "$PID_FILE" ]; then
        OLD=$(cat "$PID_FILE")
        if kill -0 "$OLD" 2>/dev/null; then
            echo "[!] Recording already running (PID $OLD). Use 'stop' first."
            exit 1
        fi
    fi

    DURATION="${2:-180}"
    TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    OUTFILE="$DIR/rec_${TIMESTAMP}.mp4"

    echo "$OUTFILE" > "$CURRENT"

    # screenrecord captures all SurfaceFlinger layers (including overlay)
    screenrecord \
        --size 1280x720 \
        --bit-rate 8000000 \
        --time-limit "$DURATION" \
        "$OUTFILE" &

    echo $! > "$PID_FILE"
    echo "[+] Recording started -> $OUTFILE"
    echo "[+] Max duration: ${DURATION}s | PID: $(cat $PID_FILE)"
    echo "[+] Run 'sh record.sh stop' to finish"
    ;;

stop)
    if [ ! -f "$PID_FILE" ]; then
        echo "[-] No recording in progress"
        exit 1
    fi
    PID=$(cat "$PID_FILE")
    if kill -0 "$PID" 2>/dev/null; then
        kill -INT "$PID"
        sleep 1
        echo "[+] Recording stopped"
    else
        echo "[*] Process already finished"
    fi
    rm -f "$PID_FILE"

    if [ -f "$CURRENT" ]; then
        FILE=$(cat "$CURRENT")
        if [ -f "$FILE" ]; then
            SIZE=$(ls -la "$FILE" | awk '{print $5}')
            echo "[+] Saved: $FILE ($SIZE bytes)"
        fi
        rm -f "$CURRENT"
    fi
    ;;

list)
    echo "=== Recordings in $DIR ==="
    ls -lh "$DIR"/*.mp4 2>/dev/null || echo "(empty)"
    ;;

pull)
    echo "[*] Use from PC:"
    echo "    adb pull $DIR/ ./recordings/"
    ;;

*)
    echo "Usage: sh record.sh {start [seconds]|stop|list|pull}"
    echo ""
    echo "  start [sec]  — Start recording (default 180s max)"
    echo "  stop         — Stop current recording"
    echo "  list         — List saved recordings"
    echo "  pull         — Show adb pull command"
    ;;
esac
