// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

namespace eris {

class Console {
public:
    virtual ~Console() = default;
    virtual void put(char c) = 0;

    void write(const char* text);
};

void console_register(Console* console);
void console_unregister(Console* console);
void console_put(char c);
void console_write(const char* text);

} // namespace eris
