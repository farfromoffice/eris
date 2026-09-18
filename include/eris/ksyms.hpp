// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

namespace eris {

struct KsymEntry {
    u64 address;
    const char* name;
};

// Nearest symbol at or below the address, with how far past it we landed.
const char* ksyms_lookup(u64 address, u64& offset);
usize ksyms_count();

} // namespace eris
