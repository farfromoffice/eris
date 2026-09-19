#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 farfromoffice
set -euo pipefail

# Packs the loadable modules into the archive the kernel reads at boot. Plain
# tar on purpose: nothing that reads it exists yet.

if [[ $# -lt 1 ]]; then
    echo "usage: $0 <output.tar> [module.ko ...]" >&2
    exit 1
fi

output=$1
shift

staging=$(mktemp -d)
trap 'rm -rf "$staging"' EXIT

mkdir -p "$staging/modules" "$staging/bin"

for file in "$@"; do
    [[ -f $file ]] || continue

    if [[ $file == *.ko ]]; then
        cp "$file" "$staging/modules/"
    else
        cp "$file" "$staging/bin/"
    fi
done

mkdir -p "$(dirname "$output")"
tar --format=ustar -C "$staging" -cf "$output" modules bin

modules=$(find "$staging/modules" -name '*.ko' | wc -l)
programs=$(find "$staging/bin" -type f | wc -l)
echo "initrd: $output with $modules module$([[ $modules -eq 1 ]] || echo s) and $programs program$([[ $programs -eq 1 ]] || echo s)"
