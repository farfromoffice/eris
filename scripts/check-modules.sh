#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 farfromoffice
set -euo pipefail

status=0
found=()

# The apps that ship with the desktop sit one level down, so both depths are
# walked and the nesting level does not change what a module has to declare.
for dir in modules/*/ modules/internal_apps/*/; do
    name=$(basename "$dir")

    if [[ $name == internal_apps ]]; then
        continue
    fi

    sources=$(grep -rl "ERIS_MODULE(" "$dir" || true)

    if [[ -z $sources ]]; then
        echo "module $name has no ERIS_MODULE descriptor" >&2
        status=1
        continue
    fi

    # shellcheck disable=SC2086
    descriptor=$(grep -rh -A1 "ERIS_MODULE(" $sources | tr '\n' ' ')
    declared=$(sed -n 's/.*ERIS_MODULE("\([^"]*\)".*/\1/p' <<<"$descriptor" | head -1)

    if [[ $declared != "$name" ]]; then
        echo "module directory $name declares itself as \"$declared\"" >&2
        status=1
    fi

    if ! grep -q 'ERIS_MODULE("[^"]*", *"[^"]*", *"[^"]*", *"[^"]*"' <<<"$descriptor"; then
        echo "module $name is missing a version, author or license field" >&2
        status=1
    fi

    if ! grep -q 'GPL\|MIT\|BSD\|Apache' <<<"$descriptor"; then
        echo "module $name declares a license the kernel treats as tainting" >&2
        status=1
    fi

    for dep in $(grep -o '"[a-z0-9_]*"' <<<"${descriptor#*_exit}" | tr -d '"'); do
        if [[ ! -d modules/$dep && ! -d modules/internal_apps/$dep ]]; then
            echo "module $name depends on $dep, which is not in the tree" >&2
            status=1
        fi
    done

    found+=("$name")
done

echo "checked ${#found[@]} modules: ${found[*]}"
exit $status
