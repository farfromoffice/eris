// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/initrd.hpp>
#include <eris/multiboot.hpp>
#include <eris/paging.hpp>
#include <eris/printk.hpp>
#include <eris/string.hpp>

namespace eris {
namespace {

constexpr usize block_size = 512;

struct ERIS_PACKED TarHeader {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char type;
    char link_name[100];
    char magic[6];
    char version[2];
    char owner[32];
    char group[32];
    char device_major[8];
    char device_minor[8];
    char prefix[155];
    char padding[12];
};

constinit const u8* archive = nullptr;
constinit usize archive_length = 0;
constinit usize file_count = 0;

usize parse_octal(const char* text, usize length)
{
    usize value = 0;
    for (usize i = 0; i < length && text[i] >= '0' && text[i] <= '7'; ++i)
        value = value * 8 + static_cast<usize>(text[i] - '0');
    return value;
}

usize round_to_block(usize bytes)
{
    return (bytes + block_size - 1) / block_size * block_size;
}

bool entry_at(usize offset, const TarHeader*& header, usize& size)
{
    if (archive == nullptr || offset + block_size > archive_length)
        return false;

    header = reinterpret_cast<const TarHeader*>(archive + offset);
    if (header->name[0] == '\0')
        return false;

    size = parse_octal(header->size, sizeof(header->size));
    return offset + block_size + size <= archive_length;
}

// Tar keeps a directory prefix in the name, and a module is looked up by the
// bare file name it was packed under.
bool name_matches(const char* stored, const char* wanted)
{
    const char* base = stored;
    for (const char* p = stored; *p != '\0'; ++p) {
        if (*p == '/')
            base = p + 1;
    }

    const usize length = strlen(wanted);
    if (memcmp(base, wanted, length) != 0)
        return false;

    const char rest = base[length];
    return rest == '\0' || (rest == '.' && memcmp(base + length, ".ko", 4) == 0);
}

} // namespace

void initrd_init(u32 multiboot_magic, u64 multiboot_info)
{
    if (multiboot_magic != multiboot_bootloader_magic)
        return;

    const auto* info = reinterpret_cast<const MultibootInfo*>(mm::phys_to_virt(multiboot_info));
    if ((info->flags & multiboot_flag_mods) == 0 || info->mods_count == 0)
        return;

    const auto* mods = reinterpret_cast<const MultibootModule*>(
        mm::phys_to_virt(info->mods_addr));

    archive = reinterpret_cast<const u8*>(mm::phys_to_virt(mods[0].mod_start));
    archive_length = mods[0].mod_end - mods[0].mod_start;

    usize offset = 0;
    const TarHeader* header = nullptr;
    usize size = 0;

    while (entry_at(offset, header, size)) {
        if (header->type == '0' || header->type == '\0')
            ++file_count;
        offset += block_size + round_to_block(size);
    }

    pr_info("initrd: %lu KiB with %lu file%s\n",
            static_cast<u64>(archive_length / 1024),
            static_cast<u64>(file_count),
            file_count == 1 ? "" : "s");
}

bool initrd_available()
{
    return archive != nullptr && file_count > 0;
}

usize initrd_file_count()
{
    return file_count;
}

const void* initrd_find(const char* name, usize& length)
{
    usize offset = 0;
    const TarHeader* header = nullptr;
    usize size = 0;

    while (entry_at(offset, header, size)) {
        if ((header->type == '0' || header->type == '\0') && name_matches(header->name, name)) {
            length = size;
            return archive + offset + block_size;
        }

        offset += block_size + round_to_block(size);
    }

    length = 0;
    return nullptr;
}

const void* initrd_data_at(usize index, const char*& name, usize& length)
{
    usize offset = 0;
    usize seen = 0;
    const TarHeader* header = nullptr;
    usize size = 0;

    while (entry_at(offset, header, size)) {
        if (header->type == '0' || header->type == '\0') {
            if (seen == index) {
                name = header->name;
                length = size;
                return archive + offset + block_size;
            }
            ++seen;
        }

        offset += block_size + round_to_block(size);
    }

    name = nullptr;
    length = 0;
    return nullptr;
}

const char* initrd_name_at(usize index, usize& length)
{
    usize offset = 0;
    usize seen = 0;
    const TarHeader* header = nullptr;
    usize size = 0;

    while (entry_at(offset, header, size)) {
        if (header->type == '0' || header->type == '\0') {
            if (seen == index) {
                length = size;
                return header->name;
            }
            ++seen;
        }

        offset += block_size + round_to_block(size);
    }

    length = 0;
    return nullptr;
}

} // namespace eris
