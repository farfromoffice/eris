// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

namespace eris::modules {

// One glyph as a coverage mask. The desktop blends it, so text comes out
// antialiased instead of a grid of squares.
struct Glyph {
    u32 offset;
    u16 width;
    u16 height;
    i16 bearing_x;
    i16 bearing_y;
    u16 advance;
};

struct Font {
    const Glyph* glyphs;
    const u8* pixels;
    u32 first;
    u32 last;
    u16 ascent;
    u16 line_height;
};

// Filled in at start up from the faces in /fonts, so the shapes on screen come
// from a real TrueType file rather than from a table somebody committed.
extern Font ui_regular;
extern Font ui_bold;
extern Font ui_small;
extern Font mono;

bool font_load(Font& font, const char* path, i32 pixel_size);
bool fonts_ready();

} // namespace eris::modules
