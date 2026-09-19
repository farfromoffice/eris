// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/compiler.hpp>
#include <eris/types.hpp>

namespace eris::elf {

struct ERIS_PACKED Header {
    u8 ident[16];
    u16 type;
    u16 machine;
    u32 version;
    u64 entry;
    u64 program_header_offset;
    u64 section_header_offset;
    u32 flags;
    u16 header_size;
    u16 program_header_size;
    u16 program_header_count;
    u16 section_header_size;
    u16 section_header_count;
    u16 section_name_index;
};

struct ERIS_PACKED SectionHeader {
    u32 name;
    u32 type;
    u64 flags;
    u64 address;
    u64 offset;
    u64 size;
    u32 link;
    u32 info;
    u64 alignment;
    u64 entry_size;
};

struct ERIS_PACKED Symbol {
    u32 name;
    u8 info;
    u8 other;
    u16 section;
    u64 value;
    u64 size;
};

struct ERIS_PACKED Rela {
    u64 offset;
    u64 info;
    i64 addend;
};

inline constexpr u16 type_relocatable = 1;
inline constexpr u16 machine_x86_64 = 62;

inline constexpr u32 section_progbits = 1;
inline constexpr u32 section_symtab = 2;
inline constexpr u32 section_strtab = 3;
inline constexpr u32 section_rela = 4;
inline constexpr u32 section_nobits = 8;

inline constexpr u64 section_flag_alloc = 0x2;
inline constexpr u64 section_flag_write = 0x1;
inline constexpr u64 section_flag_exec = 0x4;

inline constexpr u16 section_undefined = 0;

// The relocation types a kernel module built for this tree can carry.
inline constexpr u32 r_x86_64_64 = 1;
inline constexpr u32 r_x86_64_pc32 = 2;
inline constexpr u32 r_x86_64_plt32 = 4;
inline constexpr u32 r_x86_64_32 = 10;
inline constexpr u32 r_x86_64_32s = 11;
inline constexpr u32 r_x86_64_pc64 = 24;

constexpr u32 rela_type(u64 info)
{
    return static_cast<u32>(info & 0xFFFFFFFF);
}

constexpr u32 rela_symbol(u64 info)
{
    return static_cast<u32>(info >> 32);
}

constexpr u8 symbol_type(u8 info)
{
    return info & 0x0F;
}

constexpr u8 symbol_binding(u8 info)
{
    return info >> 4;
}

inline constexpr u8 symbol_type_section = 3;

bool header_looks_sane(const Header& header, usize length);

} // namespace eris::elf
