#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 farfromoffice
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "usage: $0 <name> [dependency ...]" >&2
    exit 1
fi

name=$1
shift
deps=("$@")

if [[ ! $name =~ ^[a-z][a-z0-9_]*$ ]]; then
    echo "module names are lower case, digits and underscores only" >&2
    exit 1
fi

dir="modules/$name"
if [[ -d $dir ]]; then
    echo "$dir already exists" >&2
    exit 1
fi

dep_list=""
for dep in "${deps[@]}"; do
    if [[ ! -d modules/$dep ]]; then
        echo "dependency $dep is not in the tree" >&2
        exit 1
    fi
    dep_list+=", \"$dep\""
done

mkdir -p "$dir"

cat > "$dir/$name.cpp" <<EOF
// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/module.hpp>
#include <eris/printk.hpp>

namespace eris::modules {
namespace {

class ${name^} {
public:
    int start()
    {
        return 0;
    }

    void stop()
    {
    }
};

${name^} instance{};

int ${name}_module_init()
{
    return instance.start();
}

void ${name}_module_exit()
{
    instance.stop();
}

} // namespace
} // namespace eris::modules

ERIS_MODULE("$name", "0.1", "farfromoffice", "GPL-2.0-only",
            eris::modules::${name}_module_init, eris::modules::${name}_module_exit${dep_list});
EOF

echo "created $dir/$name.cpp"
echo "next: implement the class, export the ABI, update CLAUDE.md, run make and ./scripts/boot-test.sh"
