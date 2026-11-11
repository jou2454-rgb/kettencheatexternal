#!/bin/sh
# Fails if the project contains anything that can write to another process.
pat='pwrite|PTRACE_POKE|process_vm_writev|mem_write|wpm|vm_write|O_WRONLY|O_RDWR'
dirs=""
for d in src game include tests; do [ -d "$d" ] && dirs="$dirs $d"; done
hits=$(grep -rnE "$pat" $dirs 2>/dev/null)
if [ -n "$hits" ]; then
    echo "$hits"
    echo "write-capable call found"
    exit 1
fi
echo "read-only: ok"
