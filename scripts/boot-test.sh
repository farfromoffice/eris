#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 farfromoffice
set -euo pipefail

KERNEL=${KERNEL:-build/eris32.elf}
TIMEOUT=${TIMEOUT:-20}
LOG=${LOG:-build/boot.log}

if [[ ! -f $KERNEL ]]; then
    echo "boot-test: $KERNEL is missing, run make first" >&2
    exit 1
fi

mkdir -p "$(dirname "$LOG")"
rm -f "$LOG"

timeout "$TIMEOUT" qemu-system-x86_64 \
    -kernel "$KERNEL" \
    -serial "file:$LOG" \
    -display none \
    -no-reboot \
    -m 512M >/dev/null 2>&1 || true

if [[ ! -s $LOG ]]; then
    echo "boot-test: kernel produced no serial output" >&2
    exit 1
fi

expected=(
    "eris kernel"
    "builtin modules registered"
    "module vga"
    "module keyboard"
    "module desktop"
)

status=0
for line in "${expected[@]}"; do
    if grep -q "$line" "$LOG"; then
        echo "ok   $line"
    else
        echo "fail $line" >&2
        status=1
    fi
done

if grep -qiE "panic|tainted|init failed" "$LOG"; then
    echo "fail kernel reported a panic, a taint or a failed module" >&2
    grep -iE "panic|tainted|init failed" "$LOG" >&2
    status=1
fi

echo "--- serial log"
cat "$LOG"
exit $status
