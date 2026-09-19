// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/export.hpp>

#include "app.hpp"
#include "internal.hpp"
#include "paint.hpp"

namespace eris::modules {
namespace {

Colour unpack(u32 argb)
{
    return Colour{
        static_cast<u8>(argb & 0xFF),
        static_cast<u8>((argb >> 8) & 0xFF),
        static_cast<u8>((argb >> 16) & 0xFF),
        static_cast<u8>((argb >> 24) & 0xFF),
    };
}

const Font& face_for(u8 face)
{
    switch (face) {
    case eris_face_bold:
        return ui_bold;
    case eris_face_small:
        return ui_small;
    case eris_face_mono:
        return mono;
    default:
        return ui_regular;
    }
}

void painter_fill(i32 x, i32 y, i32 width, i32 height, u32 argb)
{
    shell_canvas().blend(Rect{x, y, width, height}, unpack(argb));
}

void painter_rounded(i32 x, i32 y, i32 width, i32 height, i32 radius, u32 argb)
{
    shell_canvas().rounded(Rect{x, y, width, height}, radius, unpack(argb));
}

void painter_outline(i32 x, i32 y, i32 width, i32 height, i32 radius, u32 argb)
{
    shell_canvas().rounded_border(Rect{x, y, width, height}, radius, unpack(argb), 1);
}

void painter_disc(i32 x, i32 y, i32 radius, u32 argb)
{
    shell_canvas().disc(x, y, radius, unpack(argb));
}

void painter_ring(i32 x, i32 y, i32 radius, i32 thickness, u32 argb)
{
    shell_canvas().ring(x, y, radius, thickness, unpack(argb));
}

void painter_line(i32 x0, i32 y0, i32 x1, i32 y1, u32 argb)
{
    shell_canvas().line(x0, y0, x1, y1, unpack(argb));
}

i32 painter_text(u8 face, i32 x, i32 y, const char* text, u32 argb)
{
    return shell_canvas().text(face_for(face), x, y, text, unpack(argb));
}

i32 painter_text_width(u8 face, const char* text)
{
    return shell_canvas().text_width(face_for(face), text);
}

i32 painter_line_height(u8 face)
{
    return face_for(face).line_height;
}

void painter_clip(i32 x, i32 y, i32 width, i32 height)
{
    shell_canvas().clip(Rect{x, y, width, height});
}

void painter_clip_reset()
{
    shell_canvas().clip_reset();
}

u32 painter_colour(u8 role)
{
    return shell_colour(role);
}

constinit DesktopPainter painter = {
    painter_fill,
    painter_rounded,
    painter_outline,
    painter_disc,
    painter_ring,
    painter_line,
    painter_text,
    painter_text_width,
    painter_line_height,
    painter_clip,
    painter_clip_reset,
    painter_colour,
};

} // namespace

const DesktopPainter* desktop_painter()
{
    return &painter;
}

} // namespace eris::modules

ERIS_EXPORT_SYMBOL(desktop_register_app);
ERIS_EXPORT_SYMBOL(desktop_unregister_app);
ERIS_EXPORT_SYMBOL(desktop_available);
ERIS_EXPORT_SYMBOL(desktop_app_registered);
