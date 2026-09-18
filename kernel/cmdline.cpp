// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/cmdline.hpp>
#include <eris/multiboot.hpp>
#include <eris/string.hpp>

namespace eris {
namespace {

constexpr usize cmdline_capacity = 512;
constexpr usize max_words = 32;

constinit char cmdline[cmdline_capacity]{};

// The words are cut apart in place, so a value is a plain terminated string
// instead of a pointer into the middle of the line.
constinit char words[cmdline_capacity]{};
constinit const char* word_table[max_words]{};
constinit usize word_count = 0;

void split()
{
    word_count = 0;

    usize i = 0;
    while (words[i] != '\0' && word_count < max_words) {
        while (words[i] == ' ')
            ++i;

        if (words[i] == '\0')
            break;

        word_table[word_count++] = &words[i];

        while (words[i] != '\0' && words[i] != ' ')
            ++i;

        if (words[i] == ' ')
            words[i++] = '\0';
    }
}

// Returns what follows the key in its word, or null when the key is absent.
const char* find(const char* key)
{
    const usize length = strlen(key);

    for (usize i = 0; i < word_count; ++i) {
        const char* word = word_table[i];
        if (strlen(word) < length || memcmp(word, key, length) != 0)
            continue;

        const char* rest = word + length;
        if (*rest == '\0' || *rest == '=')
            return rest;
    }

    return nullptr;
}

} // namespace

void cmdline_init(u32 multiboot_magic, u64 multiboot_info)
{
    cmdline[0] = '\0';
    words[0] = '\0';
    word_count = 0;

    if (multiboot_magic != multiboot_bootloader_magic)
        return;

    const auto* info = reinterpret_cast<const MultibootInfo*>(multiboot_info);
    if ((info->flags & multiboot_flag_cmdline) == 0 || info->cmdline == 0)
        return;

    const auto* source = reinterpret_cast<const char*>(static_cast<u64>(info->cmdline));
    usize i = 0;
    while (i + 1 < cmdline_capacity && source[i] != '\0') {
        cmdline[i] = source[i];
        ++i;
    }
    cmdline[i] = '\0';

    memcpy(words, cmdline, i + 1);
    split();
}

const char* cmdline_raw()
{
    return cmdline;
}

bool cmdline_has(const char* key)
{
    return find(key) != nullptr;
}

const char* cmdline_value(const char* key)
{
    const char* rest = find(key);
    if (rest == nullptr || *rest != '=')
        return nullptr;
    return rest + 1;
}

} // namespace eris
