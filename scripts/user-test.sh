#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 farfromoffice
set -euo pipefail

# Runs the first program in ring 3 and then one that faults on purpose, which
# together say the kernel can start a program and survive it.

KERNEL=${KERNEL:-build/eris32.elf}
INITRD=${INITRD:-build/initrd.tar}
TIMEOUT=${TIMEOUT:-40}
LOG=${LOG:-build/user.log}
DISK=${DISK:-build/user-disk.img}

for file in "$KERNEL" "$INITRD"; do
    if [[ ! -f $file ]]; then
        echo "user-test: $file is missing, run make first" >&2
        exit 1
    fi
done

mkdir -p "$(dirname "$LOG")"
rm -f "$LOG" "$DISK"

disk_args=()
if command -v mke2fs >/dev/null && command -v debugfs >/dev/null; then
    dd if=/dev/zero of="$DISK" bs=1M count=8 status=none
    mke2fs -q -t ext2 -b 1024 "$DISK"

    content=$(mktemp)
    trap 'rm -f "$content"' EXIT
    printf 'eris reads ext2\n' > "$content"
    debugfs -w -R "write $content hello.txt" "$DISK" >/dev/null 2>&1

    disk_args=(-drive "file=$DISK,if=virtio,format=raw")
fi

timeout "$TIMEOUT" qemu-system-x86_64 \
    -kernel "$KERNEL" \
    -initrd "$INITRD" \
    "${disk_args[@]}" \
    -serial "file:$LOG" \
    -display none \
    -no-reboot \
    -m 512M \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -append "crashtest panic_exit test_exit" >/dev/null 2>&1 || true

status=0

check() {
    if grep -qF "$1" "$LOG"; then
        echo "ok   $2"
    else
        echo "fail $2" >&2
        status=1
    fi
}

check "syscall: entry at" "the call gate is installed"
check "proc: init starts at 8000000000" "the program loaded above the kernel"
check "init: hello from ring 3" "it printed through a syscall"
check "init: pid is 1" "it learned its own identity"
check "init: slept" "it slept and came back"
check "proc: init left with code 42" "its exit code reached the kernel"
check "crash: about to write" "the faulting program started"
check "was killed" "the fault killed the process"
check "still running after the program died" "the kernel survived it"

if [[ ${#disk_args[@]} -gt 0 ]]; then
    check "init: /mnt/hello.txt says eris reads ext2" "it read a file off the disk"
fi

if grep -q "kernel panic" "$LOG"; then
    echo "fail the kernel panicked" >&2
    grep -A6 "kernel panic" "$LOG" >&2
    status=1
fi

exit $status
