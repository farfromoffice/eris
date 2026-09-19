// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

#include "font.hpp"

namespace eris::modules {

// Straight 32 bit colour, alpha in the top byte, the way the framebuffer wants
// it. Blending is done here rather than by the hardware, because there is no
// hardware to ask.
struct Colour {
    u8 blue;
    u8 green;
    u8 red;
    u8 alpha;

    constexpr u32 packed() const
    {
        return (static_cast<u32>(alpha) << 24) | (static_cast<u32>(red) << 16)
            | (static_cast<u32>(green) << 8) | blue;
    }
};

constexpr Colour rgb(u32 value)
{
    return Colour{
        static_cast<u8>(value & 0xFF),
        static_cast<u8>((value >> 8) & 0xFF),
        static_cast<u8>((value >> 16) & 0xFF),
        0xFF,
    };
}

constexpr Colour rgba(u32 value, u8 alpha)
{
    Colour colour = rgb(value);
    colour.alpha = alpha;
    return colour;
}

struct Rect {
    i32 x;
    i32 y;
    i32 width;
    i32 height;

    constexpr i32 right() const { return x + width; }
    constexpr i32 bottom() const { return y + height; }
    constexpr bool empty() const { return width <= 0 || height <= 0; }
};

// Somewhere to draw. The desktop paints into one of these in ordinary memory
// and hands the changed rectangles to the framebuffer, which is the only way
// to get a picture that does not tear.
class Canvas {
public:
    void attach(u32* pixels, i32 width, i32 height, u32 pitch);

    i32 width() const { return width_; }
    i32 height() const { return height_; }
    u32 pitch() const { return pitch_; }
    const u32* pixels() const { return pixels_; }

    void clip(const Rect& area);
    void clip_reset();

    void fill(const Rect& area, Colour colour);
    void blend(const Rect& area, Colour colour);
    void rounded(const Rect& area, i32 radius, Colour colour);
    void rounded_border(const Rect& area, i32 radius, Colour colour, i32 thickness);
    void shadow(const Rect& area, i32 radius, i32 spread, u8 strength);
    void vertical_gradient(const Rect& area, Colour top, Colour bottom);
    void disc(i32 centre_x, i32 centre_y, i32 radius, Colour colour);
    void ring(i32 centre_x, i32 centre_y, i32 radius, i32 thickness, Colour colour);
    void line(i32 x0, i32 y0, i32 x1, i32 y1, Colour colour);
    void pixel(i32 x, i32 y, Colour colour);

    i32 text(const Font& font, i32 x, i32 y, const char* utf8, Colour colour);
    i32 text_width(const Font& font, const char* utf8) const;

private:
    void blend_pixel(i32 x, i32 y, Colour colour, u8 coverage);

    u32* pixels_ = nullptr;
    i32 width_ = 0;
    i32 height_ = 0;
    u32 pitch_ = 0;
    Rect clip_{0, 0, 0, 0};
};

} // namespace eris::modules
