// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/elf.hpp>
#include <eris/export.hpp>
#include <eris/mm.hpp>
#include <eris/module.hpp>
#include <eris/paging.hpp>
#include <eris/printk.hpp>
#include <eris/string.hpp>

namespace eris {
namespace {

constexpr usize max_sections = 64;

struct Placement {
    virt_addr address;
    bool executable;
    bool writable;
};

// Everything one loaded image owns, so unloading is a matter of walking this
// back rather than remembering what was done.
struct LoadedImage {
    virt_addr base;
    usize pages;
    Placement placement[max_sections];
    usize section_count;
};

usize round_up_pages(usize bytes)
{
    return (bytes + page_size - 1) / page_size;
}

const char* string_at(const u8* image, const elf::SectionHeader& strings, u32 offset)
{
    return reinterpret_cast<const char*>(image + strings.offset + offset);
}

// The relocations a compiler emits for kernel code are 32 bit: PC32 reaches
// two gigabytes and 32S needs the address itself to fit in a signed 32 bit
// word. An image above this line cannot satisfy either. It's refused here
// rather than mapped, relocated and then rejected one entry at a time.
constexpr phys_addr image_limit = 2ULL << 30;

bool allocate_image(LoadedImage& loaded, usize bytes)
{
    const usize pages = round_up_pages(bytes);
    const phys_addr frames = mm::alloc_pages_below(pages, image_limit);
    if (frames == 0)
        return false;

    const virt_addr window = mm::phys_to_virt(frames);

    // Writable while the image is being built, the real permissions land once
    // the relocations are applied. This also splits the huge pages the direct
    // map is made of, which is what lets one section differ from the next.
    if (!mm::AddressSpace::kernel().protect(window, pages * page_size,
                                            mm::PageFlags::Write | mm::PageFlags::NoExecute)) {
        mm::free_pages(frames, pages);
        return false;
    }

    loaded.base = window;
    loaded.pages = pages;
    return true;
}

void release_image(LoadedImage& loaded)
{
    if (loaded.base == 0)
        return;

    // Back to what the direct map promises everywhere else: writable data that
    // cannot be executed.
    mm::AddressSpace::kernel().protect(loaded.base, loaded.pages * page_size,
                                       mm::PageFlags::Write | mm::PageFlags::NoExecute);

    mm::free_pages(mm::virt_to_phys(loaded.base), loaded.pages);

    loaded.base = 0;
    loaded.pages = 0;
}

u64 symbol_address(const u8* image,
                   const elf::SectionHeader* sections,
                   const elf::SectionHeader& symbols,
                   const elf::SectionHeader& strings,
                   const LoadedImage& loaded,
                   u32 index,
                   bool& resolved)
{
    resolved = false;

    const auto* table = reinterpret_cast<const elf::Symbol*>(image + symbols.offset);
    const usize count = symbols.size / sizeof(elf::Symbol);
    if (index >= count)
        return 0;

    const elf::Symbol& symbol = table[index];

    if (symbol.section == elf::section_undefined) {
        const char* name = string_at(image, strings, symbol.name);
        void* address = symbol_lookup(name);
        if (address == nullptr) {
            pr_err("module loader: nothing exports %s\n", name);
            return 0;
        }

        resolved = true;
        return reinterpret_cast<u64>(address);
    }

    if (symbol.section >= max_sections)
        return 0;

    resolved = true;
    return loaded.placement[symbol.section].address + symbol.value;
}

bool apply_relocations(const u8* image,
                       const elf::Header& header,
                       const elf::SectionHeader* sections,
                       LoadedImage& loaded)
{
    for (u16 i = 0; i < header.section_header_count; ++i) {
        const elf::SectionHeader& section = sections[i];
        if (section.type != elf::section_rela)
            continue;

        const elf::SectionHeader& target = sections[section.info];
        if ((target.flags & elf::section_flag_alloc) == 0)
            continue;

        const elf::SectionHeader& symbols = sections[section.link];
        const elf::SectionHeader& strings = sections[symbols.link];

        const auto* entries = reinterpret_cast<const elf::Rela*>(image + section.offset);
        const usize count = section.size / sizeof(elf::Rela);

        for (usize entry = 0; entry < count; ++entry) {
            const elf::Rela& rela = entries[entry];

            bool resolved = false;
            const u64 symbol = symbol_address(image, sections, symbols, strings, loaded,
                                              elf::rela_symbol(rela.info), resolved);
            if (!resolved)
                return false;

            const virt_addr where = loaded.placement[section.info].address + rela.offset;
            const i64 addend = rela.addend;

            switch (elf::rela_type(rela.info)) {
            case elf::r_x86_64_64:
                *reinterpret_cast<u64*>(where) = symbol + addend;
                break;
            case elf::r_x86_64_pc32:
            case elf::r_x86_64_plt32: {
                const i64 value = static_cast<i64>(symbol) + addend - static_cast<i64>(where);
                if (value < -0x80000000LL || value > 0x7FFFFFFFLL) {
                    pr_err("module loader: a 32 bit relative relocation does not reach\n");
                    return false;
                }
                *reinterpret_cast<i32*>(where) = static_cast<i32>(value);
                break;
            }
            case elf::r_x86_64_32:
            case elf::r_x86_64_32s: {
                const i64 value = static_cast<i64>(symbol) + addend;
                if (value < -0x80000000LL || value > 0x7FFFFFFFLL) {
                    pr_err("module loader: a 32 bit absolute relocation does not fit\n");
                    return false;
                }
                *reinterpret_cast<i32*>(where) = static_cast<i32>(value);
                break;
            }
            case elf::r_x86_64_pc64:
                *reinterpret_cast<u64*>(where) = symbol + addend - where;
                break;
            default:
                pr_err("module loader: relocation type %u is not handled\n",
                       elf::rela_type(rela.info));
                return false;
            }
        }
    }

    return true;
}

void apply_protections(const LoadedImage& loaded,
                       const elf::Header& header,
                       const elf::SectionHeader* sections)
{
    for (u16 i = 0; i < header.section_header_count && i < max_sections; ++i) {
        const elf::SectionHeader& section = sections[i];
        if ((section.flags & elf::section_flag_alloc) == 0 || section.size == 0)
            continue;

        const Placement& placement = loaded.placement[i];
        const usize pages = round_up_pages(section.size);

        mm::PageFlags flags = mm::PageFlags::None;
        if (placement.writable)
            flags = flags | mm::PageFlags::Write;
        if (!placement.executable)
            flags = flags | mm::PageFlags::NoExecute;

        mm::AddressSpace::kernel().protect(placement.address & ~(virt_addr{page_size} - 1),
                                           pages * page_size, flags);
    }
}

void run_constructors(const u8* image,
                      const elf::Header& header,
                      const elf::SectionHeader* sections,
                      const LoadedImage& loaded)
{
    const elf::SectionHeader& names = sections[header.section_name_index];

    for (u16 i = 0; i < header.section_header_count; ++i) {
        const elf::SectionHeader& section = sections[i];
        const char* name = string_at(image, names, section.name);

        if (strcmp(name, ".init_array") != 0 && strcmp(name, ".ctors") != 0)
            continue;

        using Constructor = void (*)();
        const auto* entries = reinterpret_cast<Constructor*>(loaded.placement[i].address);

        for (usize entry = 0; entry < section.size / sizeof(Constructor); ++entry) {
            if (entries[entry] != nullptr)
                entries[entry]();
        }
    }
}

void register_exports(const u8* image,
                      const elf::Header& header,
                      const elf::SectionHeader* sections,
                      const LoadedImage& loaded)
{
    const elf::SectionHeader& names = sections[header.section_name_index];

    for (u16 i = 0; i < header.section_header_count; ++i) {
        const elf::SectionHeader& section = sections[i];
        if (strcmp(string_at(image, names, section.name), ".eris_symtab") != 0)
            continue;

        const auto* table = reinterpret_cast<const ExportedSymbol*>(loaded.placement[i].address);
        const usize count = section.size / sizeof(ExportedSymbol);

        if (!symbol_register_table(table, count, loaded.base))
            pr_warn("module loader: no room for the exports of this image\n");
        else
            pr_info("module loader: %lu symbol%s joined the export table\n",
                    static_cast<u64>(count),
                    count == 1 ? "" : "s");
    }
}

} // namespace

// Loads a relocatable image, resolves it against the exported symbols and
// hands the descriptors it carries to the module framework.
int module_load_image(const void* data, usize length, const char* origin)
{
    const auto* image = static_cast<const u8*>(data);

    if (length < sizeof(elf::Header)) {
        pr_err("module loader: %s is too small to be an object\n", origin);
        return -1;
    }

    const auto& header = *reinterpret_cast<const elf::Header*>(image);
    if (!elf::header_looks_sane(header, length)) {
        pr_err("module loader: %s is not a relocatable x86_64 object\n", origin);
        return -1;
    }

    if (header.section_header_count > max_sections) {
        pr_err("module loader: %s has %u sections, more than the loader keeps room for\n",
               origin, header.section_header_count);
        return -1;
    }

    const auto* sections = reinterpret_cast<const elf::SectionHeader*>(
        image + header.section_header_offset);

    LoadedImage loaded{};
    loaded.section_count = header.section_header_count;

    // Lay the allocated sections out one after another, each on its own page so
    // the permissions can differ.
    usize total = 0;
    for (u16 i = 0; i < header.section_header_count; ++i) {
        const elf::SectionHeader& section = sections[i];
        if ((section.flags & elf::section_flag_alloc) == 0 || section.size == 0)
            continue;
        total += round_up_pages(section.size) * page_size;
    }

    if (total == 0) {
        pr_err("module loader: %s has nothing to load\n", origin);
        return -1;
    }

    if (!allocate_image(loaded, total)) {
        pr_err("module loader: no memory below %lu MiB for %s\n",
               static_cast<u64>(image_limit / (1024 * 1024)), origin);
        return -1;
    }

    usize cursor = 0;
    for (u16 i = 0; i < header.section_header_count; ++i) {
        const elf::SectionHeader& section = sections[i];
        if ((section.flags & elf::section_flag_alloc) == 0 || section.size == 0)
            continue;

        const virt_addr where = loaded.base + cursor;
        loaded.placement[i] = Placement{
            where,
            (section.flags & elf::section_flag_exec) != 0,
            (section.flags & elf::section_flag_write) != 0,
        };

        if (section.type == elf::section_nobits)
            memset(reinterpret_cast<void*>(where), 0, section.size);
        else
            memcpy(reinterpret_cast<void*>(where), image + section.offset, section.size);

        cursor += round_up_pages(section.size) * page_size;
    }

    if (!apply_relocations(image, header, sections, loaded)) {
        release_image(loaded);
        return -1;
    }

    // Constructors first: an export entry is filled in by one of them, so the
    // table is empty until they have run.
    run_constructors(image, header, sections, loaded);
    register_exports(image, header, sections, loaded);

    apply_protections(loaded, header, sections);

    // Find the descriptors the image carries and hand them over.
    const elf::SectionHeader& names = sections[header.section_name_index];
    int registered = 0;

    for (u16 i = 0; i < header.section_header_count; ++i) {
        const elf::SectionHeader& section = sections[i];
        if (strcmp(string_at(image, names, section.name), ".eris_modules") != 0)
            continue;

        const usize count = section.size / sizeof(ModuleInfo);
        const auto* descriptors = reinterpret_cast<const ModuleInfo*>(loaded.placement[i].address);

        for (usize entry = 0; entry < count; ++entry) {
            if (module_register_loaded(&descriptors[entry], loaded.base, loaded.pages) == 0)
                ++registered;
        }
    }

    if (registered == 0) {
        pr_err("module loader: %s carries no module descriptor\n", origin);
        symbol_unregister_owner(loaded.base);
        release_image(loaded);
        return -1;
    }

    pr_info("module loader: %s mapped at %lx, %lu KiB, %d descriptor%s\n",
            origin,
            loaded.base,
            static_cast<u64>(loaded.pages * page_size / 1024),
            registered,
            registered == 1 ? "" : "s");

    return 0;
}

void module_release_image(virt_addr base, usize pages)
{
    LoadedImage loaded{};
    loaded.base = base;
    loaded.pages = pages;
    release_image(loaded);
}

} // namespace eris

namespace eris::elf {

bool header_looks_sane(const Header& header, usize length)
{
    if (header.ident[0] != 0x7F || header.ident[1] != 'E' || header.ident[2] != 'L'
        || header.ident[3] != 'F')
        return false;

    if (header.ident[4] != 2 || header.ident[5] != 1)
        return false;

    if (header.type != type_relocatable || header.machine != machine_x86_64)
        return false;

    if (header.section_header_size != sizeof(SectionHeader))
        return false;

    const u64 table_end = header.section_header_offset
        + static_cast<u64>(header.section_header_count) * header.section_header_size;

    return table_end <= length;
}

} // namespace eris::elf
