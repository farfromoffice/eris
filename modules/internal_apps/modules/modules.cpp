// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/module.hpp>
#include <eris/module_api.hpp>

#include <desktop/app.hpp>

namespace eris::modules {

extern const u8 modules_icon_pixels[];
extern const u16 modules_icon_width;
extern const u16 modules_icon_height;

namespace {

bool starts_with(const char* text, const char* prefix)
{
    while (text != nullptr && *prefix != '\0') {
        if (*text++ != *prefix++)
            return false;
    }

    return true;
}

// Everything the module framework knows, which is also how this app finds out
// about itself.
void draw(const DesktopPainter* painter, i32 x, i32 y, i32 width, i32 height, bool)
{
    const usize count = eris_module_count();
    i32 cursor_y = y + 14;

    char summary[48];
    usize length = 0;

    const usize tens = count / 10;
    if (tens > 0)
        summary[length++] = static_cast<char>('0' + tens);
    summary[length++] = static_cast<char>('0' + count % 10);
    summary[length++] = ' ';
    summary[length] = '\0';

    painter->text(eris_face_small, x + 16, cursor_y, summary,
                  painter->colour(eris_colour_text_bright));
    painter->text(eris_face_small, x + 16 + painter->text_width(eris_face_small, summary),
                  cursor_y, "loaded", painter->colour(eris_colour_text_dim));

    cursor_y += 24;

    for (usize i = 0; i < count && cursor_y < y + height - 20; ++i) {
        const char* name = nullptr;
        const char* version = nullptr;
        const char* license = nullptr;
        const char* state = nullptr;

        if (!eris_module_at(i, &name, &version, &license, &state))
            continue;

        const bool ready = starts_with(state, "ready");
        const bool app = desktop_app_registered(name);

        painter->disc(x + 22, cursor_y + 9, 4,
                      painter->colour(ready ? eris_colour_good : eris_colour_idle));

        painter->text(eris_face_regular, x + 36, cursor_y, name,
                      painter->colour(eris_colour_text_bright));

        const i32 name_width = painter->text_width(eris_face_regular, name);
        painter->text(eris_face_small, x + 36 + name_width + 8, cursor_y + 3, version,
                      painter->colour(eris_colour_text_dim));

        const char* tail = app ? "app" : state;
        painter->text(eris_face_small,
                      x + width - 16 - painter->text_width(eris_face_small, tail),
                      cursor_y + 3, tail,
                      painter->colour(app ? eris_colour_accent : eris_colour_text));

        cursor_y += 24;
    }
}

constinit DesktopApp app = {
    "modules",
    "loaded and what they are",
    modules_icon_pixels,
    0,
    0,
    400,
    360,
    draw,
    nullptr,
    nullptr,
    nullptr,
};

int modules_app_init()
{
    if (!desktop_available()) {
        pr_module_info("modules: no desktop to live on\n");
        return 0;
    }

    app.icon_width = modules_icon_width;
    app.icon_height = modules_icon_height;

    return desktop_register_app(&app) ? 0 : -1;
}

void modules_app_exit()
{
    desktop_unregister_app(&app);
}

} // namespace
} // namespace eris::modules

ERIS_MODULE("modules", "0.1", "farfromoffice", "GPL-2.0-only",
            eris::modules::modules_app_init, eris::modules::modules_app_exit, "desktop");
