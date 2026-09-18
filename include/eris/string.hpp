// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

extern "C" {
void* memset(void* dest, int value, eris::usize count);
void* memcpy(void* dest, const void* src, eris::usize count);
void* memmove(void* dest, const void* src, eris::usize count);
int memcmp(const void* a, const void* b, eris::usize count);
eris::usize strlen(const char* s);
int strcmp(const char* a, const char* b);
}
