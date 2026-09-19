// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/export.hpp>
#include <eris/module.hpp>
#include <eris/module_api.hpp>

#include <fbdev/fbdev.hpp>
#include <keyboard/keyboard.hpp>
#include <ps2mouse/ps2mouse.hpp>
#include <rtc/rtc.hpp>

#include "app.hpp"
#include "internal.hpp"
#include "paint.hpp"

namespace eris::modules {
namespace {

// The whole look in one place. Deep space behind, one warm accent in front,
// and nothing else competing for attention.
constexpr Colour space_top = rgb(0x05070C);
constexpr Colour space_bottom = rgb(0x0C1220);
constexpr Colour glow = rgb(0x241A0E);
constexpr Colour limb = rgb(0x1B2438);

constexpr Colour surface = rgba(0x0F1520, 0xEE);
constexpr Colour surface_raised = rgba(0x141C2A, 0xF6);
constexpr Colour surface_sunken = rgba(0x070B12, 0xFF);
constexpr Colour hairline = rgba(0x2A3549, 0xC0);
constexpr Colour accent = rgb(0xE9A23B);
constexpr Colour accent_dim = rgba(0xE9A23B, 0x66);
constexpr Colour danger = rgb(0xE86A5C);
constexpr Colour good = rgb(0x5FD08A);
constexpr Colour idle = rgb(0x6E7A8F);
constexpr Colour text_bright = rgb(0xE8EDF5);
constexpr Colour text_normal = rgb(0xA9B4C6);
constexpr Colour text_dim = rgb(0x6C7688);

constexpr i32 bar_height = 34;
constexpr i32 taskbar_height = 48;
constexpr i32 header_height = 34;
constexpr i32 gap = 16;
constexpr i32 corner = 10;
constexpr i32 button_size = 12;
constexpr i32 button_gap = 12;
constexpr i32 icon_column = 116;

constexpr usize max_apps = 8;
constexpr u64 double_click_ns = 450000000;

enum class WindowState : u8 {
    Closed,
    Normal,
    Minimised,
    Maximised,
};

// One registered app and the window the shell keeps for it.
struct Slot {
    const DesktopApp* app;
    Rect frame;
    Rect restore;
    WindowState state;
};

enum class Grab : u8 {
    None,
    Move,
};

constinit Canvas canvas{};
constinit bool fonts_loaded = false;
constinit u32* back_buffer = nullptr;
constinit u64 back_buffer_frames = 0;
constinit usize back_buffer_pages = 0;

// What the screen already holds. Writes to the aperture are slow enough to be
// visible as a flicker, so a frame only sends the pixels that changed.
constinit u32* mirror = nullptr;
constinit u64 mirror_frames = 0;
constinit usize mirror_pages = 0;

// The wallpaper costs a million blends to draw and never changes, so it is
// drawn once and copied in front of every frame.
constinit u32* wallpaper = nullptr;
constinit u64 wallpaper_frames = 0;
constinit usize wallpaper_pages = 0;

constinit volatile bool paint_queued = false;

constinit i32 screen_width = 0;
constinit i32 screen_height = 0;

constinit Slot slots[max_apps]{};
constinit usize slot_count = 0;
constinit u8 order[max_apps]{};
constinit i8 focus = -1;

constinit i32 pointer_x = 0;
constinit i32 pointer_y = 0;
constinit bool pointer_visible = false;
constinit u8 buttons_now = 0;
constinit u8 buttons_before = 0;

constinit Grab grab = Grab::None;
constinit u8 grabbed = 0;
constinit i32 grab_offset_x = 0;
constinit i32 grab_offset_y = 0;

constinit u64 last_click_at = 0;
constinit i32 last_click_target = -1;

constinit bool running = false;
constinit u64 frames_drawn = 0;
constinit u64 started_at = 0;

char* append_number(char* out, char* end, u64 value)
{
    char digits[24];
    usize index = sizeof(digits);

    digits[--index] = '\0';

    if (value == 0)
        digits[--index] = '0';

    while (value > 0 && index > 0) {
        digits[--index] = static_cast<char>('0' + value % 10);
        value /= 10;
    }

    for (const char* p = &digits[index]; *p != '\0' && out + 1 < end; ++p)
        *out++ = *p;

    *out = '\0';
    return out;
}

char* append_text(char* out, char* end, const char* text)
{
    while (text != nullptr && *text != '\0' && out + 1 < end)
        *out++ = *text++;

    *out = '\0';
    return out;
}

char* append_padded(char* out, char* end, u64 value, usize width)
{
    u64 scale = 1;
    for (usize i = 1; i < width; ++i)
        scale *= 10;

    while (scale > 1 && value < scale) {
        out = append_text(out, end, "0");
        scale /= 10;
    }

    return append_number(out, end, value);
}

Rect work_area()
{
    return Rect{0, bar_height, screen_width, screen_height - bar_height - taskbar_height};
}

Rect default_frame(const DesktopApp& app, usize index)
{
    const Rect work = work_area();

    const i32 width = app.preferred_width > 0 ? app.preferred_width : 420;
    const i32 height = app.preferred_height > 0 ? app.preferred_height : 320;

    // Cascade, so two windows opened in a row do not land on top of each other.
    const i32 x = work.x + icon_column + gap + static_cast<i32>(index) * 34;
    const i32 y = work.y + gap + static_cast<i32>(index) * 30;

    return Rect{
        x + width > work.right() - gap ? work.right() - gap - width : x,
        y + height > work.bottom() - gap ? work.bottom() - gap - height : y,
        width,
        height,
    };
}

bool inside(const Rect& area, i32 x, i32 y)
{
    return x >= area.x && y >= area.y && x < area.right() && y < area.bottom();
}

bool visible(const Slot& slot)
{
    return slot.state == WindowState::Normal || slot.state == WindowState::Maximised;
}

Rect button_rect(const Slot& slot, usize index)
{
    const i32 right = slot.frame.right() - 14;
    const i32 x = right - static_cast<i32>(3 - index) * (button_size + button_gap) - button_size;

    return Rect{x, slot.frame.y + (header_height - button_size) / 2, button_size, button_size};
}

Rect icon_rect(usize index)
{
    return Rect{gap + 6, work_area().y + gap + static_cast<i32>(index) * 96, icon_column - 12, 88};
}

Rect task_rect(usize index)
{
    const i32 width = 150;
    return Rect{
        gap + static_cast<i32>(index) * (width + 8),
        screen_height - taskbar_height + 7,
        width,
        taskbar_height - 14,
    };
}

void raise(u8 app)
{
    usize at = 0;
    for (usize i = 0; i < slot_count; ++i) {
        if (order[i] == app)
            at = i;
    }

    for (usize i = at; i + 1 < slot_count; ++i)
        order[i] = order[i + 1];

    if (slot_count > 0)
        order[slot_count - 1] = app;

    focus = static_cast<i8>(app);
}

void open_window(u8 index)
{
    Slot& slot = slots[index];

    if (slot.state == WindowState::Closed) {
        slot.frame = default_frame(*slot.app, index);
        slot.restore = slot.frame;

        if (slot.app->opened != nullptr)
            slot.app->opened();
    }

    if (slot.state != WindowState::Maximised)
        slot.state = WindowState::Normal;

    raise(index);
}

void close_window(u8 index)
{
    Slot& slot = slots[index];

    slot.state = WindowState::Closed;
    focus = -1;

    if (slot.app->closed != nullptr)
        slot.app->closed();
}

void toggle_maximise(Slot& slot)
{
    if (slot.state == WindowState::Maximised) {
        slot.frame = slot.restore;
        slot.state = WindowState::Normal;
        return;
    }

    slot.restore = slot.frame;

    const Rect work = work_area();
    slot.frame = Rect{work.x + icon_column + gap, work.y + gap,
                      work.width - icon_column - gap * 2, work.height - gap * 2};
    slot.state = WindowState::Maximised;
}

// The wallpaper is a scene rather than a colour: the night side of a body that
// is mostly off screen, the sun catching its edge, and the field behind it.
void paint_background()
{
    canvas.vertical_gradient(Rect{0, 0, screen_width, screen_height}, space_top, space_bottom);

    u32 seed = 0x9E3779B9;
    for (usize i = 0; i < 420; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const i32 x = static_cast<i32>((seed >> 8) % static_cast<u32>(screen_width));
        seed = seed * 1664525u + 1013904223u;
        const i32 y = static_cast<i32>((seed >> 8) % static_cast<u32>(screen_height));
        seed = seed * 1664525u + 1013904223u;

        const u32 roll = (seed >> 24) % 100;
        const u8 brightness = static_cast<u8>(roll < 80 ? 60 + roll : 150 + (roll - 80) * 5);

        canvas.pixel(x, y, Colour{brightness, brightness, brightness, brightness});

        // A handful get a faint cross, which reads as a bright star rather
        // than a stuck pixel.
        if (roll > 96) {
            const Colour halo{brightness, brightness, brightness, static_cast<u8>(brightness / 3)};
            canvas.pixel(x - 1, y, halo);
            canvas.pixel(x + 1, y, halo);
            canvas.pixel(x, y - 1, halo);
            canvas.pixel(x, y + 1, halo);
        }
    }

    // The body. Its centre is below the screen, so only the upper limb shows.
    const i32 centre_x = screen_width * 72 / 100;
    const i32 centre_y = screen_height + screen_height * 52 / 100;
    const i32 radius = screen_height * 92 / 100;

    const i32 outer = radius * radius;
    const i32 rim = (radius - 3) * (radius - 3);

    for (i32 y = centre_y - radius; y < screen_height; ++y) {
        if (y < 0)
            continue;

        for (i32 x = 0; x < screen_width; ++x) {
            const i32 dx = x - centre_x;
            const i32 dy = y - centre_y;
            const i32 distance = dx * dx + dy * dy;

            if (distance > outer)
                continue;

            if (distance > rim) {
                const i32 lit = dx < 0 ? 150 : 40;
                canvas.pixel(x, y,
                             Colour{accent.blue, accent.green, accent.red, static_cast<u8>(lit)});
                continue;
            }

            // How far inside the edge this pixel is, which is what gives the
            // body its shape rather than a flat disc.
            const i32 depth = static_cast<i32>((static_cast<i64>(outer - distance) * 255) / outer);
            const i32 lit = dx < 0 ? 26 : 10;
            const u8 alpha = static_cast<u8>(lit + depth / 4);

            canvas.pixel(x, y, Colour{limb.blue, limb.green, limb.red, alpha});
        }
    }

    for (i32 y = 0; y < screen_height * 3 / 5; ++y) {
        for (i32 x = 0; x < screen_width * 3 / 5; ++x) {
            const i32 dx = x - screen_width / 10;
            const i32 dy = y + 40;
            const i32 distance = dx * dx / 30 + dy * dy / 12;

            if (distance > 9000)
                continue;

            const u8 alpha = static_cast<u8>((9000 - distance) * 22 / 9000);
            canvas.pixel(x, y, Colour{glow.blue, glow.green, glow.red, alpha});
        }
    }
}

// Straight RGBA out of the app, scaled by picking the nearest source pixel.
// Icons are drawn at a handful of sizes and never rotated, so nothing more
// clever earns its keep.
void paint_icon(const DesktopApp& app, i32 x, i32 y, i32 size)
{
    if (app.icon == nullptr || app.icon_width == 0 || app.icon_height == 0)
        return;

    for (i32 row = 0; row < size; ++row) {
        const i32 source_row = row * app.icon_height / size;

        for (i32 column = 0; column < size; ++column) {
            const i32 source_column = column * app.icon_width / size;
            const u8* pixel = app.icon
                + (static_cast<usize>(source_row) * app.icon_width + source_column) * 4;

            if (pixel[3] == 0)
                continue;

            canvas.pixel(x + column, y + row, Colour{pixel[2], pixel[1], pixel[0], pixel[3]});
        }
    }
}

void paint_icons()
{
    for (usize i = 0; i < slot_count; ++i) {
        const Rect tile = icon_rect(i);
        const bool hovered = inside(tile, pointer_x, pointer_y);

        if (hovered)
            canvas.rounded(tile, 10, rgba(0x9CB4D8, 0x1A));

        paint_icon(*slots[i].app, tile.x + (tile.width - 48) / 2, tile.y + 6, 48);

        const char* label = slots[i].app->name;
        const i32 width = canvas.text_width(ui_small, label);

        canvas.text(ui_small, tile.x + (tile.width - width) / 2, tile.y + 58, label,
                    visible(slots[i]) ? text_bright : text_normal);

        if (visible(slots[i]))
            canvas.disc(tile.x + tile.width / 2, tile.y + 80, 2, accent);
    }
}

void paint_chip(i32 x, i32 y, const char* text, Colour tint)
{
    const i32 width = canvas.text_width(ui_small, text) + 16;
    canvas.rounded(Rect{x, y, width, 20}, 9, rgba(0x1B2433, 0xD0));
    canvas.text(ui_small, x + 8, y + 3, text, tint);
}

void paint_top_bar()
{
    canvas.blend(Rect{0, 0, screen_width, bar_height}, surface);
    canvas.fill(Rect{0, bar_height - 1, screen_width, 1}, hairline);

    // The mark: a ring with a bright point on it, the body and its moon, and
    // the only drawn logo the system has.
    canvas.ring(gap + 8, bar_height / 2, 8, 2, accent);
    canvas.disc(gap + 14, bar_height / 2 - 6, 2, text_bright);

    canvas.text(ui_bold, gap + 24, 7, "eris", text_bright);

    const char* version = nullptr;
    const char* name = nullptr;
    eris_version(&version, &name);

    char label[48];
    char* end = label + sizeof(label);
    char* out = append_text(label, end, version);
    out = append_text(out, end, " ");
    append_text(out, end, name);

    paint_chip(gap + 24 + canvas.text_width(ui_bold, "eris") + 10, 7, label, accent);

    const u64 uptime = (eris_monotonic_ns() - started_at) / 1000000000ULL;

    RtcTime now{};
    char clock[32];
    clock[0] = '\0';

    if (rtc_read(&now)) {
        char* clock_end = clock + sizeof(clock);
        char* cursor = append_padded(clock, clock_end, now.hour, 2);
        cursor = append_text(cursor, clock_end, ":");
        cursor = append_padded(cursor, clock_end, now.minute, 2);
        cursor = append_text(cursor, clock_end, ":");
        append_padded(cursor, clock_end, now.second, 2);
    }

    char uptime_text[32];
    char* uptime_end = uptime_text + sizeof(uptime_text);
    char* cursor = append_text(uptime_text, uptime_end, "up ");
    cursor = append_number(cursor, uptime_end, uptime / 60);
    cursor = append_text(cursor, uptime_end, "m ");
    cursor = append_number(cursor, uptime_end, uptime % 60);
    append_text(cursor, uptime_end, "s");

    i32 right = screen_width - gap;

    if (clock[0] != '\0') {
        right -= canvas.text_width(ui_regular, clock);
        canvas.text(ui_regular, right, 7, clock, text_bright);
        right -= 18;
    }

    right -= canvas.text_width(ui_small, uptime_text);
    canvas.text(ui_small, right, 9, uptime_text, text_dim);
}

void paint_taskbar()
{
    const Rect bar{0, screen_height - taskbar_height, screen_width, taskbar_height};
    canvas.blend(bar, surface);
    canvas.fill(Rect{0, bar.y, screen_width, 1}, hairline);

    for (usize i = 0; i < slot_count; ++i) {
        const Slot& slot = slots[i];
        const Rect button = task_rect(i);
        const bool hovered = inside(button, pointer_x, pointer_y);
        const bool active = focus == static_cast<i8>(i) && visible(slot);

        Colour background = rgba(0x141C2A, 0xA0);
        if (active)
            background = rgba(0x1E2838, 0xFF);
        else if (hovered)
            background = rgba(0x18202E, 0xE0);

        canvas.rounded(button, 8, background);

        if (active)
            canvas.fill(Rect{button.x + 10, button.bottom() - 3, button.width - 20, 2}, accent);

        paint_icon(*slot.app, button.x + 8, button.y + (button.height - 22) / 2, 22);

        canvas.text(ui_small, button.x + 38, button.y + button.height / 2 - 8, slot.app->name,
                    visible(slot) ? text_bright : text_normal);

        if (slot.state == WindowState::Minimised)
            canvas.disc(button.right() - 14, button.y + button.height / 2, 3, accent_dim);
    }

    char frames[40];
    char* end = frames + sizeof(frames);
    char* cursor = append_number(frames, end, frames_drawn);
    append_text(cursor, end, " frames");

    const i32 text_y = screen_height - taskbar_height / 2 - 8;
    const i32 frames_left = screen_width - gap - canvas.text_width(ui_small, frames);

    canvas.text(ui_small, frames_left, text_y, frames, text_dim);

    // The hint only appears when the tasks have left room for it, rather than
    // being written over them.
    const char* hint = mouse_present()
        ? "double click an icon to open, click a task to switch"
        : "tab switches windows, no pointer on this machine";

    const i32 tasks_right = slot_count > 0 ? task_rect(slot_count - 1).right() : gap;
    const i32 hint_width = canvas.text_width(ui_small, hint);
    const i32 available = frames_left - tasks_right - gap * 2;

    if (available > hint_width)
        canvas.text(ui_small, tasks_right + (available - hint_width) / 2 + gap, text_y, hint,
                    text_dim);
}

void paint_window_buttons(const Slot& slot, bool focused)
{
    const Colour tint = focused ? text_normal : text_dim;

    // Minimise, maximise, close, in that order, the way every desktop has put
    // them for thirty years.
    const Rect minimise = button_rect(slot, 0);
    const bool over_minimise = inside(minimise, pointer_x, pointer_y);
    if (over_minimise)
        canvas.rounded(Rect{minimise.x - 4, minimise.y - 4, minimise.width + 8,
                            minimise.height + 8},
                       6, rgba(0x9CB4D8, 0x1E));
    canvas.fill(Rect{minimise.x + 1, minimise.y + minimise.height / 2, minimise.width - 2, 2},
                over_minimise ? text_bright : tint);

    const Rect maximise = button_rect(slot, 1);
    const bool over_maximise = inside(maximise, pointer_x, pointer_y);
    if (over_maximise)
        canvas.rounded(Rect{maximise.x - 4, maximise.y - 4, maximise.width + 8,
                            maximise.height + 8},
                       6, rgba(0x9CB4D8, 0x1E));
    canvas.rounded_border(maximise, 3, over_maximise ? text_bright : tint, 1);

    const Rect close = button_rect(slot, 2);
    const bool over_close = inside(close, pointer_x, pointer_y);
    if (over_close)
        canvas.rounded(Rect{close.x - 4, close.y - 4, close.width + 8, close.height + 8}, 6,
                       rgba(0xE86A5C, 0x40));

    for (i32 i = 0; i < close.width; ++i) {
        canvas.pixel(close.x + i, close.y + i, over_close ? danger : tint);
        canvas.pixel(close.x + i, close.bottom() - 1 - i, over_close ? danger : tint);
    }
}

void paint_window(const Slot& slot, bool focused)
{
    canvas.shadow(slot.frame, corner, 10, 150);
    canvas.rounded(slot.frame, corner, focused ? surface_raised : surface);

    // A single lighter line under the top edge is what gives a flat panel the
    // impression of being lit from above.
    canvas.fill(Rect{slot.frame.x + corner, slot.frame.y + 1, slot.frame.width - 2 * corner, 1},
                rgba(0xFFFFFF, focused ? 0x16 : 0x0C));

    canvas.rounded_border(slot.frame, corner, focused ? accent_dim : hairline, 1);

    paint_icon(*slot.app, slot.frame.x + 12, slot.frame.y + 9, 16);

    canvas.text(ui_bold, slot.frame.x + 36, slot.frame.y + 8, slot.app->name,
                focused ? text_bright : text_normal);

    const i32 title_width = canvas.text_width(ui_bold, slot.app->name);
    canvas.text(ui_small, slot.frame.x + 36 + title_width + 10, slot.frame.y + 11,
                slot.app->subtitle, text_dim);

    canvas.fill(Rect{slot.frame.x + 16, slot.frame.y + header_height, slot.frame.width - 32, 1},
                hairline);

    if (focused)
        canvas.fill(Rect{slot.frame.x + 16, slot.frame.y + header_height, 42, 1}, accent);

    paint_window_buttons(slot, focused);
}

void paint_pointer()
{
    if (!pointer_visible)
        return;

    // A plain arrow, drawn with its own outline so it reads against both the
    // dark wallpaper and the lighter panels.
    for (i32 row = 0; row < 18; ++row) {
        const i32 span = row < 12 ? row : (row < 14 ? 11 : 17 - row + 4);

        for (i32 column = 0; column <= span; ++column) {
            const bool edge = column == 0 || column == span || row == 0;
            canvas.pixel(pointer_x + column, pointer_y + row,
                         edge ? Colour{20, 20, 20, 0xFF} : Colour{255, 255, 255, 0xFF});
        }
    }
}

// Sends the rows that differ from what is already on screen, one span each.
// Comparing in memory costs nothing next to writing the whole aperture, and a
// still picture sends nothing at all.
void present()
{
    const u32* source = canvas.pixels();

    for (i32 y = 0; y < screen_height; ++y) {
        const u32* row = source + static_cast<usize>(y) * screen_width;
        u32* seen = mirror + static_cast<usize>(y) * screen_width;

        i32 first = 0;
        while (first < screen_width && row[first] == seen[first])
            ++first;

        if (first == screen_width)
            continue;

        i32 last = screen_width - 1;
        while (last > first && row[last] == seen[last])
            --last;

        for (i32 x = first; x <= last; ++x)
            seen[x] = row[x];

        fb_blit(static_cast<u32>(first), static_cast<u32>(y),
                static_cast<u32>(last - first + 1), 1, row + first, canvas.pitch());
    }
}

void paint_frame()
{
    const usize pixels = static_cast<usize>(screen_width) * screen_height;

    for (usize i = 0; i < pixels; ++i)
        back_buffer[i] = wallpaper[i];

    paint_icons();
    paint_top_bar();

    for (usize i = 0; i < slot_count; ++i) {
        const u8 index = order[i];
        const Slot& slot = slots[index];

        if (!visible(slot))
            continue;

        const bool focused = focus == static_cast<i8>(index);
        paint_window(slot, focused);

        const Rect content{
            slot.frame.x + 1,
            slot.frame.y + header_height + 1,
            slot.frame.width - 2,
            slot.frame.height - header_height - 2,
        };

        canvas.clip(content);

        if (slot.app->draw != nullptr)
            slot.app->draw(desktop_painter(), content.x, content.y, content.width,
                           content.height, focused);

        canvas.clip_reset();
    }

    paint_taskbar();
    paint_pointer();

    ++frames_drawn;

    present();

}

void on_key(char c)
{
    if (!running)
        return;

    if (c == '\t') {
        // Tab walks the windows that are actually open.
        for (usize i = 1; i <= slot_count; ++i) {
            const u8 candidate = static_cast<u8>((focus + static_cast<i8>(i))
                                                 % static_cast<i8>(slot_count));
            if (visible(slots[candidate])) {
                raise(candidate);
                return;
            }
        }

        return;
    }

    if (focus >= 0 && slots[focus].app->key != nullptr)
        slots[focus].app->key(c);
}

bool press_window(u8 index)
{
    Slot& slot = slots[index];

    if (!visible(slot) || !inside(slot.frame, pointer_x, pointer_y))
        return false;

    raise(index);

    if (inside(button_rect(slot, 0), pointer_x, pointer_y)) {
        slot.state = WindowState::Minimised;
        focus = -1;
        return true;
    }

    if (inside(button_rect(slot, 1), pointer_x, pointer_y)) {
        toggle_maximise(slot);
        return true;
    }

    if (inside(button_rect(slot, 2), pointer_x, pointer_y)) {
        close_window(index);
        return true;
    }

    const Rect header{slot.frame.x, slot.frame.y, slot.frame.width, header_height};
    if (inside(header, pointer_x, pointer_y) && slot.state == WindowState::Normal) {
        grab = Grab::Move;
        grabbed = index;
        grab_offset_x = pointer_x - slot.frame.x;
        grab_offset_y = pointer_y - slot.frame.y;
    }

    return true;
}

void handle_press()
{
    // Front to back, so a click lands on the window that is actually on top.
    for (usize i = slot_count; i > 0; --i) {
        if (press_window(order[i - 1]))
            return;
    }

    for (usize i = 0; i < slot_count; ++i) {
        if (!inside(task_rect(i), pointer_x, pointer_y))
            continue;

        // One click on a task: bring it up, or put it away if it is already
        // the one in front.
        if (visible(slots[i]) && focus == static_cast<i8>(i)) {
            slots[i].state = WindowState::Minimised;
            focus = -1;
        } else {
            open_window(static_cast<u8>(i));
        }

        return;
    }

    const u64 now = eris_monotonic_ns();

    for (usize i = 0; i < slot_count; ++i) {
        if (!inside(icon_rect(i), pointer_x, pointer_y))
            continue;

        const bool second = last_click_target == static_cast<i32>(i)
            && now - last_click_at < double_click_ns;

        if (second) {
            open_window(static_cast<u8>(i));
            last_click_target = -1;
        } else {
            last_click_target = static_cast<i32>(i);
            last_click_at = now;
        }

        return;
    }

    last_click_target = -1;
}

void on_mouse(i32 dx, i32 dy, u8 buttons)
{
    if (!running)
        return;

    pointer_visible = true;
    pointer_x += dx;
    pointer_y += dy;

    if (pointer_x < 0)
        pointer_x = 0;
    if (pointer_y < 0)
        pointer_y = 0;
    if (pointer_x > screen_width - 2)
        pointer_x = screen_width - 2;
    if (pointer_y > screen_height - 2)
        pointer_y = screen_height - 2;

    buttons_before = buttons_now;
    buttons_now = buttons;

    const bool pressed = (buttons_now & 1) != 0 && (buttons_before & 1) == 0;
    const bool released = (buttons_now & 1) == 0 && (buttons_before & 1) != 0;

    if (pressed)
        handle_press();

    if (released)
        grab = Grab::None;

    if (grab == Grab::Move) {
        Slot& slot = slots[grabbed];
        slot.frame.x = pointer_x - grab_offset_x;
        slot.frame.y = pointer_y - grab_offset_y;

        const Rect work = work_area();
        if (slot.frame.y < work.y)
            slot.frame.y = work.y;
        if (slot.frame.y > work.bottom() - header_height)
            slot.frame.y = work.bottom() - header_height;

        slot.restore = slot.frame;
    }
}

void paint_work(void*)
{
    if (running)
        paint_frame();

    // Cleared last, so a tick that arrives while this frame is still being
    // painted does not start a second one on another core.
    paint_queued = false;
}

// The timer only asks for a frame. Painting one takes milliseconds and must
// not happen with interrupts off.
void redraw(void*)
{
    if (!running || paint_queued)
        return;

    paint_queued = true;

    if (!eris_schedule_work(paint_work, nullptr))
        paint_queued = false;
}

} // namespace

Canvas& shell_canvas()
{
    return canvas;
}

u32 shell_colour(u8 role)
{
    switch (role) {
    case eris_colour_text:        return text_normal.packed();
    case eris_colour_text_dim:    return text_dim.packed();
    case eris_colour_text_bright: return text_bright.packed();
    case eris_colour_accent:      return accent.packed();
    case eris_colour_accent_dim:  return accent_dim.packed();
    case eris_colour_surface:     return surface_raised.packed();
    case eris_colour_sunken:      return surface_sunken.packed();
    case eris_colour_hairline:    return hairline.packed();
    case eris_colour_good:        return good.packed();
    case eris_colour_danger:      return danger.packed();
    case eris_colour_idle:        return idle.packed();
    default:                      return text_normal.packed();
    }
}

namespace {

int desktop_init()
{
    if (!fb_present()) {
        pr_module_info("desktop: no framebuffer, nothing to draw on\n");
        return 0;
    }

    // The faces come out of the file system, so the shell is useless without
    // them and says so rather than drawing rectangles where text should be.
    fonts_loaded = font_load(ui_regular, "/eris-sans.ttf", 15)
        && font_load(ui_bold, "/eris-sans.ttf", 16)
        && font_load(ui_small, "/eris-sans.ttf", 12)
        && font_load(mono, "/eris-mono.ttf", 13);

    if (!fonts_loaded) {
        pr_module_err("desktop: no fonts in /, nothing to draw text with\n");
        return -1;
    }

    screen_width = static_cast<i32>(fb_width());
    screen_height = static_cast<i32>(fb_height());

    const usize bytes = static_cast<usize>(screen_width) * screen_height * sizeof(u32);
    back_buffer_pages = (bytes + 4095) / 4096;
    back_buffer_frames = eris_alloc_pages(back_buffer_pages);

    if (back_buffer_frames == 0) {
        pr_module_err("desktop: no memory for a %lu KiB back buffer\n",
                      static_cast<u64>(bytes / 1024));
        return -1;
    }

    wallpaper_pages = back_buffer_pages;
    wallpaper_frames = eris_alloc_pages(wallpaper_pages);

    if (wallpaper_frames == 0) {
        eris_free_pages(back_buffer_frames, back_buffer_pages);
        back_buffer_frames = 0;
        pr_module_err("desktop: no memory for a %lu KiB wallpaper\n",
                      static_cast<u64>(bytes / 1024));
        return -1;
    }

    mirror_pages = back_buffer_pages;
    mirror_frames = eris_alloc_pages(mirror_pages);

    if (mirror_frames == 0) {
        eris_free_pages(back_buffer_frames, back_buffer_pages);
        eris_free_pages(wallpaper_frames, wallpaper_pages);
        back_buffer_frames = 0;
        wallpaper_frames = 0;
        pr_module_err("desktop: no memory for a %lu KiB mirror\n",
                      static_cast<u64>(bytes / 1024));
        return -1;
    }

    back_buffer = reinterpret_cast<u32*>(eris_phys_to_virt(back_buffer_frames));
    mirror = reinterpret_cast<u32*>(eris_phys_to_virt(mirror_frames));
    wallpaper = reinterpret_cast<u32*>(eris_phys_to_virt(wallpaper_frames));

    // The screen starts black and so does the mirror, so the first frame sends
    // everything that is not black and nothing else.
    for (usize i = 0; i < bytes / sizeof(u32); ++i)
        mirror[i] = 0;

    canvas.attach(back_buffer, screen_width, screen_height,
                  static_cast<u32>(screen_width) * sizeof(u32));

    paint_background();

    for (usize i = 0; i < bytes / sizeof(u32); ++i)
        wallpaper[i] = back_buffer[i];

    pointer_x = screen_width / 2;
    pointer_y = screen_height / 2;

    started_at = eris_monotonic_ns();
    running = true;

    keyboard_subscribe(on_key);

    if (mouse_present())
        mouse_subscribe(on_mouse);

    paint_frame();

    // Sixty times a second is enough for a clock and a log. Every frame is a
    // full repaint into memory and only the changed rows reach the screen.
    eris_timer_every(16000000, redraw, nullptr);

    pr_module_info("desktop: %d by %d, %lu KiB back buffer, pointer %s\n",
                   screen_width, screen_height, static_cast<u64>(bytes / 1024),
                   mouse_present() ? "on" : "off");
    return 0;
}

void desktop_exit()
{
    running = false;

    mouse_unsubscribe(on_mouse);
    keyboard_unsubscribe(on_key);

    if (back_buffer_frames != 0) {
        eris_free_pages(back_buffer_frames, back_buffer_pages);
        back_buffer_frames = 0;
        back_buffer = nullptr;
    }

    if (mirror_frames != 0) {
        eris_free_pages(mirror_frames, mirror_pages);
        mirror_frames = 0;
        mirror = nullptr;
    }

    if (wallpaper_frames != 0) {
        eris_free_pages(wallpaper_frames, wallpaper_pages);
        wallpaper_frames = 0;
        wallpaper = nullptr;
    }
}

} // namespace
} // namespace eris::modules

