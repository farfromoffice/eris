// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

extern "C" {

struct RtcTime {
    eris::u16 year;
    eris::u8 month;
    eris::u8 day;
    eris::u8 hour;
    eris::u8 minute;
    eris::u8 second;
};

bool rtc_read(RtcTime* out);
eris::u64 rtc_unix_time();

}
