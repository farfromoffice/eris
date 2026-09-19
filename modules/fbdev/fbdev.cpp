// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/export.hpp>
#include <eris/module.hpp>
#include <eris/module_api.hpp>

#include "fbdev.hpp"

namespace eris::modules {
namespace {

// The display adapter QEMU and Bochs both offer. Mode setting goes through two
// ports and the framebuffer itself is the first memory window.
constexpr u16 vendor_bochs = 0x1234;
constexpr u16 device_bochs = 0x1111;
constexpr u16 vendor_qemu = 0x1AF4;
constexpr u16 device_qemu_vga = 0x1050;

constexpr u16 port_index = 0x01CE;
constexpr u16 port_data = 0x01CF;

constexpr u16 index_id = 0;
constexpr u16 index_xres = 1;
constexpr u16 index_yres = 2;
constexpr u16 index_bpp = 3;
constexpr u16 index_enable = 4;
constexpr u16 index_virt_width = 6;
constexpr u16 index_virt_height = 7;
constexpr u16 index_x_offset = 8;
constexpr u16 index_y_offset = 9;

constexpr u16 enable_disabled = 0x00;
constexpr u16 enable_enabled = 0x01;
constexpr u16 enable_linear = 0x40;

constexpr u16 wanted_width = 1024;
constexpr u16 wanted_height = 768;
constexpr u16 wanted_bpp = 32;

constinit u8* framebuffer = nullptr;
constinit u32 width = 0;
constinit u32 height = 0;
constinit u32 pitch = 0;
constinit usize mapped_bytes = 0;

void write_register(u16 index, u16 value)
{
    eris_outw(port_index, index);
    eris_outw(port_data, value);
}

u16 read_register(u16 index)
{
    eris_outw(port_index, index);
    return eris_inw(port_data);
}

bool find_adapter(usize& index, u64& aperture, u64& length)
{
    for (usize i = 0; i < eris_pci_device_count(); ++i) {
        u16 vendor = 0;
        u16 device = 0;

        if (!eris_pci_device_at(i, &vendor, &device, nullptr, nullptr, nullptr))
            continue;

        const bool bochs = vendor == vendor_bochs && device == device_bochs;
        const bool qemu = vendor == vendor_qemu && device == device_qemu_vga;

        if (!bochs && !qemu)
            continue;

        bool memory = false;
        u64 size = 0;
        const u64 bar = eris_pci_bar(i, 0, &size, &memory);

        if (!memory || bar == 0)
            continue;

        index = i;
        aperture = bar;
        length = size;
        return true;
    }

    return false;
}

int fbdev_init()
{
    const u16 id = read_register(index_id);
    if (id < 0xB0C0 || id > 0xB0CF) {
        pr_module_info("fbdev: no bochs display interface here\n");
        return 0;
    }

    usize index = 0;
    u64 aperture = 0;
    u64 aperture_size = 0;

    if (!find_adapter(index, aperture, aperture_size)) {
        pr_module_info("fbdev: the adapter has no memory window\n");
        return 0;
    }

    eris_pci_enable(index);

    // The mode has to be set with the display off, and the linear bit is what
    // turns the aperture into a plain array of pixels.
    write_register(index_enable, enable_disabled);
    write_register(index_xres, wanted_width);
    write_register(index_yres, wanted_height);
    write_register(index_bpp, wanted_bpp);
    write_register(index_virt_width, wanted_width);
    write_register(index_virt_height, wanted_height);
    write_register(index_x_offset, 0);
    write_register(index_y_offset, 0);
    write_register(index_enable, enable_enabled | enable_linear);

    width = read_register(index_xres);
    height = read_register(index_yres);
    pitch = width * sizeof(u32);

    mapped_bytes = static_cast<usize>(pitch) * height;
    if (mapped_bytes > aperture_size)
        mapped_bytes = static_cast<usize>(aperture_size);

    framebuffer = static_cast<u8*>(eris_map_device(aperture, mapped_bytes));
    if (framebuffer == nullptr) {
        pr_module_err("fbdev: the aperture at %lx would not map\n", aperture);
        write_register(index_enable, enable_disabled);
        return -1;
    }

    for (usize i = 0; i < mapped_bytes; ++i)
        framebuffer[i] = 0;

    pr_module_info("fbdev: %u by %u at %u bits, aperture %lx, %lu KiB mapped\n",
                   width, height, wanted_bpp, aperture,
                   static_cast<u64>(mapped_bytes / 1024));
    return 0;
}

void fbdev_exit()
{
    if (framebuffer == nullptr)
        return;

    write_register(index_enable, enable_disabled);
    eris_unmap_device(framebuffer, mapped_bytes);
    framebuffer = nullptr;
}

} // namespace
} // namespace eris::modules

extern "C" {

bool fb_present()
{
    return eris::modules::framebuffer != nullptr;
}

eris::u32 fb_width()
{
    return eris::modules::width;
}

eris::u32 fb_height()
{
    return eris::modules::height;
}

eris::u32 fb_pitch()
{
    return eris::modules::pitch;
}

void* fb_pixels()
{
    return eris::modules::framebuffer;
}

// Copies one rectangle out of a caller's buffer. Device memory is slow to read
// and fine to write, so nothing here ever reads it back.
void fb_blit(eris::u32 x, eris::u32 y, eris::u32 rect_width, eris::u32 rect_height,
             const void* source, eris::u32 source_pitch)
{
    namespace fb = eris::modules;

    if (fb::framebuffer == nullptr || source == nullptr)
        return;

    if (x >= fb::width || y >= fb::height)
        return;

    if (x + rect_width > fb::width)
        rect_width = fb::width - x;
    if (y + rect_height > fb::height)
        rect_height = fb::height - y;

    const auto* in = static_cast<const eris::u8*>(source);

    for (eris::u32 row = 0; row < rect_height; ++row) {
        auto* out = reinterpret_cast<eris::u32*>(fb::framebuffer + (y + row) * fb::pitch) + x;
        const auto* line = reinterpret_cast<const eris::u32*>(in + row * source_pitch);

        for (eris::u32 column = 0; column < rect_width; ++column)
            out[column] = line[column];
    }
}

}

ERIS_EXPORT_SYMBOL(fb_present);
ERIS_EXPORT_SYMBOL(fb_width);
ERIS_EXPORT_SYMBOL(fb_height);
ERIS_EXPORT_SYMBOL(fb_pitch);
ERIS_EXPORT_SYMBOL(fb_pixels);
ERIS_EXPORT_SYMBOL(fb_blit);

ERIS_MODULE("fbdev", "0.1", "farfromoffice", "GPL-2.0-only",
            eris::modules::fbdev_init, eris::modules::fbdev_exit);
