// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/console.hpp>
#include <eris/export.hpp>
#include <eris/io.hpp>
#include <eris/paging.hpp>
#include <eris/module.hpp>
#include <eris/printk.hpp>

#include "vga.hpp"

namespace eris::modules {
namespace {

constexpr usize width = 80;
constexpr usize height = 25;
constexpr u64 framebuffer_addr = 0xB8000;
constexpr usize buffer_bytes = width * height * sizeof(u16);

class VgaConsole final : public Console {
public:
    void put(char c) override;

    bool attach();
    void detach();
    void clear();
    void put_cell(usize x, usize y, char c, u8 color);
    void set_color(u8 color) { color_ = color; }
    u8 color() const { return color_; }

private:
    void scroll();

    volatile u16* buffer_ = nullptr;
    usize column_ = 0;
    usize row_ = 0;
    u8 color_ = 0x07;
};

VgaConsole instance{};
bool console_enabled = false;

u16 make_cell(char c, u8 color)
{
    return static_cast<u16>(static_cast<u8>(c)) | static_cast<u16>(color) << 8;
}

bool VgaConsole::attach()
{
    // The framebuffer is device memory, so it comes through the mapping API
    // rather than by trusting that the address happens to be mapped.
    buffer_ = static_cast<volatile u16*>(mm::map_device(framebuffer_addr, buffer_bytes));
    return buffer_ != nullptr;
}

void VgaConsole::detach()
{
    mm::unmap_device(const_cast<u16*>(buffer_), buffer_bytes);
    buffer_ = nullptr;
}

void VgaConsole::clear()
{
    for (usize i = 0; i < width * height; ++i)
        buffer_[i] = make_cell(' ', color_);
    column_ = 0;
    row_ = 0;
}

void VgaConsole::put_cell(usize x, usize y, char c, u8 color)
{
    if (x < width && y < height)
        buffer_[y * width + x] = make_cell(c, color);
}

void VgaConsole::scroll()
{
    for (usize y = 1; y < height; ++y) {
        for (usize x = 0; x < width; ++x)
            buffer_[(y - 1) * width + x] = buffer_[y * width + x];
    }

    for (usize x = 0; x < width; ++x)
        buffer_[(height - 1) * width + x] = make_cell(' ', color_);
}

void VgaConsole::put(char c)
{
    switch (c) {
    case '\n':
        column_ = 0;
        ++row_;
        break;
    case '\r':
        column_ = 0;
        break;
    case '\t':
        column_ = (column_ + 8) & ~usize{7};
        break;
    case '\b':
        if (column_ > 0)
            --column_;
        put_cell(column_, row_, ' ', color_);
        break;
    default:
        put_cell(column_, row_, c, color_);
        ++column_;
        break;
    }

    if (column_ >= width) {
        column_ = 0;
        ++row_;
    }

    if (row_ >= height) {
        scroll();
        row_ = height - 1;
    }
}

void hide_cursor()
{
    arch::outb(0x3D4, 0x0A);
    arch::outb(0x3D5, 0x20);
}

int vga_module_init()
{
    if (!instance.attach())
        return -1;

    hide_cursor();
    instance.clear();
    vga_console_enable(true);
    return 0;
}

void vga_module_exit()
{
    vga_console_enable(false);
    instance.clear();
    instance.detach();
}

} // namespace
} // namespace eris::modules

extern "C" {

void vga_clear()
{
    eris::modules::instance.clear();
}

void vga_put_cell(eris::usize x, eris::usize y, char c, eris::u8 color)
{
    eris::modules::instance.put_cell(x, y, c, color);
}

void vga_write(eris::usize x, eris::usize y, const char* text, eris::u8 color)
{
    eris::usize cursor = x;
    for (const char* p = text; *p != '\0'; ++p, ++cursor)
        eris::modules::instance.put_cell(cursor, y, *p, color);
}

void vga_fill_row(eris::usize y, char c, eris::u8 color)
{
    for (eris::usize x = 0; x < eris::modules::width; ++x)
        eris::modules::instance.put_cell(x, y, c, color);
}

void vga_set_color(eris::u8 color)
{
    eris::modules::instance.set_color(color);
}

void vga_console_enable(bool enabled)
{
    if (enabled == eris::modules::console_enabled)
        return;

    eris::modules::console_enabled = enabled;
    if (enabled)
        eris::console_register(&eris::modules::instance);
    else
        eris::console_unregister(&eris::modules::instance);
}

eris::usize vga_width()
{
    return eris::modules::width;
}

eris::usize vga_height()
{
    return eris::modules::height;
}

}

ERIS_EXPORT_SYMBOL(vga_clear);
ERIS_EXPORT_SYMBOL(vga_put_cell);
ERIS_EXPORT_SYMBOL(vga_write);
ERIS_EXPORT_SYMBOL(vga_fill_row);
ERIS_EXPORT_SYMBOL(vga_set_color);
ERIS_EXPORT_SYMBOL(vga_console_enable);

ERIS_MODULE("vga", "0.1", "eris", "GPL-2.0-only", eris::modules::vga_module_init,
            eris::modules::vga_module_exit);
