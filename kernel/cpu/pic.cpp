// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/io.hpp>
#include <eris/irq.hpp>

namespace eris::arch {
namespace {

constexpr u16 pic1_command = 0x20;
constexpr u16 pic1_data = 0x21;
constexpr u16 pic2_command = 0xA0;
constexpr u16 pic2_data = 0xA1;
constexpr u8 eoi = 0x20;

}

void pic_init()
{
    const u8 mask1 = inb(pic1_data);
    const u8 mask2 = inb(pic2_data);

    outb(pic1_command, 0x11);
    io_wait();
    outb(pic2_command, 0x11);
    io_wait();
    outb(pic1_data, 0x20);
    io_wait();
    outb(pic2_data, 0x28);
    io_wait();
    outb(pic1_data, 0x04);
    io_wait();
    outb(pic2_data, 0x02);
    io_wait();
    outb(pic1_data, 0x01);
    io_wait();
    outb(pic2_data, 0x01);
    io_wait();

    outb(pic1_data, mask1);
    outb(pic2_data, mask2);
}

void irq_eoi(u8 irq)
{
    if (irq >= 8)
        outb(pic2_command, eoi);
    outb(pic1_command, eoi);
}

void irq_unmask(u8 irq)
{
    const u16 port = irq < 8 ? pic1_data : pic2_data;
    const u8 bit = static_cast<u8>(irq < 8 ? irq : irq - 8);
    outb(port, static_cast<u8>(inb(port) & ~(1u << bit)));
}

void irq_mask(u8 irq)
{
    const u16 port = irq < 8 ? pic1_data : pic2_data;
    const u8 bit = static_cast<u8>(irq < 8 ? irq : irq - 8);
    outb(port, static_cast<u8>(inb(port) | (1u << bit)));
}

}
