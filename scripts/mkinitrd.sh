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

mkdir -p "$staging/modules"

for module in "$@"; do
    [[ -f $module ]] || continue
    cp "$module" "$staging/modules/"
done

mkdir -p "$(dirname "$output")"
tar --format=ustar -C "$staging" -cf "$output" modules

count=$(find "$staging/modules" -name '*.ko' | wc -l)
echo "initrd: $output with $count module$([[ $count -eq 1 ]] || echo s)"
