// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/export.hpp>
#include <eris/string.hpp>

extern "C" {
extern eris::ExportedSymbol __eris_symtab_start[];
extern eris::ExportedSymbol __eris_symtab_end[];
}

namespace eris {

void* symbol_lookup(const char* name)
{
    for (ExportedSymbol* symbol = __eris_symtab_start; symbol != __eris_symtab_end; ++symbol) {
        if (strcmp(symbol->name, name) == 0)
            return symbol->address;
    }
    return nullptr;
}

usize symbol_count()
{
    return static_cast<usize>(__eris_symtab_end - __eris_symtab_start);
}

const ExportedSymbol* symbol_at(usize index)
{
    return index < symbol_count() ? &__eris_symtab_start[index] : nullptr;
}

} // namespace eris
