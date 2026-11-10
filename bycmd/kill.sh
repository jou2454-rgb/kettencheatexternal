#!/system/bin/sh
for pid in $(pidof eeeww); do
    kill -9 "$pid" 2>/dev/null && echo "killed $pid"
done
