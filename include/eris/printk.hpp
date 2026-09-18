// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/compiler.hpp>
#include <eris/types.hpp>

namespace eris {

enum class LogLevel : u8 { Debug, Info, Warn, Error };

ERIS_PRINTF(2, 3) void printk(LogLevel level, const char* fmt, ...);
ERIS_PRINTF(1, 0) void vprintk(const char* fmt, __builtin_va_list args);

}

#define pr_debug(...) ::eris::printk(::eris::LogLevel::Debug, __VA_ARGS__)
#define pr_info(...)  ::eris::printk(::eris::LogLevel::Info, __VA_ARGS__)
#define pr_warn(...)  ::eris::printk(::eris::LogLevel::Warn, __VA_ARGS__)
#define pr_err(...)   ::eris::printk(::eris::LogLevel::Error, __VA_ARGS__)
