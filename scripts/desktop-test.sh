#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 farfromoffice
set -euo pipefail

# Brings the shell up on a framebuffer and looks at what it drew. The serial
# log says the modules arrived, the screen dump says they turned into a picture.

KERNEL=${KERNEL:-build/eris32.elf}
INITRD=${INITRD:-build/initrd.tar}
TIMEOUT=${TIMEOUT:-60}
LOG=${LOG:-build/desktop.log}
SHOT=${SHOT:-build/desktop.ppm}
MONITOR=${MONITOR:-build/desktop.monitor}

for file in "$KERNEL" "$INITRD"; do
    if [[ ! -f $file ]]; then
        echo "desktop-test: $file is missing, run make first" >&2
        exit 1
    fi
done

mkdir -p "$(dirname "$LOG")"
rm -f "$LOG" "$SHOT" "$MONITOR"

timeout "$TIMEOUT" qemu-system-x86_64 \
    -kernel "$KERNEL" \
    -initrd "$INITRD" \
    -serial "file:$LOG" \
    -monitor "unix:$MONITOR,server,nowait" \
    -display none \
    -no-reboot \
    -m 2G \
    -smp 2 \
    -vga std \
    -global VGA.vgamem_mb=64 >/dev/null 2>&1 &

qemu=$!

python3 - "$MONITOR" "$SHOT" <<'PY'
import os, socket, sys, time

monitor, shot = sys.argv[1], sys.argv[2]

for _ in range(120):
    if os.path.exists(monitor):
        break
    time.sleep(0.5)

# The shell needs the modules, the fonts and a few frames before there is
# anything worth looking at.
time.sleep(12)

client = socket.socket(socket.AF_UNIX)
client.connect(monitor)
time.sleep(0.5)
client.sendall(('screendump %s\n' % shot).encode())
time.sleep(4)
client.close()
PY

kill "$qemu" 2>/dev/null || true
wait "$qemu" 2>/dev/null || true

status=0

check() {
    if grep -qF "$1" "$LOG"; then
        echo "ok   $2"
    else
        echo "fail $2" >&2
        status=1
    fi
}

check "fbdev: 1024 by 768" "the adapter took the mode the shell asks for"
check "desktop: 1024 by 768" "the shell attached to the framebuffer"
check "desktop: system registered" "the system app registered itself"
check "desktop: modules registered" "the modules app registered itself"
check "desktop: console registered" "the console app registered itself"
check "desktop: about registered" "the about app registered itself"

if grep -qi "panic" "$LOG"; then
    echo "fail the kernel panicked" >&2
    grep -i -A6 "panic" "$LOG" >&2
    status=1
fi

if [[ ! -s $SHOT ]]; then
    echo "fail nothing came out of the screen dump" >&2
    exit 1
fi

python3 - "$SHOT" <<'PY' || status=1
import sys

data = open(sys.argv[1], 'rb').read()

fields = []
cursor = 0
while len(fields) < 4:
    end = cursor
    while data[end:end + 1] not in (b' ', b'\n', b'\t'):
        end += 1
    fields.append(data[cursor:end])
    cursor = end + 1

magic, width, height = fields[0], int(fields[1]), int(fields[2])
pixels = data[cursor:]

if magic != b'P6' or width != 1024 or height != 768:
    print('fail the dump is %s at %d by %d' % (magic, width, height), file=sys.stderr)
    sys.exit(1)

def row(y):
    start = y * width * 3
    return pixels[start:start + width * 3]

def lit(y):
    line = row(y)
    return sum(1 for i in range(0, len(line), 3) if line[i] + line[i + 1] + line[i + 2] > 90)

shades = len({pixels[i:i + 3] for i in range(0, len(pixels), 3 * 37)})

results = [
    ('the top bar carries text', lit(16) > 40),
    ('the icon column carries a label', lit(115) > 12),
    ('the taskbar carries entries', lit(743) > 120),
    ('the icons are drawn', lit(79) > 12),
    ('the wallpaper is more than one flat colour', shades > 24),
]

failed = False
for name, ok in results:
    print('%s %s' % ('ok  ' if ok else 'fail', name), file=sys.stderr if not ok else sys.stdout)
    failed = failed or not ok

sys.exit(1 if failed else 0)
PY

exit $status
