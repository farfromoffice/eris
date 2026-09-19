// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

extern "C" {

// A linear framebuffer, one 32 bit pixel per dot, no banking and no mode
// switching after init. Everything above draws into its own buffer and hands
// the changed rectangle back.
bool fb_present();
eris::u32 fb_width();
eris::u32 fb_height();
eris::u32 fb_pitch();
void* fb_pixels();

void fb_blit(eris::u32 x, eris::u32 y, eris::u32 rect_width, eris::u32 rect_height,
             const void* source, eris::u32 source_pitch);

}
