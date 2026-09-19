// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include "font.hpp"

namespace eris::modules {

// The faces the shell draws with. They start empty and are filled by
// font_load, which reads the TrueType files the initrd carried in.
Font ui_regular{};
Font ui_bold{};
Font ui_small{};
Font mono{};

bool fonts_ready()
{
    return ui_regular.glyphs != nullptr && mono.glyphs != nullptr;
}

} // namespace eris::modules
