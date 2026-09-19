// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include "paint.hpp"

namespace eris::modules {
namespace {

i32 clamp(i32 value, i32 low, i32 high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

Rect intersect(const Rect& a, const Rect& b)
{
    const i32 x = a.x > b.x ? a.x : b.x;
    const i32 y = a.y > b.y ? a.y : b.y;
    const i32 right = a.right() < b.right() ? a.right() : b.right();
    const i32 bottom = a.bottom() < b.bottom() ? a.bottom() : b.bottom();

    return Rect{x, y, right - x, bottom - y};
}

} // namespace

void Canvas::attach(u32* pixels, i32 width, i32 height, u32 pitch)
{
    pixels_ = pixels;
    width_ = width;
    height_ = height;
    pitch_ = pitch;
    clip_ = Rect{0, 0, width, height};
}

void Canvas::clip(const Rect& area)
{
    clip_ = intersect(area, Rect{0, 0, width_, height_});
}

void Canvas::clip_reset()
{
    clip_ = Rect{0, 0, width_, height_};
}

void Canvas::pixel(i32 x, i32 y, Colour colour)
{
    blend_pixel(x, y, colour, colour.alpha);
}

// One source over destination step. Everything on screen goes through here, so
// it stays small and branchless enough to run per pixel.
void Canvas::blend_pixel(i32 x, i32 y, Colour colour, u8 coverage)
{
    if (pixels_ == nullptr || coverage == 0)
        return;

    if (x < clip_.x || y < clip_.y || x >= clip_.right() || y >= clip_.bottom())
        return;

    u32* target = pixels_ + static_cast<usize>(y) * (pitch_ / sizeof(u32)) + x;

    if (coverage == 0xFF) {
        *target = colour.packed();
        return;
    }

    const u32 destination = *target;
    const u32 inverse = 255u - coverage;

    const u32 red = ((colour.red * coverage) + (((destination >> 16) & 0xFF) * inverse)) / 255;
    const u32 green = ((colour.green * coverage) + (((destination >> 8) & 0xFF) * inverse)) / 255;
    const u32 blue = ((colour.blue * coverage) + ((destination & 0xFF) * inverse)) / 255;

    *target = 0xFF000000u | (red << 16) | (green << 8) | blue;
}

void Canvas::fill(const Rect& area, Colour colour)
{
    const Rect region = intersect(area, clip_);
    if (region.empty())
        return;

    const u32 value = colour.packed();

    for (i32 y = region.y; y < region.bottom(); ++y) {
        u32* row = pixels_ + static_cast<usize>(y) * (pitch_ / sizeof(u32));
        for (i32 x = region.x; x < region.right(); ++x)
            row[x] = value;
    }
}

void Canvas::blend(const Rect& area, Colour colour)
{
    const Rect region = intersect(area, clip_);
    if (region.empty())
        return;

    for (i32 y = region.y; y < region.bottom(); ++y) {
        for (i32 x = region.x; x < region.right(); ++x)
            blend_pixel(x, y, colour, colour.alpha);
    }
}

// Rounded corners with a soft edge. Each corner is a quarter of a circle, and
// drawing the whole circle is what leaves rings hanging off the sides.
void Canvas::rounded(const Rect& area, i32 radius, Colour colour)
{
    if (radius <= 0) {
        blend(area, colour);
        return;
    }

    const i32 limit = (area.width < area.height ? area.width : area.height) / 2;
    radius = clamp(radius, 0, limit);

    blend(Rect{area.x + radius, area.y, area.width - 2 * radius, area.height}, colour);
    blend(Rect{area.x, area.y + radius, radius, area.height - 2 * radius}, colour);
    blend(Rect{area.right() - radius, area.y + radius, radius, area.height - 2 * radius}, colour);

    const i32 inner = (radius - 1) * (radius - 1);
    const i32 outer = radius * radius;

    for (i32 corner = 0; corner < 4; ++corner) {
        const bool right_side = (corner & 1) != 0;
        const bool bottom_side = (corner & 2) != 0;

        const i32 centre_x = right_side ? area.right() - radius : area.x + radius - 1;
        const i32 centre_y = bottom_side ? area.bottom() - radius : area.y + radius - 1;

        for (i32 dy = 0; dy < radius; ++dy) {
            for (i32 dx = 0; dx < radius; ++dx) {
                const i32 distance = dx * dx + dy * dy;
                if (distance > outer)
                    continue;

                u32 coverage = colour.alpha;
                if (distance > inner) {
                    const i32 span = outer - inner;
                    coverage = static_cast<u32>(colour.alpha)
                        * static_cast<u32>(outer - distance) / static_cast<u32>(span == 0 ? 1 : span);
                }

                blend_pixel(centre_x + (right_side ? dx : -dx),
                            centre_y + (bottom_side ? dy : -dy),
                            colour,
                            static_cast<u8>(coverage));
            }
        }
    }
}

// The outline of the same shape: straight runs along the sides and a quarter
// arc in each corner, one pixel wide per pass.
void Canvas::rounded_border(const Rect& area, i32 radius, Colour colour, i32 thickness)
{
    for (i32 pass = 0; pass < thickness; ++pass) {
        const Rect ring{
            area.x + pass,
            area.y + pass,
            area.width - 2 * pass,
            area.height - 2 * pass,
        };

        if (ring.empty())
            break;

        const i32 limit = (ring.width < ring.height ? ring.width : ring.height) / 2;
        const i32 r = clamp(radius - pass, 0, limit);

        for (i32 x = ring.x + r; x < ring.right() - r; ++x) {
            blend_pixel(x, ring.y, colour, colour.alpha);
            blend_pixel(x, ring.bottom() - 1, colour, colour.alpha);
        }

        for (i32 y = ring.y + r; y < ring.bottom() - r; ++y) {
            blend_pixel(ring.x, y, colour, colour.alpha);
            blend_pixel(ring.right() - 1, y, colour, colour.alpha);
        }

        const i32 outer = r * r;
        const i32 inner = (r - 1) * (r - 1);

        for (i32 corner = 0; corner < 4; ++corner) {
            const bool right_side = (corner & 1) != 0;
            const bool bottom_side = (corner & 2) != 0;

            const i32 centre_x = right_side ? ring.right() - r : ring.x + r - 1;
            const i32 centre_y = bottom_side ? ring.bottom() - r : ring.y + r - 1;

            for (i32 dy = 0; dy < r; ++dy) {
                for (i32 dx = 0; dx < r; ++dx) {
                    const i32 distance = dx * dx + dy * dy;
                    if (distance > outer || distance < inner - r)
                        continue;

                    const i32 span = outer - (inner - r);
                    const i32 fade = span == 0 ? 255 : (outer - distance) * 255 / span;
                    const i32 coverage = colour.alpha * (fade > 255 ? 255 : fade) / 255;

                    blend_pixel(centre_x + (right_side ? dx : -dx),
                                centre_y + (bottom_side ? dy : -dy),
                                colour,
                                static_cast<u8>(coverage));
                }
            }
        }
    }
}

// A cheap approximation of a soft shadow: a few rounded rectangles, each a
// little larger and a little fainter than the last.
void Canvas::shadow(const Rect& area, i32 radius, i32 spread, u8 strength)
{
    for (i32 i = spread; i > 0; --i) {
        const Rect layer{area.x - i, area.y - i + 2, area.width + 2 * i, area.height + 2 * i};
        const u8 alpha = static_cast<u8>(strength / (i + 1));
        rounded(layer, radius + i, Colour{0, 0, 0, alpha});
    }
}

void Canvas::vertical_gradient(const Rect& area, Colour top, Colour bottom)
{
    const Rect region = intersect(area, clip_);
    if (region.empty())
        return;

    for (i32 y = region.y; y < region.bottom(); ++y) {
        const i32 travelled = y - area.y;
        const i32 span = area.height > 1 ? area.height - 1 : 1;

        const u8 red = static_cast<u8>(top.red + (bottom.red - top.red) * travelled / span);
        const u8 green = static_cast<u8>(top.green + (bottom.green - top.green) * travelled / span);
        const u8 blue = static_cast<u8>(top.blue + (bottom.blue - top.blue) * travelled / span);

        const u32 value = (0xFFu << 24) | (static_cast<u32>(red) << 16)
            | (static_cast<u32>(green) << 8) | blue;

        u32* row = pixels_ + static_cast<usize>(y) * (pitch_ / sizeof(u32));
        for (i32 x = region.x; x < region.right(); ++x)
            row[x] = value;
    }
}

// A filled circle with a soft rim, used for the body in the wallpaper and for
// the small marks in the panels.
void Canvas::disc(i32 centre_x, i32 centre_y, i32 radius, Colour colour)
{
    const i32 outer = radius * radius;
    const i32 inner = (radius - 1) * (radius - 1);

    for (i32 dy = -radius; dy <= radius; ++dy) {
        for (i32 dx = -radius; dx <= radius; ++dx) {
            const i32 distance = dx * dx + dy * dy;
            if (distance > outer)
                continue;

            u32 coverage = colour.alpha;
            if (distance > inner) {
                const i32 span = outer - inner;
                coverage = static_cast<u32>(colour.alpha) * static_cast<u32>(outer - distance)
                    / static_cast<u32>(span == 0 ? 1 : span);
            }

            blend_pixel(centre_x + dx, centre_y + dy, colour, static_cast<u8>(coverage));
        }
    }
}

void Canvas::ring(i32 centre_x, i32 centre_y, i32 radius, i32 thickness, Colour colour)
{
    const i32 outer = radius * radius;
    const i32 inner = (radius - thickness) * (radius - thickness);

    for (i32 dy = -radius; dy <= radius; ++dy) {
        for (i32 dx = -radius; dx <= radius; ++dx) {
            const i32 distance = dx * dx + dy * dy;
            if (distance > outer || distance < inner)
                continue;

            blend_pixel(centre_x + dx, centre_y + dy, colour, colour.alpha);
        }
    }
}

// Bresenham, with the ends left sharp. The graph is the only thing that draws
// lines and it never needs them antialiased.
void Canvas::line(i32 x0, i32 y0, i32 x1, i32 y1, Colour colour)
{
    const i32 dx = x1 > x0 ? x1 - x0 : x0 - x1;
    const i32 dy = y1 > y0 ? y1 - y0 : y0 - y1;
    const i32 step_x = x0 < x1 ? 1 : -1;
    const i32 step_y = y0 < y1 ? 1 : -1;

    i32 error = (dx > dy ? dx : -dy) / 2;

    for (;;) {
        blend_pixel(x0, y0, colour, colour.alpha);

        if (x0 == x1 && y0 == y1)
            break;

        const i32 current = error;
        if (current > -dx) {
            error -= dy;
            x0 += step_x;
        }
        if (current < dy) {
            error += dx;
            y0 += step_y;
        }
    }
}

i32 Canvas::text(const Font& font, i32 x, i32 y, const char* utf8, Colour colour)
{
    if (utf8 == nullptr)
        return x;

    i32 pen = x;

    for (const char* p = utf8; *p != '\0'; ++p) {
        const auto code = static_cast<u32>(static_cast<u8>(*p));
        if (code < font.first || code > font.last) {
            pen += font.glyphs[0].advance;
            continue;
        }

        const Glyph& glyph = font.glyphs[code - font.first];
        const u8* coverage = font.pixels + glyph.offset;

        for (u16 row = 0; row < glyph.height; ++row) {
            for (u16 column = 0; column < glyph.width; ++column) {
                const u8 alpha = coverage[row * glyph.width + column];
                if (alpha == 0)
                    continue;

                const u32 mixed = static_cast<u32>(alpha) * colour.alpha / 255;
                blend_pixel(pen + glyph.bearing_x + column,
                            y + font.ascent + glyph.bearing_y + row,
                            colour,
                            static_cast<u8>(mixed));
            }
        }

        pen += glyph.advance;
    }

    return pen;
}

i32 Canvas::text_width(const Font& font, const char* utf8) const
{
    if (utf8 == nullptr)
        return 0;

    i32 width = 0;

    for (const char* p = utf8; *p != '\0'; ++p) {
        const auto code = static_cast<u32>(static_cast<u8>(*p));
        width += code >= font.first && code <= font.last ? font.glyphs[code - font.first].advance
                                                         : font.glyphs[0].advance;
    }

    return width;
}

} // namespace eris::modules
