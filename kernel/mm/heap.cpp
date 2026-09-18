// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/lock.hpp>
#include <eris/mm.hpp>
#include <eris/paging.hpp>
#include <eris/panic.hpp>
#include <eris/printk.hpp>
#include <eris/string.hpp>

namespace eris {
namespace {

struct BlockHeader {
    usize size;
    bool free;
    BlockHeader* next;
    BlockHeader* prev;
};

constexpr usize heap_reserved = 64 * 1024 * 1024;
constexpr usize heap_growth_step = 2 * 1024 * 1024;
constexpr usize alignment = 16;

constinit BlockHeader* head = nullptr;
constinit BlockHeader* tail = nullptr;
constinit virt_addr heap_base = 0;
constinit usize committed = 0;
constinit usize used = 0;
constinit IrqSpinLock heap_lock{};

usize align_up(usize value)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

void split(BlockHeader* block, usize size)
{
    const usize remaining = block->size - size;
    if (remaining < sizeof(BlockHeader) + alignment)
        return;

    auto* next = reinterpret_cast<BlockHeader*>(reinterpret_cast<u8*>(block) + sizeof(BlockHeader) + size);
    next->size = remaining - sizeof(BlockHeader);
    next->free = true;
    next->next = block->next;
    next->prev = block;

    if (block->next != nullptr)
        block->next->prev = next;
    else
        tail = next;

    block->next = next;
    block->size = size;
}

void merge(BlockHeader* block)
{
    if (block->next != nullptr && block->next->free) {
        BlockHeader* victim = block->next;
        block->size += sizeof(BlockHeader) + victim->size;
        block->next = victim->next;

        if (block->next != nullptr)
            block->next->prev = block;
        else
            tail = block;
    }

    if (block->prev != nullptr && block->prev->free) {
        BlockHeader* previous = block->prev;
        previous->size += sizeof(BlockHeader) + block->size;
        previous->next = block->next;

        if (previous->next != nullptr)
            previous->next->prev = previous;
        else
            tail = previous;
    }
}

// Commits more physical pages at the end of the reservation and gives them to
// the free list. The heap only occupies what it has actually handed out.
bool grow(usize wanted)
{
    usize step = heap_growth_step;
    while (step < wanted + sizeof(BlockHeader))
        step += heap_growth_step;

    if (committed + step > heap_reserved)
        return false;

    const virt_addr where = heap_base + committed;

    for (usize offset = 0; offset < step; offset += page_size) {
        const phys_addr frame = mm::alloc_page();
        if (frame == 0)
            return false;

        if (!mm::AddressSpace::kernel().map(where + offset, frame, page_size,
                                            mm::PageFlags::Write | mm::PageFlags::NoExecute)) {
            mm::free_page(frame);
            return false;
        }
    }

    auto* block = reinterpret_cast<BlockHeader*>(where);
    block->size = step - sizeof(BlockHeader);
    block->free = true;
    block->next = nullptr;
    block->prev = tail;

    if (tail != nullptr)
        tail->next = block;
    tail = block;

    if (head == nullptr)
        head = block;

    committed += step;
    merge(block);
    return true;
}

} // namespace

namespace mm {

void heap_init()
{
    heap_base = vmalloc_reserve(heap_reserved);
    if (heap_base == 0)
        panic("heap: no virtual space for the reservation");

    head = nullptr;
    tail = nullptr;
    committed = 0;
    used = 0;

    // One growth step is the initial commit, asking for the step itself would
    // round up to two.
    if (!grow(page_size))
        panic("heap: cannot commit the first %lu KiB", static_cast<u64>(heap_growth_step / 1024));
}

} // namespace mm

void* kmalloc(usize size)
{
    if (size == 0 || size > heap_reserved)
        return nullptr;

    const usize wanted = align_up(size);
    IrqGuard guard(heap_lock);

    for (int attempt = 0; attempt < 2; ++attempt) {
        for (BlockHeader* block = head; block != nullptr; block = block->next) {
            if (!block->free || block->size < wanted)
                continue;

            split(block, wanted);
            block->free = false;
            used += block->size;
            return reinterpret_cast<u8*>(block) + sizeof(BlockHeader);
        }

        if (!grow(wanted))
            return nullptr;
    }

    return nullptr;
}

void* kzalloc(usize size)
{
    void* ptr = kmalloc(size);
    if (ptr != nullptr)
        memset(ptr, 0, size);
    return ptr;
}

void kfree(void* ptr)
{
    if (ptr == nullptr)
        return;

    IrqGuard guard(heap_lock);

    auto* block = reinterpret_cast<BlockHeader*>(static_cast<u8*>(ptr) - sizeof(BlockHeader));
    if (block->free)
        return;

    block->free = true;
    used -= block->size;
    merge(block);
}

usize heap_used()
{
    return used;
}

usize heap_capacity()
{
    return committed;
}

} // namespace eris
