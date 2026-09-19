#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 farfromoffice
set -euo pipefail

# Builds an ext2 image with known contents, boots with it on the virtio disk,
# and checks the kernel reads it back through the mount.

KERNEL=${KERNEL:-build/eris32.elf}
INITRD=${INITRD:-build/initrd.tar}
TIMEOUT=${TIMEOUT:-40}
LOG=${LOG:-build/fs.log}
DISK=${DISK:-build/fs-disk.img}

for file in "$KERNEL" "$INITRD"; do
    if [[ ! -f $file ]]; then
        echo "fs-test: $file is missing, run make first" >&2
        exit 1
    fi
done

for tool in mke2fs debugfs; do
    if ! command -v "$tool" >/dev/null; then
        echo "fs-test: $tool is not installed, skipping" >&2
        exit 0
    fi
done

mkdir -p "$(dirname "$LOG")"
rm -f "$LOG" "$DISK"

dd if=/dev/zero of="$DISK" bs=1M count=8 status=none
mke2fs -q -t ext2 -b 1024 "$DISK"

content=$(mktemp)
trap 'rm -f "$content"' EXIT
printf 'eris reads ext2\n' > "$content"

debugfs -w -R "write $content hello.txt" "$DISK" >/dev/null 2>&1
debugfs -w -R "mkdir docs" "$DISK" >/dev/null 2>&1

timeout "$TIMEOUT" qemu-system-x86_64 \
    -kernel "$KERNEL" \
    -initrd "$INITRD" \
    -drive "file=$DISK,if=virtio,format=raw" \
    -serial "file:$LOG" \
    -display none \
    -no-reboot \
    -m 512M \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -append "fstest panic_exit test_exit" >/dev/null 2>&1 || true

status=0

check() {
    if grep -qF "$1" "$LOG"; then
        echo "ok   $2"
    else
        echo "fail $2" >&2
        status=1
    fi
}

check "vfs: ramfs mounted at /" "ramfs is the root"
check "block: vda" "the disk reached the block layer"
check "vfs: devfs mounted at /dev" "devfs is mounted"
check "ext2: 8192 blocks" "the superblock was read"
check "vfs: ext2 mounted at /mnt" "the disk is mounted"
check '/notes.txt reads "written into ramfs"' "a file in memory reads back"
check "/dev holds vda" "the disk has a device node"
check '/mnt/hello.txt is 16 bytes and reads "eris reads ext2"' "a file on the disk reads back"
check "/mnt holds docs" "a directory listing works"
check "this line went through /dev/console" "writing a device node prints"

if grep -qi "panic" "$LOG"; then
    echo "fail the kernel panicked" >&2
    grep -i -A6 "panic" "$LOG" >&2
    status=1
fi

exit $status
