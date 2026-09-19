// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

#include "app.hpp"

namespace eris::modules {

class Canvas;

// What the shell and the painter share. Apps see none of this, they only ever
// get the painter.
Canvas& shell_canvas();
u32 shell_colour(u8 role);
const DesktopPainter* desktop_painter();

} // namespace eris::modules
