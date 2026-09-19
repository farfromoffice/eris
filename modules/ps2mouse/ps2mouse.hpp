// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

extern "C" {

// Relative motion and the button state, which is all a PS/2 mouse reports. The
// desktop turns it into a position, because only it knows how big the screen
// is.
using MouseHandler = void (*)(eris::i32 dx, eris::i32 dy, eris::u8 buttons);

bool mouse_subscribe(MouseHandler handler);
void mouse_unsubscribe(MouseHandler handler);
bool mouse_present();

}
