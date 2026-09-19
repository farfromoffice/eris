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

mkdir -p "$staging/modules" "$staging/bin" "$staging/fonts"

for file in "$@"; do
    [[ -f $file ]] || continue

    case $file in
        *.ko) cp "$file" "$staging/modules/" ;;
        *.ttf|*.otf) cp "$file" "$staging/fonts/" ;;
        *) cp "$file" "$staging/bin/" ;;
    esac
done

mkdir -p "$(dirname "$output")"
tar --format=ustar -C "$staging" -cf "$output" modules bin fonts

modules=$(find "$staging/modules" -name '*.ko' | wc -l)
programs=$(find "$staging/bin" -type f | wc -l)
faces=$(find "$staging/fonts" -type f | wc -l)

echo "initrd: $output with $modules module$([[ $modules -eq 1 ]] || echo s), \
$programs program$([[ $programs -eq 1 ]] || echo s) and $faces font$([[ $faces -eq 1 ]] || echo s)"
