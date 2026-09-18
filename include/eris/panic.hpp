// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/compiler.hpp>
#include <eris/irq.hpp>

namespace eris {

ERIS_NORETURN ERIS_PRINTF(1, 2) void panic(const char* fmt, ...);
ERIS_NORETURN ERIS_PRINTF(2, 3) void panic_with_registers(const arch::Registers& regs,
                                                          const char* fmt, ...);

} // namespace eris
