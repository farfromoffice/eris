// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/module.hpp>
#include <eris/module_api.hpp>

#include <desktop/app.hpp>

namespace eris::modules {

extern const u8 system_icon_pixels[];
extern const u16 system_icon_width;
extern const u16 system_icon_height;

namespace {

constexpr usize history_length = 72;

constinit u16 history[history_length]{};
constinit usize history_head = 0;
constinit usize history_count = 0;
constinit u64 last_sample = 0;

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

void sample()
{
    const u64 now = eris_monotonic_ns();
    if (last_sample != 0 && now - last_sample < 1000000000ULL)
        return;

    last_sample = now;

    u64 total_pages = 0;
    u64 free_pages = 0;
    eris_memory_stats(&total_pages, &free_pages, nullptr);

    history[history_head] = static_cast<u16>((total_pages - free_pages) * 4 / 1024);
    history_head = (history_head + 1) % history_length;

    if (history_count < history_length)
        ++history_count;
}

void row(const DesktopPainter* painter, i32 x, i32 y, i32 width, const char* label,
         const char* value)
{
    painter->text(eris_face_regular, x + 16, y, label, painter->colour(eris_colour_text_dim));
    painter->text(eris_face_regular,
                  x + width - 16 - painter->text_width(eris_face_regular, value), y, value,
                  painter->colour(eris_colour_text_bright));
}

// What the machine is, and where its memory has been for the last minute.
void draw(const DesktopPainter* painter, i32 x, i32 y, i32 width, i32 height, bool)
{
    sample();

    u64 total_pages = 0;
    u64 free_pages = 0;
    u64 heap_bytes = 0;
    eris_memory_stats(&total_pages, &free_pages, &heap_bytes);

    const u64 total_mib = total_pages * 4 / 1024;
    const u64 used_mib = total_mib - free_pages * 4 / 1024;

    char value[64];
    char* end = value + sizeof(value);

    const char* version = nullptr;
    const char* name = nullptr;
    eris_version(&version, &name);

    i32 cursor_y = y + 18;

    char* out = append_text(value, end, version);
    out = append_text(out, end, " ");
    append_text(out, end, name);
    row(painter, x, cursor_y, width, "release", value);
    cursor_y += 26;

    out = append_number(value, end, eris_cpu_count());
    append_text(out, end, " online");
    row(painter, x, cursor_y, width, "cores", value);
    cursor_y += 26;

    out = append_number(value, end, used_mib);
    out = append_text(out, end, " of ");
    out = append_number(out, end, total_mib);
    append_text(out, end, " MiB");
    row(painter, x, cursor_y, width, "memory", value);
    cursor_y += 26;

    out = append_number(value, end, heap_bytes / 1024);
    append_text(out, end, " KiB committed");
    row(painter, x, cursor_y, width, "heap", value);
    cursor_y += 30;

    // A bar for where memory stands, and a line for where it has been. A
    // number alone says nothing about whether it is moving.
    painter->rounded(x + 16, cursor_y, width - 32, 8, 4, 0xFF1C2534);

    if (total_mib > 0) {
        const i32 filled = static_cast<i32>((width - 32) * used_mib / total_mib);
        painter->rounded(x + 16, cursor_y, filled < 8 ? 8 : filled, 8, 4,
                         painter->colour(eris_colour_accent));
    }

    cursor_y += 20;

    const i32 graph_height = y + height - cursor_y - 16;
    if (graph_height < 30 || history_count < 2)
        return;

    painter->rounded(x + 16, cursor_y, width - 32, graph_height, 6,
                     painter->colour(eris_colour_sunken));
    painter->text(eris_face_small, x + 26, cursor_y + 6, "memory in use",
                  painter->colour(eris_colour_text_dim));

    u16 lowest = 0xFFFF;
    u16 highest = 0;

    for (usize i = 0; i < history_count; ++i) {
        const u16 point = history[(history_head + history_length - history_count + i)
                                  % history_length];
        if (point < lowest)
            lowest = point;
        if (point > highest)
            highest = point;
    }

    const i32 span = highest > lowest ? highest - lowest : 1;
    i32 previous_x = 0;
    i32 previous_y = 0;

    for (usize i = 0; i < history_count; ++i) {
        const u16 point = history[(history_head + history_length - history_count + i)
                                  % history_length];

        const i32 plot_x = x + 24
            + static_cast<i32>(i) * (width - 48) / static_cast<i32>(history_length - 1);
        const i32 plot_y = cursor_y + graph_height - 10
            - (point - lowest) * (graph_height - 34) / span;

        if (i > 0)
            painter->line(previous_x, previous_y, plot_x, plot_y,
                          painter->colour(eris_colour_accent));

        previous_x = plot_x;
        previous_y = plot_y;
    }
}

constinit DesktopApp app = {
    "system",
    "what the kernel is",
    system_icon_pixels,
    0,
    0,
    420,
    340,
    draw,
    nullptr,
    nullptr,
    nullptr,
};

int system_app_init()
{
    if (!desktop_available()) {
        pr_module_info("system: no desktop to live on\n");
        return 0;
    }

    app.icon_width = system_icon_width;
    app.icon_height = system_icon_height;

    return desktop_register_app(&app) ? 0 : -1;
}

void system_app_exit()
{
    desktop_unregister_app(&app);
}

} // namespace
} // namespace eris::modules

ERIS_MODULE("system", "0.1", "farfromoffice", "GPL-2.0-only",
            eris::modules::system_app_init, eris::modules::system_app_exit, "desktop");
