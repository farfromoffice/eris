// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/module.hpp>
#include <eris/printk.hpp>
#include <eris/string.hpp>
#include <eris/time.hpp>

#include <keyboard/keyboard.hpp>
#include <vga/vga.hpp>

namespace eris::modules {
namespace {

constexpr u8 color_bar = 0x1F;
constexpr u8 color_desktop = 0x10;
constexpr u8 color_window = 0x70;
constexpr u8 color_text = 0x7F;

constexpr usize window_x = 8;
constexpr usize window_y = 4;
constexpr usize window_w = 60;
constexpr usize window_h = 14;
constexpr usize input_capacity = window_w - 4;

constinit char input[input_capacity + 1]{};
constinit usize input_length = 0;
constinit usize line_cursor = 0;

void draw_background()
{
    for (usize y = 0; y < vga_height(); ++y)
        vga_fill_row(y, ' ', color_desktop);

    vga_fill_row(0, ' ', color_bar);
    vga_write(1, 0, "eris desktop", color_bar);
    vga_write(vga_width() - 12, 0, "module: on", color_bar);
}

void draw_window_frame()
{
    for (usize y = window_y; y < window_y + window_h; ++y) {
        for (usize x = window_x; x < window_x + window_w; ++x)
            vga_put_cell(x, y, ' ', color_window);
    }

    vga_write(window_x + 2, window_y, "[ console ]", color_window);
}

void draw_input()
{
    for (usize x = 0; x < input_capacity; ++x)
        vga_put_cell(window_x + 2 + x, window_y + window_h - 2, ' ', color_text);

    vga_write(window_x + 2, window_y + window_h - 2, input, color_text);
    vga_put_cell(window_x + 2 + input_length, window_y + window_h - 2, '_', color_text);
}

void push_line(const char* text)
{
    const usize first_line = window_y + 2;
    const usize last_line = window_y + window_h - 4;

    if (line_cursor > last_line - first_line) {
        for (usize y = first_line; y < last_line; ++y) {
            for (usize x = window_x + 2; x < window_x + window_w - 2; ++x)
                vga_put_cell(x, y, ' ', color_window);
        }
        line_cursor = 0;
    }

    for (usize x = window_x + 2; x < window_x + window_w - 2; ++x)
        vga_put_cell(x, first_line + line_cursor, ' ', color_window);

    vga_write(window_x + 2, first_line + line_cursor, text, color_window);
    ++line_cursor;
}

void submit()
{
    input[input_length] = '\0';
    if (input_length > 0)
        push_line(input);

    input_length = 0;
    input[0] = '\0';
    draw_input();
}

void on_key(char c)
{
    switch (c) {
    case '\n':
        submit();
        return;
    case '\b':
        if (input_length > 0)
            input[--input_length] = '\0';
        break;
    default:
        if (input_length < input_capacity) {
            input[input_length++] = c;
            input[input_length] = '\0';
        }
        break;
    }

    draw_input();
}

int desktop_module_init()
{
    vga_console_enable(false);
    draw_background();
    draw_window_frame();
    push_line("eris desktop module running");
    push_line("type below, enter echoes the line");
    draw_input();

    if (!keyboard_subscribe(on_key)) {
        vga_console_enable(true);
        return -1;
    }

    return 0;
}

void desktop_module_exit()
{
    keyboard_unsubscribe(on_key);
    vga_clear();
    vga_console_enable(true);
}

} // namespace
} // namespace eris::modules

ERIS_MODULE("desktop", "0.1", "eris", "GPL-2.0-only", eris::modules::desktop_module_init,
            eris::modules::desktop_module_exit, "vga", "keyboard");
