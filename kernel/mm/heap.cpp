// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/mm.hpp>
#include <eris/panic.hpp>
#include <eris/string.hpp>

namespace eris {
namespace {

struct BlockHeader {
    usize size;
    bool free;
    BlockHeader* next;
    BlockHeader* prev;
};

constexpr usize heap_pages = 512;
constexpr usize alignment = 16;

constinit BlockHeader* head = nullptr;
constinit usize capacity = 0;
constinit usize used = 0;

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
    block->next = next;
    block->size = size;
}

void merge(BlockHeader* block)
{
    if (block->next != nullptr && block->next->free) {
        block->size += sizeof(BlockHeader) + block->next->size;
        block->next = block->next->next;
        if (block->next != nullptr)
            block->next->prev = block;
    }

    if (block->prev != nullptr && block->prev->free) {
        block->prev->size += sizeof(BlockHeader) + block->size;
        block->prev->next = block->next;
        if (block->next != nullptr)
            block->next->prev = block->prev;
    }
}

} // namespace

namespace mm {

void heap_init()
{
    const phys_addr base = alloc_pages(heap_pages);
    if (base == 0)
        panic("heap: cannot reserve %lu pages", static_cast<u64>(heap_pages));

    capacity = heap_pages * page_size;
    head = reinterpret_cast<BlockHeader*>(base);
    head->size = capacity - sizeof(BlockHeader);
    head->free = true;
    head->next = nullptr;
    head->prev = nullptr;
    used = 0;
}

} // namespace mm

void* kmalloc(usize size)
{
    if (size == 0 || size > capacity)
        return nullptr;

    const usize wanted = align_up(size);

    for (BlockHeader* block = head; block != nullptr; block = block->next) {
        if (!block->free || block->size < wanted)
            continue;

        split(block, wanted);
        block->free = false;
        used += block->size;
        return reinterpret_cast<u8*>(block) + sizeof(BlockHeader);
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
    return capacity;
}

} // namespace eris
