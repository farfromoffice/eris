// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/module.hpp>
#include <eris/module_api.hpp>

#include <desktop/app.hpp>

namespace eris::modules {

extern const u8 console_icon_pixels[];
extern const u16 console_icon_width;
extern const u16 console_icon_height;

namespace {

constexpr usize log_lines = 24;
constexpr usize log_columns = 112;
constexpr usize input_capacity = 72;

constinit char lines[log_lines][log_columns]{};
constinit usize head = 0;
constinit usize count = 0;

constinit char input[input_capacity + 1]{};
constinit usize input_length = 0;

void push(const char* text)
{
    usize i = 0;
    while (i + 1 < log_columns && text[i] != '\0') {
        lines[head][i] = text[i];
        ++i;
    }

    lines[head][i] = '\0';
    head = (head + 1) % log_lines;

    if (count < log_lines)
        ++count;
}

// Everything the kernel prints arrives one character at a time, and a finished
// line joins the ring.
void sink(char c)
{
    static char pending[log_columns];
    static usize length = 0;

    if (c == '\r')
        return;

    if (c == '\n' || length + 1 >= log_columns) {
        pending[length] = '\0';
        if (length > 0)
            push(pending);
        length = 0;
        return;
    }

    pending[length++] = c;
}

const char* line_at(usize index)
{
    if (index >= count)
        return nullptr;

    return lines[(head + log_lines - count + index) % log_lines];
}

void draw(const DesktopPainter* painter, i32 x, i32 y, i32 width, i32 height, bool focused)
{
    const i32 field_height = 30;
    const i32 log_height = height - field_height - 18;

    painter->rounded(x + 12, y + 8, width - 24, log_height, 8,
                     painter->colour(eris_colour_sunken));

    painter->clip(x + 12, y + 8, width - 24, log_height);

    const i32 step = painter->line_height(eris_face_mono);
    i32 cursor_y = y + 14;

    for (usize i = 0; i < count && cursor_y < y + 8 + log_height - step; ++i) {
        const char* line = line_at(i);
        if (line == nullptr)
            break;

        // A kernel line starts with its level in brackets. The tag takes the
        // colour and the message takes the contrast, rather than both fighting.
        const bool tagged = line[0] == '[' && line[4] == ']';
        u32 tag_tint = painter->colour(eris_colour_text_dim);
        u32 body_tint = painter->colour(eris_colour_text);

        if (tagged && line[1] == 'e') {
            tag_tint = painter->colour(eris_colour_danger);
            body_tint = painter->colour(eris_colour_text_bright);
        } else if (tagged && line[1] == 'w') {
            tag_tint = painter->colour(eris_colour_accent);
        }

        if (tagged) {
            const char tag[4] = {line[1], line[2], line[3], '\0'};
            painter->text(eris_face_mono, x + 24, cursor_y, tag, tag_tint);

            const char* message = line + 5;
            while (*message == ' ')
                ++message;

            painter->text(eris_face_mono, x + 64, cursor_y, message, body_tint);
        } else {
            painter->text(eris_face_mono, x + 64, cursor_y, line,
                          painter->colour(eris_colour_text_bright));
        }

        cursor_y += step;
    }

    painter->clip_reset();

    const i32 field_y = y + height - field_height - 8;
    painter->rounded(x + 12, field_y, width - 24, field_height, 8, 0xFF121A27);
    painter->outline(x + 12, field_y, width - 24, field_height, 8,
                     painter->colour(focused ? eris_colour_accent_dim : eris_colour_hairline));

    painter->text(eris_face_mono, x + 24, field_y + 7, ">",
                  painter->colour(eris_colour_accent));

    const i32 pen = painter->text(eris_face_mono, x + 38, field_y + 7, input,
                                  painter->colour(eris_colour_text_bright));

    if (focused)
        painter->fill(pen + 2, field_y + 8, 7, 14, painter->colour(eris_colour_accent_dim));
}

void key(char c)
{
    switch (c) {
    case '\n':
        if (input_length > 0) {
            char echo[log_columns];
            echo[0] = '>';
            echo[1] = ' ';

            usize i = 0;
            while (i < input_length && i + 3 < log_columns) {
                echo[i + 2] = input[i];
                ++i;
            }

            echo[i + 2] = '\0';
            push(echo);

            input_length = 0;
            input[0] = '\0';
        }
        break;
    case '\b':
        if (input_length > 0)
            input[--input_length] = '\0';
        break;
    default:
        if (c >= ' ' && input_length < input_capacity) {
            input[input_length++] = c;
            input[input_length] = '\0';
        }
        break;
    }
}

constinit DesktopApp app = {
    "console",
    "the kernel log, live",
    console_icon_pixels,
    0,
    0,
    560,
    440,
    draw,
    key,
    nullptr,
    nullptr,
};

int console_app_init()
{
    if (!desktop_available()) {
        pr_module_info("console: no desktop to live on\n");
        return 0;
    }

    app.icon_width = console_icon_width;
    app.icon_height = console_icon_height;

    if (!desktop_register_app(&app))
        return -1;

    eris_console_subscribe(sink);
    push("console: listening to the kernel");
    return 0;
}

void console_app_exit()
{
    eris_console_unsubscribe();
    desktop_unregister_app(&app);
}

} // namespace
} // namespace eris::modules

ERIS_MODULE("console", "0.1", "farfromoffice", "GPL-2.0-only",
            eris::modules::console_app_init, eris::modules::console_app_exit, "desktop");
