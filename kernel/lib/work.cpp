// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/io.hpp>
#include <eris/work.hpp>

namespace eris {
namespace {

constexpr usize queue_size = 64;

struct Item {
    WorkFunction function;
    void* context;
};

constinit Item queue[queue_size]{};
constinit usize head = 0;
constinit usize tail = 0;
constinit u64 dropped = 0;

} // namespace

bool schedule_work(WorkFunction function, void* context)
{
    if (function == nullptr)
        return false;

    const usize next = (tail + 1) % queue_size;
    if (next == head) {
        ++dropped;
        return false;
    }

    queue[tail] = Item{function, context};
    tail = next;
    return true;
}

void work_run_pending()
{
    while (head != tail) {
        // Taking the item before running it keeps the slot reusable if the
        // work schedules more work.
        const Item item = queue[head];
        head = (head + 1) % queue_size;
        item.function(item.context);
    }
}

usize work_pending()
{
    return tail >= head ? tail - head : queue_size - head + tail;
}

u64 work_dropped()
{
    return dropped;
}

} // namespace eris
