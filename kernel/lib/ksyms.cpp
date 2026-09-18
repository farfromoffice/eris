// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/ksyms.hpp>

namespace eris {

// The table itself is generated from the first link pass by
// scripts/gen-ksyms.sh and compiled into the image on the second one.
extern const KsymEntry ksyms_table[];
extern const usize ksyms_table_count;

const char* ksyms_lookup(u64 address, u64& offset)
{
    const KsymEntry* best = nullptr;

    for (usize i = 0; i < ksyms_table_count; ++i) {
        if (ksyms_table[i].address > address)
            break;
        best = &ksyms_table[i];
    }

    if (best == nullptr)
        return nullptr;

    offset = address - best->address;
    return best->name;
}

usize ksyms_count()
{
    return ksyms_table_count;
}

} // namespace eris