extern "C" {

// An app is a module of its own: it registers here when it loads and the shell
// gives it an icon on the desktop, a place in the taskbar and a window when
// somebody asks for one.
bool desktop_register_app(const DesktopApp* app)
{
    using namespace eris::modules;

    if (app == nullptr || app->name == nullptr || slot_count >= max_apps)
        return false;

    for (eris::usize i = 0; i < slot_count; ++i) {
        if (slots[i].app == app)
            return true;
    }

    slots[slot_count] = Slot{app, Rect{}, Rect{}, WindowState::Closed};
    order[slot_count] = static_cast<eris::u8>(slot_count);
    ++slot_count;

    pr_module_info("desktop: %s registered\n", app->name);
    return true;
}

void desktop_unregister_app(const DesktopApp* app)
{
    using namespace eris::modules;

    for (eris::usize i = 0; i < slot_count; ++i) {
        if (slots[i].app != app)
            continue;

        for (eris::usize j = i; j + 1 < slot_count; ++j)
            slots[j] = slots[j + 1];

        --slot_count;
        focus = -1;

        for (eris::usize j = 0; j < slot_count; ++j)
            order[j] = static_cast<eris::u8>(j);

        return;
    }
}

// Whether a module by this name has put an app on the desktop, which is how
// the module list tells an app apart from a driver.
bool desktop_app_registered(const char* name)
{
    using namespace eris::modules;

    if (name == nullptr)
        return false;

    for (eris::usize i = 0; i < slot_count; ++i) {
        const char* candidate = slots[i].app->name;

        eris::usize at = 0;
        while (candidate[at] != '\0' && candidate[at] == name[at])
            ++at;

        if (candidate[at] == '\0' && name[at] == '\0')
            return true;
    }

    return false;
}

bool desktop_available()
{
    return eris::modules::running;
}

}

ERIS_MODULE("desktop", "0.3", "farfromoffice", "GPL-2.0-only",
            eris::modules::desktop_init, eris::modules::desktop_exit,
            "fbdev", "keyboard", "ps2mouse", "rtc");
