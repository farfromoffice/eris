#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 farfromoffice
set -euo pipefail

# Spawns threads that count, sleep and block, then checks the numbers add up.

KERNEL=${KERNEL:-build/eris32.elf}
CPUS=${CPUS:-1}
TIMEOUT=${TIMEOUT:-40}
LOG=${LOG:-build/thread.log}

if [[ ! -f $KERNEL ]]; then
    echo "thread-test: $KERNEL is missing, run make first" >&2
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
    -append "threadtest panic_exit test_exit" >/dev/null 2>&1 || true

status=0

if ! grep -q "sched: round robin" "$LOG"; then
    echo "fail the scheduler never started" >&2
    status=1
fi

if ! grep -q "6 of 6 threads finished" "$LOG"; then
    echo "fail not every thread finished" >&2
    status=1
else
    sed -n 's/^\[inf\] \(thread selftest.*\)/ok   \1/p' "$LOG"
fi

if grep -q "lost .* rounds" "$LOG"; then
    echo "fail the counters lost work" >&2
    status=1
fi

if grep -q "woke before the gate opened" "$LOG"; then
    echo "fail a blocked thread ran before it was woken" >&2
    status=1
fi

if grep -qi "panic" "$LOG"; then
    echo "fail the kernel panicked" >&2
    grep -i -A6 "panic" "$LOG" >&2
    status=1
fi

exit $status
