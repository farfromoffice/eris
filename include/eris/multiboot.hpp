// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/compiler.hpp>
#include <eris/types.hpp>

namespace eris {

struct ERIS_PACKED MultibootInfo {
    u32 flags;
    u32 mem_lower;
    u32 mem_upper;
    u32 boot_device;
    u32 cmdline;
    u32 mods_count;
    u32 mods_addr;
    u32 syms[4];
    u32 mmap_length;
    u32 mmap_addr;
};

struct ERIS_PACKED MultibootModule {
    u32 mod_start;
    u32 mod_end;
    u32 string;
    u32 reserved;
};

struct ERIS_PACKED MultibootMmapEntry {
    u32 size;
    u64 addr;
    u64 len;
    u32 type;
};

inline constexpr u32 multiboot_bootloader_magic = 0x2BADB002;
inline constexpr u32 multiboot_flag_cmdline = 1u << 2;
inline constexpr u32 multiboot_flag_mods = 1u << 3;
inline constexpr u32 multiboot_flag_mmap = 1u << 6;
inline constexpr u32 multiboot_mmap_type_available = 1;

} // namespace eris
