// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/console.hpp>

namespace eris {
namespace {

constexpr usize max_consoles = 4;

constinit Console* consoles[max_consoles]{};
constinit usize console_count = 0;

} // namespace

void Console::write(const char* text)
{
    for (const char* p = text; *p != '\0'; ++p)
        put(*p);
}

void console_register(Console* console)
{
    if (console == nullptr || console_count >= max_consoles)
        return;

    for (usize i = 0; i < console_count; ++i) {
        if (consoles[i] == console)
            return;
    }

    consoles[console_count++] = console;
}

void console_unregister(Console* console)
{
    for (usize i = 0; i < console_count; ++i) {
        if (consoles[i] != console)
            continue;

        for (usize j = i; j + 1 < console_count; ++j)
            consoles[j] = consoles[j + 1];
        --console_count;
        return;
    }
}

void console_put(char c)
{
    for (usize i = 0; i < console_count; ++i)
        consoles[i]->put(c);
}

void console_write(const char* text)
{
    for (const char* p = text; *p != '\0'; ++p)
        console_put(*p);
}

} // namespace eris
