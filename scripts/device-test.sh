#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 farfromoffice
set -euo pipefail

# Walks the bus, loads the drivers out of the initrd and makes one of them talk
# to a real device: a disk image with known contents, read and written back.

KERNEL=${KERNEL:-build/eris32.elf}
INITRD=${INITRD:-build/initrd.tar}
TIMEOUT=${TIMEOUT:-40}
LOG=${LOG:-build/device.log}
DISK=${DISK:-build/test-disk.img}

for file in "$KERNEL" "$INITRD"; do
    if [[ ! -f $file ]]; then
        echo "device-test: $file is missing, run make first" >&2
        exit 1
    fi
done

mkdir -p "$(dirname "$LOG")"
rm -f "$LOG"

python3 - "$DISK" <<'PY'
import sys

image = bytearray(64 * 1024)
image[0:18] = b'ERIS-DISK-SECTOR-0'

for sector in range(1, 8):
    tag = f'sector {sector} of the test disk'.encode()
    image[sector * 512:sector * 512 + len(tag)] = tag

open(sys.argv[1], 'wb').write(bytes(image))
PY

timeout "$TIMEOUT" qemu-system-x86_64 \
    -kernel "$KERNEL" \
    -initrd "$INITRD" \
    -drive "file=$DISK,if=virtio,format=raw" \
    -serial "file:$LOG" \
    -display none \
    -no-reboot \
    -m 512M \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -append "blktest panic_exit test_exit" >/dev/null 2>&1 || true

status=0

check() {
    if grep -qF "$1" "$LOG"; then
        echo "ok   $2"
    else
        echo "fail $2" >&2
        status=1
    fi
}

check "pci:" "the bus was walked"
check "1af4:1001" "the virtio disk was seen"
check "virtio_blk: 128 sectors" "the driver attached and read the capacity"
check 'sector 0 reads "ERIS-DISK-SECTOR-0"' "a read returned what the image holds"
check "sector 4 came back byte for byte" "a write and read back round trip matched"
check "rtc: 20" "the clock chip driver came up"

if grep -qi "panic" "$LOG"; then
    echo "fail the kernel panicked" >&2
    grep -i -A6 "panic" "$LOG" >&2
    status=1
fi

exit $status
