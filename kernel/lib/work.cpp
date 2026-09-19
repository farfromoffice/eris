// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/atomic.hpp>
#include <eris/cpu.hpp>
#include <eris/io.hpp>
#include <eris/lock.hpp>
#include <eris/thread.hpp>
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
constinit IrqSpinLock queue_lock{};
constinit WaitQueue waiting{};
constinit Atomic<u64> in_flight[arch::max_cpus]{};

bool any_in_flight(virt_addr base, usize length)
{
    for (auto& slot : in_flight) {
        const u64 address = slot.load();
        if (address >= base && address - base < length)
            return true;
    }
    return false;
}

} // namespace

bool schedule_work(WorkFunction function, void* context)
{
    if (function == nullptr)
        return false;

    IrqGuard guard(queue_lock);

    const usize next = (tail + 1) % queue_size;
    if (next == head) {
        ++dropped;
        return false;
    }

    queue[tail] = Item{function, context};
    tail = next;
    waiting.wake_all();
    return true;
}

void work_run_pending()
{
    for (;;) {
        const u64 flags = queue_lock.lock();

        if (head == tail) {
            queue_lock.unlock(flags);
            return;
        }

        // Taking the item before running it keeps the slot reusable if the
        // work schedules more work, and the lock is not held across the call.
        const Item item = queue[head];
        head = (head + 1) % queue_size;

        Atomic<u64>& slot = in_flight[arch::this_cpu().index];
        slot.store(reinterpret_cast<u64>(item.function));

        queue_lock.unlock(flags);

        item.function(item.context);
        slot.store(0);
    }
}

usize work_cancel_owner(virt_addr base, usize length)
{
    usize dropped_here = 0;

    {
        IrqGuard guard(queue_lock);

        // The items that stay keep their order.
        usize read = head;
        usize write = head;

        while (read != tail) {
            const Item item = queue[read];
            read = (read + 1) % queue_size;

            const auto address = reinterpret_cast<virt_addr>(item.function);
            if (address >= base && address - base < length) {
                ++dropped_here;
                continue;
            }

            queue[write] = item;
            write = (write + 1) % queue_size;
        }

        tail = write;
    }

    while (any_in_flight(base, length))
        cpu_relax();

    return dropped_here;
}

void work_thread(void*)
{
    for (;;) {
        work_run_pending();

        // Sleeping on the queue rather than on a timer is what keeps a queued
        // item waiting microseconds instead of ten milliseconds.
        const u64 flags = queue_lock.lock();

        if (head != tail) {
            queue_lock.unlock(flags);
            continue;
        }

        waiting.wait(queue_lock, flags);
    }
}

void work_start()
{
    Thread::spawn("kworker", work_thread, nullptr);
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
