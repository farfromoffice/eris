// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/console.hpp>
#include <eris/types.hpp>

namespace eris {

void backtrace();
void backtrace_from(u64 rip, u64 rbp);
void print_symbol(u64 address);

} // namespace eris
