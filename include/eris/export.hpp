// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

namespace eris {

struct ExportedSymbol {
    const char* name;
    void* address;
};

void* symbol_lookup(const char* name);

// A loaded image brings its own export table, which joins the kernel one for
// as long as the module is around.
bool symbol_register_table(const ExportedSymbol* table, usize count, virt_addr owner);
void symbol_unregister_owner(virt_addr owner);
usize symbol_count();
const ExportedSymbol* symbol_at(usize index);

} // namespace eris

#define ERIS_EXPORT_SYMBOL(sym)                                              \
    extern "C" {                                                             \
    ::eris::ExportedSymbol eris_export_##sym                                 \
        __attribute__((section(".eris_symtab"), used, aligned(8))) = {       \
            #sym, reinterpret_cast<void*>(sym) };                            \
    }
