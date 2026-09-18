#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 farfromoffice
set -euo pipefail

# Drives the panic path on purpose. Each kind triggers a different fault and
# the log has to come back with a panic, a register dump and a backtrace.

KERNEL=${KERNEL:-build/eris32.elf}
TIMEOUT=${TIMEOUT:-20}
LOGDIR=${LOGDIR:-build/faults}

kinds=(unmapped opcode divide doublefault stack panic)

if [[ $# -gt 0 ]]; then
    kinds=("$@")
fi

if [[ ! -f $KERNEL ]]; then
    echo "faultinject: $KERNEL is missing, run make first" >&2
    exit 1
fi

mkdir -p "$LOGDIR"
status=0

for kind in "${kinds[@]}"; do
    log="$LOGDIR/$kind.log"
    rm -f "$log"

    timeout "$TIMEOUT" qemu-system-x86_64 \
        -kernel "$KERNEL" \
        -serial "file:$log" \
        -display none \
        -no-reboot \
        -m 512M \
        -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
        -append "fault=$kind panic_exit" >/dev/null 2>&1 || true

    if ! grep -q "kernel panic" "$log"; then
        echo "fail $kind did not reach the panic path" >&2
        status=1
        continue
    fi

    if ! grep -q "call trace:" "$log"; then
        echo "fail $kind panicked without a backtrace" >&2
        status=1
        continue
    fi

    reason=$(sed -n 's/^\*\*\* kernel panic: //p' "$log" | head -1)
    echo "ok   $kind: $reason"
done

if [[ $status -ne 0 ]]; then
    echo "logs are under $LOGDIR" >&2
fi

exit $status
