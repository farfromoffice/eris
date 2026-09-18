// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/string.hpp>

using eris::usize;

extern "C" {

void* memset(void* dest, int value, usize count)
{
    auto* out = static_cast<unsigned char*>(dest);
    const auto byte = static_cast<unsigned char>(value);
    for (usize i = 0; i < count; ++i)
        out[i] = byte;
    return dest;
}

void* memcpy(void* dest, const void* src, usize count)
{
    auto* out = static_cast<unsigned char*>(dest);
    const auto* in = static_cast<const unsigned char*>(src);
    for (usize i = 0; i < count; ++i)
        out[i] = in[i];
    return dest;
}

void* memmove(void* dest, const void* src, usize count)
{
    auto* out = static_cast<unsigned char*>(dest);
    const auto* in = static_cast<const unsigned char*>(src);
    if (out < in) {
        for (usize i = 0; i < count; ++i)
            out[i] = in[i];
    } else {
        for (usize i = count; i > 0; --i)
            out[i - 1] = in[i - 1];
    }
    return dest;
}

int memcmp(const void* a, const void* b, usize count)
{
    const auto* lhs = static_cast<const unsigned char*>(a);
    const auto* rhs = static_cast<const unsigned char*>(b);
    for (usize i = 0; i < count; ++i) {
        if (lhs[i] != rhs[i])
            return lhs[i] < rhs[i] ? -1 : 1;
    }
    return 0;
}

usize strlen(const char* s)
{
    usize length = 0;
    while (s[length] != '\0')
        ++length;
    return length;
}

int strcmp(const char* a, const char* b)
{
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}

}
