#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 farfromoffice
set -euo pipefail

# Boots with an initrd and checks the loader end to end: a module that was never
# linked into the image loads, runs, unloads without leaking and comes back.

KERNEL=${KERNEL:-build/eris32.elf}
INITRD=${INITRD:-build/initrd.tar}
TIMEOUT=${TIMEOUT:-40}
LOG=${LOG:-build/module.log}

for file in "$KERNEL" "$INITRD"; do
    if [[ ! -f $file ]]; then
        echo "module-test: $file is missing, run make first" >&2
        exit 1
    fi
done

mkdir -p "$(dirname "$LOG")"
rm -f "$LOG"

timeout "$TIMEOUT" qemu-system-x86_64 \
    -kernel "$KERNEL" \
    -initrd "$INITRD" \
    -serial "file:$LOG" \
    -display none \
    -no-reboot \
    -m 512M \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -append "kotest panic_exit test_exit" >/dev/null 2>&1 || true

status=0

check() {
    if grep -qF "$1" "$LOG"; then
        echo "ok   $2"
    else
        echo "fail $2" >&2
        status=1
    fi
}

check "initrd:" "the archive was found"
check "module loader: modules/desktop.ko" "the out of tree image loaded"
check "module desktop 0.1 loaded" "its init ran"
check "module desktop unloaded" "it unloaded"
check "an image with the wrong abi was refused" "a stale abi is refused"
check "reloaded" "it came back"

accounting=$(grep -o '[0-9]* pages free before and [0-9]* after' "$LOG" | head -1)
before=$(awk '{ print $1 }' <<<"$accounting")
after=$(awk '{ print $6 }' <<<"$accounting")

if [[ -n $before && -n $after ]]; then
    if [[ $before -eq $after ]]; then
        echo "ok   the round trip leaked nothing, $after pages free"
    else
        echo "fail the round trip lost $((before - after)) pages" >&2
        status=1
    fi
else
    echo "fail no page accounting in the log" >&2
    status=1
fi

if grep -qi "panic" "$LOG"; then
    echo "fail the kernel panicked" >&2
    grep -i -A6 "panic" "$LOG" >&2
    status=1
fi

exit $status
