// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/export.hpp>
#include <eris/module.hpp>
#include <eris/module_api.hpp>

#include "ps2mouse.hpp"

namespace eris::modules {
namespace {

constexpr u16 port_data = 0x60;
constexpr u16 port_status = 0x64;
constexpr u16 port_command = 0x64;

constexpr u8 status_output_full = 0x01;
constexpr u8 status_input_full = 0x02;
constexpr u8 status_from_mouse = 0x20;

constexpr u8 command_enable_aux = 0xA8;
constexpr u8 command_read_config = 0x20;
constexpr u8 command_write_config = 0x60;
constexpr u8 command_to_mouse = 0xD4;

constexpr u8 config_aux_interrupt = 0x02;
constexpr u8 config_aux_clock = 0x20;

constexpr u8 mouse_set_defaults = 0xF6;
constexpr u8 mouse_set_sample_rate = 0xF3;
constexpr u8 mouse_set_resolution = 0xE8;
constexpr u8 mouse_enable_reporting = 0xF4;
constexpr u8 mouse_acknowledge = 0xFA;

constexpr u8 packet_left = 0x01;
constexpr u8 packet_right = 0x02;
constexpr u8 packet_middle = 0x04;
constexpr u8 packet_always_one = 0x08;
constexpr u8 packet_x_sign = 0x10;
constexpr u8 packet_y_sign = 0x20;
constexpr u8 packet_x_overflow = 0x40;
constexpr u8 packet_y_overflow = 0x80;

constexpr usize max_subscribers = 4;
constexpr u8 irq_mouse = 12;

constinit MouseHandler subscribers[max_subscribers]{};
constinit usize subscriber_count = 0;

constinit u8 packet[3]{};
constinit usize packet_index = 0;
constinit bool attached = false;

bool wait_writable()
{
    for (usize i = 0; i < 100000; ++i) {
        if ((eris_inb(port_status) & status_input_full) == 0)
            return true;
    }
    return false;
}

bool wait_readable()
{
    for (usize i = 0; i < 100000; ++i) {
        if ((eris_inb(port_status) & status_output_full) != 0)
            return true;
    }
    return false;
}

void write_command(u8 command)
{
    wait_writable();
    eris_outb(port_command, command);
}

bool write_to_mouse(u8 value)
{
    write_command(command_to_mouse);

    if (!wait_writable())
        return false;

    eris_outb(port_data, value);

    if (!wait_readable())
        return false;

    return eris_inb(port_data) == mouse_acknowledge;
}

// One three byte packet: buttons and two nine bit deltas, the ninth bit living
// in the first byte. The always-one bit is the only thing that says the stream
// is still in step.
void handle_packet()
{
    const u8 flags = packet[0];

    if ((flags & packet_always_one) == 0)
        return;

    if ((flags & (packet_x_overflow | packet_y_overflow)) != 0)
        return;

    i32 dx = packet[1];
    i32 dy = packet[2];

    if ((flags & packet_x_sign) != 0)
        dx |= ~0xFF;
    if ((flags & packet_y_sign) != 0)
        dy |= ~0xFF;

    // The mouse counts up as it moves away from the user, the screen counts
    // down, so the vertical axis is flipped here rather than in every handler.
    const u8 buttons = flags & (packet_left | packet_right | packet_middle);

    for (usize i = 0; i < subscriber_count; ++i)
        subscribers[i](dx, -dy, buttons);
}

void mouse_irq(void*)
{
    const u8 status = eris_inb(port_status);

    if ((status & status_output_full) == 0 || (status & status_from_mouse) == 0)
        return;

    const u8 value = eris_inb(port_data);

    if (packet_index == 0 && (value & packet_always_one) == 0)
        return;

    packet[packet_index++] = value;

    if (packet_index == 3) {
        packet_index = 0;
        handle_packet();
    }
}

int ps2mouse_init()
{
    // The keyboard shares this controller and its handler would eat the
    // answers, so its line stays masked while the second port is set up.
    eris_irq_mask(1);

    write_command(command_enable_aux);

    write_command(command_read_config);
    if (!wait_readable()) {
        pr_module_info("ps2mouse: the controller does not answer\n");
        eris_irq_unmask(1);
        return 0;
    }

    u8 config = eris_inb(port_data);
    config |= config_aux_interrupt;
    config &= static_cast<u8>(~config_aux_clock);

    write_command(command_write_config);
    if (!wait_writable()) {
        eris_irq_unmask(1);
        return 0;
    }
    eris_outb(port_data, config);

    if (!write_to_mouse(mouse_set_defaults)) {
        pr_module_info("ps2mouse: no mouse on the second port\n");
        eris_irq_unmask(1);
        return 0;
    }

    // Defaults are a hundred reports a second at four counts a millimetre,
    // which is enough to see the pointer step. Twice the rate and twice the
    // resolution is what makes it feel attached to the hand.
    if (write_to_mouse(mouse_set_sample_rate))
        write_to_mouse(200);

    if (write_to_mouse(mouse_set_resolution))
        write_to_mouse(3);

    if (!write_to_mouse(mouse_enable_reporting)) {
        pr_module_info("ps2mouse: no mouse on the second port\n");
        eris_irq_unmask(1);
        return 0;
    }

    packet_index = 0;
    attached = true;

    eris_irq_register(irq_mouse, mouse_irq);
    eris_irq_unmask(irq_mouse);
    eris_irq_unmask(1);

    pr_module_info("ps2mouse: reporting on irq %u\n", irq_mouse);
    return 0;
}

void ps2mouse_exit()
{
    if (!attached)
        return;

    eris_irq_mask(irq_mouse);
    eris_irq_register(irq_mouse, nullptr);

    subscriber_count = 0;
    attached = false;
}

} // namespace
} // namespace eris::modules

extern "C" {

bool mouse_subscribe(MouseHandler handler)
{
    using namespace eris::modules;

    if (handler == nullptr || subscriber_count >= max_subscribers)
        return false;

    subscribers[subscriber_count++] = handler;
    return true;
}

void mouse_unsubscribe(MouseHandler handler)
{
    using namespace eris::modules;

    for (eris::usize i = 0; i < subscriber_count; ++i) {
        if (subscribers[i] != handler)
            continue;

        for (eris::usize j = i; j + 1 < subscriber_count; ++j)
            subscribers[j] = subscribers[j + 1];

        --subscriber_count;
        return;
    }
}

bool mouse_present()
{
    return eris::modules::attached;
}

}

ERIS_EXPORT_SYMBOL(mouse_subscribe);
ERIS_EXPORT_SYMBOL(mouse_unsubscribe);
ERIS_EXPORT_SYMBOL(mouse_present);

ERIS_MODULE("ps2mouse", "0.1", "farfromoffice", "GPL-2.0-only",
            eris::modules::ps2mouse_init, eris::modules::ps2mouse_exit);
