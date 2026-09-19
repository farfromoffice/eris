// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/compiler.hpp>
#include <eris/lock.hpp>
#include <eris/types.hpp>

namespace eris {

enum class ThreadState : u8 {
    Ready,
    Running,
    Sleeping,
    Blocked,
    Dead,
};

using ThreadEntry = void (*)(void* argument);

class WaitQueue;
class Scheduler;

class Thread {
public:
    static Thread* spawn(const char* name, ThreadEntry entry, void* argument);

    const char* name() const { return name_; }
    u32 id() const { return id_; }
    ThreadState state() const { return state_; }
    u32 cpu() const { return cpu_; }

    // Only the scheduler moves a thread between states, everything else asks
    // for one of these.
    void wake();

    // Where a syscall from ring 3 continues. Zero means this thread is not
    // running a user program and its own stack top will do.
    void set_syscall_stack(virt_addr top) { syscall_stack_ = top; }
    virt_addr syscall_stack() const { return syscall_stack_; }

    // The tables this thread runs on. Zero means the kernel ones, anything
    // else is the process it is carrying, and the scheduler switches to it.
    void set_address_space(u64 root) { space_root_ = root; }
    u64 address_space() const { return space_root_; }

private:
    friend class Scheduler;
    friend class WaitQueue;

    static constexpr usize name_length = 24;

    char name_[name_length]{};
    u32 id_ = 0;
    u32 cpu_ = 0;
    ThreadState state_ = ThreadState::Ready;

    u64 stack_pointer_ = 0;
    virt_addr stack_base_ = 0;
    usize stack_pages_ = 0;

    u64 wake_at_ = 0;
    virt_addr syscall_stack_ = 0;
    u64 space_root_ = 0;
    Thread* queue_next_ = nullptr;
    Thread* list_next_ = nullptr;
};

// Blocks the caller until something wakes it, which is what a driver does
// instead of spinning on a register.
class WaitQueue {
public:
    constexpr WaitQueue() = default;

    void wait();

    // Blocks with a lock already held. The lock goes only once the thread is
    // off the run queue, so a wake up cannot land in between and be lost.
    void wait(IrqSpinLock& lock, u64 flags);
    void wake_one();
    void wake_all();

private:
    Thread* head_ = nullptr;
};

void sched_init();
void sched_start_cpu();
void sched_tick();

Thread* current_thread();
void yield();
void thread_sleep_ns(u64 nanoseconds);
void thread_sleep_ms(u64 milliseconds);
ERIS_NORETURN void thread_exit();

usize thread_count();
Thread* thread_at(usize index);
const char* thread_state_name(ThreadState state);

void preempt_disable();
void preempt_enable();
bool preempt_allowed();

} // namespace eris
