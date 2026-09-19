// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

// What an application is, as far as the desktop is concerned. An app is its own
// module: it brings an icon, a name and a way to draw itself, and the desktop
// gives it a rectangle and the tools to fill it. Nothing about the window, the
// taskbar or the pointer belongs to the app.

extern "C" {

enum ErisFace : eris::u8 {
    eris_face_regular = 0,
    eris_face_bold = 1,
    eris_face_small = 2,
    eris_face_mono = 3,
};

// Drawing is handed over as plain function pointers, so an app never links
// against the desktop's internals and the two can be built apart.
struct DesktopPainter {
    void (*fill)(eris::i32 x, eris::i32 y, eris::i32 width, eris::i32 height, eris::u32 argb);
    void (*rounded)(eris::i32 x, eris::i32 y, eris::i32 width, eris::i32 height,
                    eris::i32 radius, eris::u32 argb);
    void (*outline)(eris::i32 x, eris::i32 y, eris::i32 width, eris::i32 height,
                    eris::i32 radius, eris::u32 argb);
    void (*disc)(eris::i32 x, eris::i32 y, eris::i32 radius, eris::u32 argb);
    void (*ring)(eris::i32 x, eris::i32 y, eris::i32 radius, eris::i32 thickness,
                 eris::u32 argb);
    void (*line)(eris::i32 x0, eris::i32 y0, eris::i32 x1, eris::i32 y1, eris::u32 argb);
    eris::i32 (*text)(eris::u8 face, eris::i32 x, eris::i32 y, const char* text, eris::u32 argb);
    eris::i32 (*text_width)(eris::u8 face, const char* text);
    eris::i32 (*line_height)(eris::u8 face);
    void (*clip)(eris::i32 x, eris::i32 y, eris::i32 width, eris::i32 height);
    void (*clip_reset)();

    // The palette the shell uses, so an app looks like it belongs without
    // hard coding the same numbers in four places.
    eris::u32 (*colour)(eris::u8 role);
};

enum ErisColour : eris::u8 {
    eris_colour_text = 0,
    eris_colour_text_dim = 1,
    eris_colour_text_bright = 2,
    eris_colour_accent = 3,
    eris_colour_accent_dim = 4,
    eris_colour_surface = 5,
    eris_colour_sunken = 6,
    eris_colour_hairline = 7,
    eris_colour_good = 8,
    eris_colour_danger = 9,
    eris_colour_idle = 10,
};

struct DesktopApp {
    const char* name;
    const char* subtitle;

    // Straight RGBA, rasterised from the icon.svg next to the app at build
    // time, so the desktop needs no vector rasteriser of its own.
    const eris::u8* icon;
    eris::u16 icon_width;
    eris::u16 icon_height;

    eris::i32 preferred_width;
    eris::i32 preferred_height;

    void (*draw)(const DesktopPainter* painter, eris::i32 x, eris::i32 y,
                 eris::i32 width, eris::i32 height, bool focused);
    void (*key)(char c);
    void (*opened)();
    void (*closed)();
};

bool desktop_register_app(const DesktopApp* app);
void desktop_unregister_app(const DesktopApp* app);
bool desktop_available();
bool desktop_app_registered(const char* name);

}
