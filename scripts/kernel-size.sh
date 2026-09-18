#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 farfromoffice
set -euo pipefail

KERNEL=${KERNEL:-build/eris.elf}

if [[ ! -f $KERNEL ]]; then
    echo "kernel-size: $KERNEL is missing, run make first" >&2
    exit 1
fi

symbol_address() {
    nm "$KERNEL" | awk -v name="$1" '$3 == name { print $1 }'
}

span() {
    local start end
    start=$(symbol_address "$1")
    end=$(symbol_address "$2")

    if [[ -z $start || -z $end ]]; then
        echo "missing"
        return 1
    fi

    echo $((16#$end - 16#$start))
}

echo "image"
size "$KERNEL"

echo
echo "framework tables"

status=0
modules_bytes=$(span __eris_modules_start __eris_modules_end) || status=1
symtab_bytes=$(span __eris_symtab_start __eris_symtab_end) || status=1
ctors_bytes=$(span __init_array_start __init_array_end) || status=1

echo "  module descriptors $modules_bytes bytes"
echo "  exported symbols   $symtab_bytes bytes"
echo "  global constructors $ctors_bytes bytes"

if [[ $status -ne 0 ]]; then
    echo "a linker marker is missing, the framework sections did not survive the link" >&2
    exit 1
fi

if [[ $modules_bytes -eq 0 ]]; then
    echo "no module descriptor made it into the image" >&2
    exit 1
fi

echo
echo "largest objects"
nm --size-sort --radix=d -S "$KERNEL" | tail -10 | awk '{ printf "  %-48s %d bytes\n", $4, $2 + 0 }'
