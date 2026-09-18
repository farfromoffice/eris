// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

extern "C" {

using KeyHandler = void (*)(char);

bool keyboard_subscribe(KeyHandler handler);
void keyboard_unsubscribe(KeyHandler handler);

}
