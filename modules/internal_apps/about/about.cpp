// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/module.hpp>
#include <eris/module_api.hpp>

#include <desktop/app.hpp>

namespace eris::modules {

extern const u8 about_icon_pixels[];
extern const u16 about_icon_width;
extern const u16 about_icon_height;

namespace {

void draw(const DesktopPainter* painter, i32 x, i32 y, i32 width, i32, bool)
{
    const char* version = nullptr;
    const char* name = nullptr;
    eris_version(&version, &name);

    painter->ring(x + 46, y + 44, 22, 3, painter->colour(eris_colour_accent));
    painter->disc(x + 62, y + 26, 5, painter->colour(eris_colour_text_bright));

    painter->text(eris_face_bold, x + 88, y + 26, "eris",
                  painter->colour(eris_colour_text_bright));

    char release[48];
    usize length = 0;

    for (const char* p = version; p != nullptr && *p != '\0' && length + 1 < sizeof(release); ++p)
        release[length++] = *p;

    if (length + 1 < sizeof(release))
        release[length++] = ' ';

    for (const char* p = name; p != nullptr && *p != '\0' && length + 1 < sizeof(release); ++p)
        release[length++] = *p;

    release[length] = '\0';

    painter->text(eris_face_small, x + 88, y + 50, release,
                  painter->colour(eris_colour_accent));

    const char* lines[] = {
        "a barebone kernel that ended up with a desktop",
        "the shell is a module, the apps are modules of their own",
        "text is Adwaita, parsed from a TrueType file at start up",
        "icons are svg, rasterised when the tree is built",
        "no network, by design",
    };

    i32 cursor_y = y + 86;

    for (const char* line : lines) {
        painter->text(eris_face_small, x + 24, cursor_y, line,
                      painter->colour(eris_colour_text_dim));
        cursor_y += 22;
    }

    painter->fill(x + 24, cursor_y + 4, width - 48, 1, painter->colour(eris_colour_hairline));
}

constinit DesktopApp app = {
    "about",
    "where this came from",
    about_icon_pixels,
    0,
    0,
    460,
    260,
    draw,
    nullptr,
    nullptr,
    nullptr,
};

int about_app_init()
{
    if (!desktop_available()) {
        pr_module_info("about: no desktop to live on\n");
        return 0;
    }

    app.icon_width = about_icon_width;
    app.icon_height = about_icon_height;

    return desktop_register_app(&app) ? 0 : -1;
}

void about_app_exit()
{
    desktop_unregister_app(&app);
}

} // namespace
} // namespace eris::modules

ERIS_MODULE("about", "0.1", "farfromoffice", "GPL-2.0-only",
            eris::modules::about_app_init, eris::modules::about_app_exit, "desktop");
