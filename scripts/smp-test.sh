#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 farfromoffice
set -euo pipefail

# Boots on several cores and makes every one of them hammer the structures the
# locks are supposed to protect.

KERNEL=${KERNEL:-build/eris32.elf}
CPUS=${CPUS:-4}
TIMEOUT=${TIMEOUT:-40}
LOG=${LOG:-build/smp.log}

if [[ ! -f $KERNEL ]]; then
    echo "smp-test: $KERNEL is missing, run make first" >&2
    exit 1
fi

mkdir -p "$(dirname "$LOG")"
rm -f "$LOG"

timeout "$TIMEOUT" qemu-system-x86_64 \
    -kernel "$KERNEL" \
    -serial "file:$LOG" \
    -display none \
    -no-reboot \
    -m 512M \
    -smp "$CPUS" \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -append "smptest mmtest timetest panic_exit test_exit" >/dev/null 2>&1 || true

status=0

if [[ $CPUS -eq 1 ]]; then
    if ! grep -q "smp: one cpu" "$LOG"; then
        echo "fail the single core path did not report" >&2
        status=1
    else
        echo "ok   single core"
    fi
elif ! grep -q "smp: $CPUS of $CPUS cpus online" "$LOG"; then
    echo "fail not every core came up" >&2
    status=1
else
    echo "ok   $CPUS cores online"
fi

if ! grep -q "smp selftest" "$LOG"; then
    echo "fail the cross core stress test did not report" >&2
    status=1
else
    sed -n 's/^\[inf\] \(smp selftest.*\)/ok   \1/p' "$LOG"
fi

if grep -q "did not come back to where it started" "$LOG"; then
    echo "fail a refcount was lost under concurrency" >&2
    status=1
fi

if grep -qi "panic" "$LOG"; then
    echo "fail the kernel panicked" >&2
    grep -i -A5 "panic" "$LOG" >&2
    status=1
fi

exit $status
