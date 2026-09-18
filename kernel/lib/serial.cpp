// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/console.hpp>
#include <eris/io.hpp>
#include <eris/serial.hpp>

namespace eris {
namespace {

constexpr u16 port = 0x3F8;

class SerialConsole final : public Console {
public:
    void put(char c) override
    {
        if (c == '\n')
            put('\r');

        while ((arch::inb(port + 5) & 0x20) == 0)
            ;
        arch::outb(port, static_cast<u8>(c));
    }
};

SerialConsole instance{};

} // namespace

bool serial_init()
{
    using namespace eris::arch;

    outb(port + 1, 0x00);
    outb(port + 3, 0x80);
    outb(port + 0, 0x01);
    outb(port + 1, 0x00);
    outb(port + 3, 0x03);
    outb(port + 2, 0xC7);
    outb(port + 4, 0x1E);

    outb(port + 0, 0xAE);
    if (inb(port + 0) != 0xAE)
        return false;

    outb(port + 4, 0x0F);
    console_register(&instance);
    return true;
}

} // namespace eris
