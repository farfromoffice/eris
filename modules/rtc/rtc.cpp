// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/export.hpp>
#include <eris/module.hpp>
#include <eris/module_api.hpp>

#include "rtc.hpp"

namespace eris::modules {
namespace {

constexpr u16 port_index = 0x70;
constexpr u16 port_data = 0x71;

constexpr u8 register_seconds = 0x00;
constexpr u8 register_minutes = 0x02;
constexpr u8 register_hours = 0x04;
constexpr u8 register_day = 0x07;
constexpr u8 register_month = 0x08;
constexpr u8 register_year = 0x09;
constexpr u8 register_status_a = 0x0A;
constexpr u8 register_status_b = 0x0B;

constexpr u8 status_a_update_in_progress = 0x80;
constexpr u8 status_b_binary = 0x04;
constexpr u8 status_b_24_hour = 0x02;

constinit bool attached = false;

u8 read_register(u8 index)
{
    // The high bit of the index port masks the NMI, and leaving it set is a
    // good way to lose an interrupt nobody can explain later.
    eris_outb(port_index, index & 0x7F);
    return eris_inb(port_data);
}

bool update_in_progress()
{
    return (read_register(register_status_a) & status_a_update_in_progress) != 0;
}

u8 decode(u8 value, bool binary)
{
    return binary ? value : static_cast<u8>((value & 0x0F) + (value >> 4) * 10);
}

bool read_once(RtcTime& out, u8 status_b)
{
    const bool binary = (status_b & status_b_binary) != 0;
    const bool twenty_four_hour = (status_b & status_b_24_hour) != 0;

    const u8 raw_hour = read_register(register_hours);

    out.second = decode(read_register(register_seconds), binary);
    out.minute = decode(read_register(register_minutes), binary);
    out.hour = decode(static_cast<u8>(raw_hour & 0x7F), binary);
    out.day = decode(read_register(register_day), binary);
    out.month = decode(read_register(register_month), binary);
    out.year = static_cast<u16>(2000 + decode(read_register(register_year), binary));

    if (!twenty_four_hour && (raw_hour & 0x80) != 0)
        out.hour = static_cast<u8>((out.hour % 12) + 12);

    return true;
}

bool same(const RtcTime& a, const RtcTime& b)
{
    return a.second == b.second && a.minute == b.minute && a.hour == b.hour
        && a.day == b.day && a.month == b.month && a.year == b.year;
}

int rtc_init()
{
    RtcTime now{};
    if (!rtc_read(&now)) {
        pr_module_err("rtc: the chip never settled\n");
        return -1;
    }

    attached = true;

    pr_module_info("rtc: %u-%u-%u %u:%u:%u\n",
                   now.year, now.month, now.day, now.hour, now.minute, now.second);
    return 0;
}

void rtc_exit()
{
    attached = false;
}

} // namespace
} // namespace eris::modules

extern "C" {

// The chip updates itself once a second, so a read that straddles the update
// returns a mix of before and after. Read twice and only trust a match.
bool rtc_read(RtcTime* out)
{
    using namespace eris::modules;

    if (out == nullptr)
        return false;

    const eris::u8 status_b = read_register(register_status_b);

    for (int attempt = 0; attempt < 8; ++attempt) {
        while (update_in_progress())
            eris_udelay(100);

        RtcTime first{};
        RtcTime second{};

        read_once(first, status_b);
        read_once(second, status_b);

        if (same(first, second)) {
            *out = first;
            return true;
        }
    }

    return false;
}

eris::u64 rtc_unix_time()
{
    RtcTime now{};
    if (!rtc_read(&now))
        return 0;

    // Days before the start of each month in a non leap year.
    static constexpr eris::u16 month_days[12] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334,
    };

    const eris::u64 years = now.year - 1970;
    eris::u64 days = years * 365 + (years + 1) / 4;

    if (now.month >= 1 && now.month <= 12)
        days += month_days[now.month - 1];

    const bool leap = (now.year % 4 == 0 && now.year % 100 != 0) || now.year % 400 == 0;
    if (leap && now.month > 2)
        ++days;

    days += now.day - 1;

    return ((days * 24 + now.hour) * 60 + now.minute) * 60 + now.second;
}

}

ERIS_EXPORT_SYMBOL(rtc_read);
ERIS_EXPORT_SYMBOL(rtc_unix_time);

ERIS_MODULE("rtc", "0.1", "farfromoffice", "GPL-2.0-only",
            eris::modules::rtc_init, eris::modules::rtc_exit);
