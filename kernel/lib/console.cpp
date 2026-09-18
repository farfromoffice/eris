// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/console.hpp>
#include <eris/lock.hpp>

namespace eris {
namespace {

constexpr usize max_consoles = 4;

constinit Console* consoles[max_consoles]{};
constinit usize console_count = 0;
constinit RecursiveIrqLock console_lock{};

} // namespace

void Console::write(const char* text)
{
    for (const char* p = text; *p != '\0'; ++p)
        put(*p);
}

void console_register(Console* console)
{
    RecursiveGuard guard(console_lock);

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
    RecursiveGuard guard(console_lock);

    for (usize i = 0; i < console_count; ++i) {
        if (consoles[i] != console)
            continue;

        for (usize j = i; j + 1 < console_count; ++j)
            consoles[j] = consoles[j + 1];
        --console_count;
        return;
    }
}

u64 console_begin()
{
    return console_lock.lock();
}

void console_end(u64 token)
{
    console_lock.unlock(token);
}

void console_put(char c)
{
    RecursiveGuard guard(console_lock);

    for (usize i = 0; i < console_count; ++i)
        consoles[i]->put(c);
}

// Taken once for the whole string, otherwise two cores interleave letter by
// letter and the log stops being readable.
void console_write(const char* text)
{
    RecursiveGuard guard(console_lock);

    for (const char* p = text; *p != '\0'; ++p) {
        for (usize i = 0; i < console_count; ++i)
            consoles[i]->put(*p);
    }
}

} // namespace eris
