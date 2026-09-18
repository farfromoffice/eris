// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/console.hpp>
#include <eris/export.hpp>
#include <eris/io.hpp>
#include <eris/irq.hpp>
#include <eris/module.hpp>

#include "keyboard.hpp"

namespace eris::modules {
namespace {

constexpr usize max_subscribers = 8;

constexpr char scancode_map[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ',
};

constinit KeyHandler subscribers[max_subscribers]{};
constinit usize subscriber_count = 0;

void keyboard_irq(arch::Registers&)
{
    const u8 scancode = arch::inb(0x60);
    if ((scancode & 0x80) != 0)
        return;

    const char c = scancode < sizeof(scancode_map) ? scancode_map[scancode] : 0;
    if (c == 0)
        return;

    if (subscriber_count == 0) {
        console_put(c);
        return;
    }

    for (usize i = 0; i < subscriber_count; ++i)
        subscribers[i](c);
}

int keyboard_module_init()
{
    arch::irq_register(1, keyboard_irq);
    arch::irq_unmask(1);
    return 0;
}

void keyboard_module_exit()
{
    arch::irq_mask(1);
    arch::irq_register(1, nullptr);
    subscriber_count = 0;
}

} // namespace
} // namespace eris::modules

extern "C" {

bool keyboard_subscribe(KeyHandler handler)
{
    using namespace eris::modules;

    if (handler == nullptr || subscriber_count >= max_subscribers)
        return false;

    subscribers[subscriber_count++] = handler;
    return true;
}

void keyboard_unsubscribe(KeyHandler handler)
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

}

ERIS_EXPORT_SYMBOL(keyboard_subscribe);
ERIS_EXPORT_SYMBOL(keyboard_unsubscribe);

ERIS_MODULE("keyboard", "0.1", "eris", "GPL-2.0-only", eris::modules::keyboard_module_init,
            eris::modules::keyboard_module_exit);
