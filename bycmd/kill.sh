#!/system/bin/sh
# kill all eeeww and capture_bycmd processes
killall -9 eeeww 2>/dev/null
pkill -9 -f eeeww 2>/dev/null
pkill -9 -f capture_bycmd 2>/dev/null
ps -A 2>/dev/null | grep -E 'eeeww|capture_bycmd' | while read _ pid _rest; do
    kill -9 "$pid" 2>/dev/null && echo "killed $pid"
done
echo "done"
