// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

extern "C" {

void vga_clear();
void vga_put_cell(eris::usize x, eris::usize y, char c, eris::u8 color);
void vga_write(eris::usize x, eris::usize y, const char* text, eris::u8 color);
void vga_fill_row(eris::usize y, char c, eris::u8 color);
void vga_set_color(eris::u8 color);
void vga_console_enable(bool enabled);
eris::usize vga_width();
eris::usize vga_height();

}
