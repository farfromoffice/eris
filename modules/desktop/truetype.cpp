// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/module_api.hpp>

#include "font.hpp"

namespace eris::modules {
namespace {

constexpr u32 first_character = 32;
constexpr u32 last_character = 126;
constexpr usize character_count = last_character - first_character + 1;

constexpr usize max_points = 256;
constexpr usize max_contours = 32;
constexpr usize max_edges = 768;
constexpr usize curve_steps = 8;

// Outlines are carried in 26.6 fixed point: whole pixels in the high bits and
// a sixty fourth of a pixel in the low ones. Rounding to whole pixels before
// the fill is what turns a smooth curve into a staircase.
constexpr i32 unit = 64;
constexpr i32 samples = 5;

// A 16.16 fixed point scale, because a font is measured in units per em and a
// screen is measured in pixels, and neither side has a floating point unit
// available in kernel code.
struct Face {
    const u8* data;
    usize length;

    const u8* glyf;
    const u8* loca;
    const u8* hmtx;
    const u8* cmap;

    u16 units_per_em;
    u16 glyph_count;
    u16 metric_count;
    bool long_loca;

    i16 ascender;
    i16 descender;
    i16 line_gap;
};

struct Point {
    i32 x;
    i32 y;
    bool on_curve;
};

struct Edge {
    i32 x0;
    i32 y0;
    i32 x1;
    i32 y1;
};

u16 read16(const u8* data)
{
    return static_cast<u16>((data[0] << 8) | data[1]);
}

i16 read16s(const u8* data)
{
    return static_cast<i16>(read16(data));
}

u32 read32(const u8* data)
{
    return (static_cast<u32>(data[0]) << 24) | (static_cast<u32>(data[1]) << 16)
        | (static_cast<u32>(data[2]) << 8) | data[3];
}

bool same_tag(const u8* data, const char* tag)
{
    return data[0] == tag[0] && data[1] == tag[1] && data[2] == tag[2] && data[3] == tag[3];
}

const u8* find_table(const u8* data, usize length, const char* tag, u32* size)
{
    if (length < 12)
        return nullptr;

    const u16 count = read16(data + 4);

    for (u16 i = 0; i < count; ++i) {
        const u8* entry = data + 12 + i * 16;
        if (!same_tag(entry, tag))
            continue;

        const u32 offset = read32(entry + 8);
        const u32 table_length = read32(entry + 12);

        if (offset + table_length > length)
            return nullptr;

        if (size != nullptr)
            *size = table_length;

        return data + offset;
    }

    return nullptr;
}

bool open_face(Face& face, const u8* data, usize length)
{
    face.data = data;
    face.length = length;

    const u8* head = find_table(data, length, "head", nullptr);
    const u8* hhea = find_table(data, length, "hhea", nullptr);
    const u8* maxp = find_table(data, length, "maxp", nullptr);

    face.glyf = find_table(data, length, "glyf", nullptr);
    face.loca = find_table(data, length, "loca", nullptr);
    face.hmtx = find_table(data, length, "hmtx", nullptr);
    face.cmap = find_table(data, length, "cmap", nullptr);

    if (head == nullptr || hhea == nullptr || maxp == nullptr || face.glyf == nullptr
        || face.loca == nullptr || face.hmtx == nullptr || face.cmap == nullptr)
        return false;

    face.units_per_em = read16(head + 18);
    face.long_loca = read16s(head + 50) == 1;
    face.glyph_count = read16(maxp + 4);
    face.metric_count = read16(hhea + 34);

    face.ascender = read16s(hhea + 4);
    face.descender = read16s(hhea + 6);
    face.line_gap = read16s(hhea + 8);

    return face.units_per_em != 0;
}

// Format 4 is what every desktop font carries for the basic plane, and the
// subset the tree ships has nothing else.
u16 glyph_for(const Face& face, u32 character)
{
    const u16 tables = read16(face.cmap + 2);
    const u8* subtable = nullptr;

    for (u16 i = 0; i < tables; ++i) {
        const u8* record = face.cmap + 4 + i * 8;
        const u32 offset = read32(record + 4);
        const u8* candidate = face.cmap + offset;

        if (read16(candidate) == 4)
            subtable = candidate;
    }

    if (subtable == nullptr)
        return 0;

    const u16 segments = read16(subtable + 6) / 2;
    const u8* ends = subtable + 14;
    const u8* starts = ends + segments * 2 + 2;
    const u8* deltas = starts + segments * 2;
    const u8* ranges = deltas + segments * 2;

    for (u16 segment = 0; segment < segments; ++segment) {
        const u16 end = read16(ends + segment * 2);
        if (character > end)
            continue;

        const u16 start = read16(starts + segment * 2);
        if (character < start)
            return 0;

        const i16 delta = read16s(deltas + segment * 2);
        const u16 range = read16(ranges + segment * 2);

        if (range == 0)
            return static_cast<u16>(character + delta);

        const u8* position = ranges + segment * 2 + range + (character - start) * 2;
        const u16 glyph = read16(position);

        return glyph == 0 ? 0 : static_cast<u16>(glyph + delta);
    }

    return 0;
}

const u8* outline_for(const Face& face, u16 glyph, u32& length)
{
    if (glyph + 1 > face.glyph_count)
        return nullptr;

    u32 start = 0;
    u32 end = 0;

    if (face.long_loca) {
        start = read32(face.loca + glyph * 4);
        end = read32(face.loca + glyph * 4 + 4);
    } else {
        start = read16(face.loca + glyph * 2) * 2u;
        end = read16(face.loca + glyph * 2 + 2) * 2u;
    }

    if (end <= start)
        return nullptr;

    length = end - start;
    return face.glyf + start;
}

u16 advance_for(const Face& face, u16 glyph)
{
    if (face.metric_count == 0)
        return 0;

    const u16 index = glyph < face.metric_count ? glyph : static_cast<u16>(face.metric_count - 1);
    return read16(face.hmtx + index * 4);
}

// Decodes one simple glyph: the contour ends, the flags, and the coordinates,
// which are stored as deltas in a compressed form.
bool decode_glyph(const u8* outline, u32 length, Point* points, usize& point_count,
                  u16* contour_ends, usize& contour_count)
{
    if (length < 10)
        return false;

    const i16 contours = read16s(outline);
    if (contours <= 0 || static_cast<usize>(contours) > max_contours)
        return false;

    contour_count = static_cast<usize>(contours);

    const u8* cursor = outline + 10;
    for (usize i = 0; i < contour_count; ++i) {
        contour_ends[i] = read16(cursor);
        cursor += 2;
    }

    point_count = contour_ends[contour_count - 1] + 1u;
    if (point_count > max_points)
        return false;

    const u16 instructions = read16(cursor);
    cursor += 2 + instructions;

    u8 flags[max_points];
    usize index = 0;

    while (index < point_count) {
        const u8 flag = *cursor++;
        flags[index++] = flag;

        if ((flag & 0x08) != 0) {
            u8 repeat = *cursor++;
            while (repeat-- > 0 && index < point_count)
                flags[index++] = flag;
        }
    }

    i32 value = 0;
    for (usize i = 0; i < point_count; ++i) {
        const u8 flag = flags[i];

        if ((flag & 0x02) != 0) {
            const u8 delta = *cursor++;
            value += (flag & 0x10) != 0 ? delta : -delta;
        } else if ((flag & 0x10) == 0) {
            value += read16s(cursor);
            cursor += 2;
        }

        points[i].x = value;
        points[i].on_curve = (flag & 0x01) != 0;
    }

    value = 0;
    for (usize i = 0; i < point_count; ++i) {
        const u8 flag = flags[i];

        if ((flag & 0x04) != 0) {
            const u8 delta = *cursor++;
            value += (flag & 0x20) != 0 ? delta : -delta;
        } else if ((flag & 0x20) == 0) {
            value += read16s(cursor);
            cursor += 2;
        }

        points[i].y = value;
    }

    return true;
}

struct Raster {
    Edge edges[max_edges];
    usize edge_count;
};

void add_edge(Raster& raster, i32 x0, i32 y0, i32 x1, i32 y1)
{
    if (y0 == y1 || raster.edge_count >= max_edges)
        return;

    raster.edges[raster.edge_count++] = Edge{x0, y0, x1, y1};
}

// Quadratic curves are the only kind a TrueType outline carries, and a handful
// of straight steps is close enough at the sizes a desktop uses.
void add_curve(Raster& raster, i32 x0, i32 y0, i32 cx, i32 cy, i32 x1, i32 y1)
{
    i32 previous_x = x0;
    i32 previous_y = y0;

    for (usize step = 1; step <= curve_steps; ++step) {
        const i64 t = static_cast<i64>(step) * 256 / static_cast<i64>(curve_steps);
        const i64 inverse = 256 - t;

        const i32 x = static_cast<i32>((inverse * inverse * x0 + 2 * inverse * t * cx
                                        + t * t * x1) / 65536);
        const i32 y = static_cast<i32>((inverse * inverse * y0 + 2 * inverse * t * cy
                                        + t * t * y1) / 65536);

        add_edge(raster, previous_x, previous_y, x, y);
        previous_x = x;
        previous_y = y;
    }
}

} // namespace

// Loads a face from the file system and turns the characters the shell uses
// into coverage masks. Everything is done once, at start up, because a glyph
// that never changes has no business being rasterised twice.
bool font_load(Font& font, const char* path, i32 pixel_size)
{
    const u64 size = eris_file_size(path);
    if (size == 0)
        return false;

    auto* file = static_cast<u8*>(eris_kmalloc(static_cast<usize>(size)));
    if (file == nullptr)
        return false;

    if (eris_file_read(path, file, static_cast<usize>(size)) <= 0) {
        eris_kfree(file);
        return false;
    }

    Face face{};
    if (!open_face(face, file, static_cast<usize>(size))) {
        eris_kfree(file);
        return false;
    }

    // 16.16 units to pixels.
    const i32 scale = (pixel_size << 16) / face.units_per_em;

    auto* glyphs = static_cast<Glyph*>(eris_kzalloc(sizeof(Glyph) * character_count));
    if (glyphs == nullptr) {
        eris_kfree(file);
        return false;
    }

    // Two passes: measure everything, then fill one buffer, so the coverage of
    // the whole face is contiguous.
    usize total_pixels = 0;

    struct Measure {
        i32 min_x;
        i32 min_y;
        i32 max_x;
        i32 max_y;
        u16 width;
        u16 height;
        u16 advance;
        u16 glyph;
    };

    auto* measures = static_cast<Measure*>(eris_kzalloc(sizeof(Measure) * character_count));
    if (measures == nullptr) {
        eris_kfree(glyphs);
        eris_kfree(file);
        return false;
    }

    for (usize i = 0; i < character_count; ++i) {
        const u16 glyph = glyph_for(face, static_cast<u32>(first_character + i));

        u32 outline_length = 0;
        const u8* outline = outline_for(face, glyph, outline_length);

        measures[i].glyph = glyph;
        measures[i].advance = static_cast<u16>((advance_for(face, glyph) * scale) >> 16);

        if (outline == nullptr || outline_length < 10)
            continue;

        const i32 min_x = (read16s(outline + 2) * scale) >> 16;
        const i32 min_y = (read16s(outline + 4) * scale) >> 16;
        const i32 max_x = ((read16s(outline + 6) * scale) >> 16) + 1;
        const i32 max_y = ((read16s(outline + 8) * scale) >> 16) + 1;

        measures[i].min_x = min_x;
        measures[i].min_y = min_y;
        measures[i].max_x = max_x;
        measures[i].max_y = max_y;
        measures[i].width = static_cast<u16>(max_x > min_x ? max_x - min_x : 0);
        measures[i].height = static_cast<u16>(max_y > min_y ? max_y - min_y : 0);

        total_pixels += static_cast<usize>(measures[i].width) * measures[i].height;
    }

    auto* coverage = static_cast<u8*>(eris_kzalloc(total_pixels == 0 ? 1 : total_pixels));
    if (coverage == nullptr) {
        eris_kfree(measures);
        eris_kfree(glyphs);
        eris_kfree(file);
        return false;
    }

    usize written = 0;

    for (usize i = 0; i < character_count; ++i) {
        const Measure& measure = measures[i];

        glyphs[i].offset = static_cast<u32>(written);
        glyphs[i].width = measure.width;
        glyphs[i].height = measure.height;
        glyphs[i].bearing_x = static_cast<i16>(measure.min_x);
        glyphs[i].bearing_y = static_cast<i16>(-measure.max_y);
        glyphs[i].advance = measure.advance;

        if (measure.width == 0 || measure.height == 0)
            continue;

        u32 outline_length = 0;
        const u8* outline = outline_for(face, measure.glyph, outline_length);
        if (outline == nullptr)
            continue;

        Point points[max_points];
        u16 ends[max_contours];
        usize point_count = 0;
        usize contour_count = 0;

        usize sub_count = 0;
        const u8* subs[4]{};
        i32 sub_dx[4]{};
        i32 sub_dy[4]{};

        if (read16s(outline) < 0) {
            // A composite is a list of other glyphs with an offset each, which
            // is how a dotted i or an accented letter is built.
            const u8* cursor = outline + 10;

            while (sub_count < 4) {
                const u16 flags = read16(cursor);
                const u16 component = read16(cursor + 2);
                cursor += 4;

                i32 dx = 0;
                i32 dy = 0;

                if ((flags & 0x0001) != 0) {
                    dx = read16s(cursor);
                    dy = read16s(cursor + 2);
                    cursor += 4;
                } else {
                    dx = static_cast<i8>(cursor[0]);
                    dy = static_cast<i8>(cursor[1]);
                    cursor += 2;
                }

                if ((flags & 0x0008) != 0)
                    cursor += 2;
                else if ((flags & 0x0040) != 0)
                    cursor += 4;
                else if ((flags & 0x0080) != 0)
                    cursor += 8;

                u32 component_length = 0;
                const u8* component_outline = outline_for(face, component, component_length);

                if (component_outline != nullptr && read16s(component_outline) > 0) {
                    subs[sub_count] = component_outline;
                    sub_dx[sub_count] = dx;
                    sub_dy[sub_count] = dy;
                    ++sub_count;
                }

                if ((flags & 0x0020) == 0)
                    break;
            }
        }

        if (sub_count == 0
            && !decode_glyph(outline, outline_length, points, point_count, ends, contour_count)) {
            written += static_cast<usize>(measure.width) * measure.height;
            continue;
        }

        // Scale into the glyph's own pixel box, with y flipped, because a font
        // grows upwards and a framebuffer grows downwards.
        Raster raster{};

        for (usize piece = 0; piece == 0 || piece < sub_count; ++piece) {
        i32 piece_dx = 0;
        i32 piece_dy = 0;

        if (sub_count > 0) {
            piece_dx = sub_dx[piece];
            piece_dy = sub_dy[piece];

            if (!decode_glyph(subs[piece], 0x10000, points, point_count, ends, contour_count))
                continue;
        }

        usize start = 0;

        for (usize contour = 0; contour < contour_count; ++contour) {
            const usize end = ends[contour];
            const usize count = end - start + 1;

            if (count < 2) {
                start = end + 1;
                continue;
            }

            i32 previous_x = 0;
            i32 previous_y = 0;
            bool have_previous = false;
            i32 first_x = 0;
            i32 first_y = 0;

            i32 control_x = 0;
            i32 control_y = 0;
            bool have_control = false;

            for (usize step = 0; step <= count; ++step) {
                const Point& point = points[start + (step % count)];

                const i32 x = static_cast<i32>((static_cast<i64>(point.x + piece_dx) * scale) >> 10)
                    - measure.min_x * unit;
                const i32 y = measure.max_y * unit
                    - static_cast<i32>((static_cast<i64>(point.y + piece_dy) * scale) >> 10);

                if (!have_previous) {
                    previous_x = x;
                    previous_y = y;
                    first_x = x;
                    first_y = y;
                    have_previous = true;
                    continue;
                }

                if (point.on_curve) {
                    if (have_control) {
                        add_curve(raster, previous_x, previous_y, control_x, control_y, x, y);
                        have_control = false;
                    } else {
                        add_edge(raster, previous_x, previous_y, x, y);
                    }

                    previous_x = x;
                    previous_y = y;
                } else if (have_control) {
                    // Two control points in a row imply a point between them.
                    const i32 middle_x = (control_x + x) / 2;
                    const i32 middle_y = (control_y + y) / 2;

                    add_curve(raster, previous_x, previous_y, control_x, control_y,
                              middle_x, middle_y);

                    previous_x = middle_x;
                    previous_y = middle_y;
                    control_x = x;
                    control_y = y;
                } else {
                    control_x = x;
                    control_y = y;
                    have_control = true;
                }
            }

            if (have_control)
                add_curve(raster, previous_x, previous_y, control_x, control_y, first_x, first_y);
            else
                add_edge(raster, previous_x, previous_y, first_x, first_y);

            start = end + 1;
        }
        }

        // Scanline fill. Five samples a row and partial coverage across the
        // ends of every span, which is what antialiasing is: how much of the
        // pixel the shape actually covers.
        u8* target = coverage + written;
        const i32 weight = 255 / samples;

        for (u16 row = 0; row < measure.height; ++row) {
            for (i32 sample = 0; sample < samples; ++sample) {
                const i32 scan = row * unit + (sample * unit) / samples + unit / (2 * samples);

                i32 crossings[64];
                usize crossing_count = 0;

                for (usize e = 0; e < raster.edge_count && crossing_count < 64; ++e) {
                    const Edge& edge = raster.edges[e];

                    const i32 top = edge.y0 < edge.y1 ? edge.y0 : edge.y1;
                    const i32 bottom = edge.y0 < edge.y1 ? edge.y1 : edge.y0;

                    if (scan < top || scan >= bottom)
                        continue;

                    const i32 span = edge.y1 - edge.y0;
                    const i32 x = edge.x0
                        + static_cast<i32>((static_cast<i64>(edge.x1 - edge.x0)
                                            * (scan - edge.y0)) / span);

                    crossings[crossing_count++] = x;
                }

                for (usize a = 1; a < crossing_count; ++a) {
                    const i32 value = crossings[a];
                    usize b = a;
                    while (b > 0 && crossings[b - 1] > value) {
                        crossings[b] = crossings[b - 1];
                        --b;
                    }
                    crossings[b] = value;
                }

                for (usize pair = 0; pair + 1 < crossing_count; pair += 2) {
                    i32 left = crossings[pair];
                    i32 right = crossings[pair + 1];

                    if (right <= 0 || left >= measure.width * unit)
                        continue;

                    if (left < 0)
                        left = 0;
                    if (right > measure.width * unit)
                        right = measure.width * unit;

                    const i32 first_pixel = left / unit;
                    const i32 last_pixel = (right - 1) / unit;

                    for (i32 pixel = first_pixel; pixel <= last_pixel; ++pixel) {
                        const i32 pixel_left = pixel * unit;
                        const i32 pixel_right = pixel_left + unit;

                        const i32 covered_left = left > pixel_left ? left : pixel_left;
                        const i32 covered_right = right < pixel_right ? right : pixel_right;
                        const i32 covered = covered_right - covered_left;

                        if (covered <= 0)
                            continue;

                        const usize index = static_cast<usize>(row) * measure.width
                            + static_cast<usize>(pixel);

                        const u32 added = static_cast<u32>(weight * covered) / unit;
                        const u32 value = target[index] + added;

                        target[index] = static_cast<u8>(value > 255 ? 255 : value);
                    }
                }
            }
        }

        written += static_cast<usize>(measure.width) * measure.height;
    }

    font.glyphs = glyphs;
    font.pixels = coverage;
    font.first = first_character;
    font.last = last_character;
    font.ascent = static_cast<u16>((face.ascender * scale) >> 16);
    font.line_height = static_cast<u16>(
        (((face.ascender - face.descender + face.line_gap) * scale) >> 16) + 1);

    eris_kfree(measures);
    eris_kfree(file);

    return true;
}

void font_unload(Font& font)
{
    eris_kfree(font.glyphs);
    eris_kfree(font.pixels);
    font = Font{};
}

} // namespace eris::modules
