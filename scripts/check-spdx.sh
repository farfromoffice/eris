#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 farfromoffice
set -euo pipefail

status=0
count=0

while IFS= read -r file; do
    count=$((count + 1))
    if ! head -5 "$file" | grep -q "SPDX-License-Identifier: GPL-2.0-only"; then
        echo "missing SPDX header: $file" >&2
        status=1
    fi
done < <(git ls-files '*.cpp' '*.hpp' '*.asm' '*.ld' '*.sh' 'Makefile')

if [[ $status -eq 0 ]]; then
    echo "$count tracked sources carry an SPDX header"
fi

exit $status
