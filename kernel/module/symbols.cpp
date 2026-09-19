// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/export.hpp>
#include <eris/string.hpp>

extern "C" {
extern eris::ExportedSymbol __eris_symtab_start[];
extern eris::ExportedSymbol __eris_symtab_end[];
}

namespace eris {
namespace {

constexpr usize max_tables = 16;

struct RegisteredTable {
    const ExportedSymbol* table;
    usize count;
    virt_addr owner;
};

constinit RegisteredTable tables[max_tables]{};
constinit usize table_count = 0;

} // namespace

bool symbol_register_table(const ExportedSymbol* table, usize count, virt_addr owner)
{
    if (table == nullptr || count == 0)
        return false;

    if (table_count >= max_tables)
        return false;

    tables[table_count++] = RegisteredTable{table, count, owner};
    return true;
}

void symbol_unregister_owner(virt_addr owner)
{
    for (usize i = 0; i < table_count;) {
        if (tables[i].owner == owner)
            tables[i] = tables[--table_count];
        else
            ++i;
    }
}

void* symbol_lookup(const char* name)
{
    for (ExportedSymbol* symbol = __eris_symtab_start; symbol != __eris_symtab_end; ++symbol) {
        if (strcmp(symbol->name, name) == 0)
            return symbol->address;
    }

    // Then whatever the loaded images brought with them.
    for (usize i = 0; i < table_count; ++i) {
        for (usize entry = 0; entry < tables[i].count; ++entry) {
            const ExportedSymbol& symbol = tables[i].table[entry];
            if (symbol.name != nullptr && strcmp(symbol.name, name) == 0)
                return symbol.address;
        }
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
