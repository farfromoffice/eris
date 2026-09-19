// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/cpu.hpp>
#include <eris/io.hpp>
#include <eris/lock.hpp>
#include <eris/mm.hpp>
#include <eris/paging.hpp>
#include <eris/panic.hpp>
#include <eris/printk.hpp>
#include <eris/string.hpp>
#include <eris/thread.hpp>
#include <eris/time.hpp>

extern "C" {
void context_switch(eris::u64* save_here, eris::u64 resume_from);
void thread_entry_trampoline();
}

namespace eris {

// The one thing allowed to move a thread between states. Declared as a friend
// of Thread, so the bookkeeping stays out of the public interface.
class Scheduler {
public:
    static void enqueue(Thread* thread);
    static Thread* dequeue();
    static void park_sleeper_at(Thread* thread, u64 deadline);
    static Thread* next_in_list(Thread* thread);
    static void wake_expired_sleepers(u64 now);
    static void switch_to(Thread* next);
    static void set_current(arch::PerCpu& cpu, Thread* thread);
    static Thread* idle_thread_of(arch::PerCpu& cpu);

    static void detach_from_sleepers(Thread* thread);
    static void block_on(Thread*& queue_head, Thread* thread);
    static Thread* take_from(Thread*& queue_head);

    static void prepare(Thread* thread, const char* name, ThreadEntry entry, void* argument);
    static void adopt(Thread* thread);
    static void mark_dead(Thread* thread);
    static void make_idle(arch::PerCpu& cpu, Thread* idle);
};

namespace {

constexpr usize stack_pages = 8;
constexpr usize max_threads = 64;

constinit IrqSpinLock sched_lock{};

constinit Thread* run_queue_head = nullptr;
constinit Thread* run_queue_tail = nullptr;

constinit Thread* all_threads = nullptr;
constinit usize threads_alive = 0;
constinit u32 next_thread_id = 1;
constinit bool running = false;

// A thread that fell asleep is parked here until its deadline passes, so the
// scheduler never walks threads it cannot run.
constinit Thread* sleepers = nullptr;

} // namespace

void Scheduler::enqueue(Thread* thread)
{
    thread->queue_next_ = nullptr;
    thread->state_ = ThreadState::Ready;

    if (run_queue_tail == nullptr) {
        run_queue_head = thread;
        run_queue_tail = thread;
        return;
    }

    run_queue_tail->queue_next_ = thread;
    run_queue_tail = thread;
}

Thread* Scheduler::dequeue()
{
    Thread* thread = run_queue_head;
    if (thread == nullptr)
        return nullptr;

    run_queue_head = thread->queue_next_;
    if (run_queue_head == nullptr)
        run_queue_tail = nullptr;

    thread->queue_next_ = nullptr;
    return thread;
}

void Scheduler::park_sleeper_at(Thread* thread, u64 deadline)
{
    thread->wake_at_ = deadline;
    thread->state_ = ThreadState::Sleeping;
    thread->queue_next_ = sleepers;
    sleepers = thread;
}

Thread* Scheduler::next_in_list(Thread* thread)
{
    return thread->list_next_;
}

void Scheduler::wake_expired_sleepers(u64 now)
{
    Thread* remaining = nullptr;

    while (sleepers != nullptr) {
        Thread* thread = sleepers;
        sleepers = thread->queue_next_;

        if (thread->wake_at_ <= now) {
            enqueue(thread);
            continue;
        }

        thread->queue_next_ = remaining;
        remaining = thread;
    }

    sleepers = remaining;
}

Thread* Scheduler::idle_thread_of(arch::PerCpu& cpu)
{
    return static_cast<Thread*>(cpu.idle_thread);
}

void Scheduler::set_current(arch::PerCpu& cpu, Thread* thread)
{
    cpu.current_thread = thread;
    thread->cpu_ = cpu.index;
    thread->state_ = ThreadState::Running;
}

// Picks the next thread for this CPU and leaves the old one wherever the
// caller put it. The scheduler lock is held on the way in and released after
// the switch, by whoever resumes next.
void Scheduler::switch_to(Thread* next)
{
    arch::PerCpu& cpu = arch::this_cpu();
    auto* previous = static_cast<Thread*>(cpu.current_thread);

    if (previous == next) {
        set_current(cpu, next);
        return;
    }

    set_current(cpu, next);

    // A thread that dropped into ring 3 keeps its own syscall stack, and the
    // scheduler must not hand the CPU one that is already in use. An idle
    // thread has no stack of its own and never enters ring 3, so the previous
    // value is left alone rather than replaced with zero.
    const virt_addr kernel_stack = next->syscall_stack_ != 0
        ? next->syscall_stack_
        : (next->stack_base_ != 0 ? next->stack_base_ + next->stack_pages_ * page_size : 0);

    if (kernel_stack != 0)
        arch::tss_set_kernel_stack(kernel_stack);

    // The address space follows the thread, otherwise a program resumed on
    // another core comes back to tables that never mapped it.
    const u64 wanted = next->space_root_ != 0 ? next->space_root_
                                              : mm::AddressSpace::kernel().root();
    u64 current = 0;
    asm volatile("mov %%cr3, %0" : "=r"(current));

    if (wanted != current)
        asm volatile("mov %0, %%cr3" : : "r"(wanted) : "memory");
    context_switch(&previous->stack_pointer_, next->stack_pointer_);
}

void Scheduler::detach_from_sleepers(Thread* thread)
{
    Thread** link = &sleepers;
    while (*link != nullptr) {
        if (*link == thread) {
            *link = thread->queue_next_;
            return;
        }
        link = &(*link)->queue_next_;
    }
}

void Scheduler::block_on(Thread*& queue_head, Thread* thread)
{
    thread->state_ = ThreadState::Blocked;
    thread->queue_next_ = queue_head;
    queue_head = thread;
}

Thread* Scheduler::take_from(Thread*& queue_head)
{
    Thread* thread = queue_head;
    if (thread == nullptr)
        return nullptr;

    queue_head = thread->queue_next_;
    thread->queue_next_ = nullptr;
    return thread;
}

void Scheduler::prepare(Thread* thread, const char* name, ThreadEntry entry, void* argument)
{
    usize i = 0;
    while (i + 1 < Thread::name_length && name[i] != '\0') {
        thread->name_[i] = name[i];
        ++i;
    }
    thread->name_[i] = '\0';

    // The frame the switch will pop: six callee saved registers and the return
    // address it lands on.
    auto* top = reinterpret_cast<u64*>(thread->stack_base_ + thread->stack_pages_ * page_size);
    *--top = reinterpret_cast<u64>(&thread_entry_trampoline);
    *--top = 0;                                   // rbp
    *--top = 0;                                   // rbx
    *--top = reinterpret_cast<u64>(entry);        // r12
    *--top = reinterpret_cast<u64>(argument);     // r13
    *--top = 0;                                   // r14
    *--top = 0;                                   // r15

    thread->stack_pointer_ = reinterpret_cast<u64>(top);
    thread->state_ = ThreadState::Ready;
}

void Scheduler::adopt(Thread* thread)
{
    thread->id_ = next_thread_id++;
    thread->list_next_ = all_threads;
    all_threads = thread;
    ++threads_alive;

    enqueue(thread);
}

void Scheduler::mark_dead(Thread* thread)
{
    thread->state_ = ThreadState::Dead;
    --threads_alive;
}

void Scheduler::make_idle(arch::PerCpu& cpu, Thread* idle)
{
    idle->id_ = 0;
    idle->cpu_ = cpu.index;
    idle->state_ = ThreadState::Running;
    memcpy(idle->name_, "idle", 5);

    cpu.idle_thread = idle;
    cpu.current_thread = idle;
}

extern "C" void thread_entry_start(ThreadEntry entry, void* argument)
{
    // The switch handed us the lock the previous thread was holding.
    sched_lock.unlock(1ULL << 9);

    entry(argument);
    thread_exit();
}

Thread* Thread::spawn(const char* name, ThreadEntry entry, void* argument)
{
    if (threads_alive >= max_threads)
        return nullptr;

    auto* thread = static_cast<Thread*>(kzalloc(sizeof(Thread)));
    if (thread == nullptr)
        return nullptr;

    // One page below the stack stays unmapped, so a thread that runs off the
    // end faults instead of walking into whatever was allocated before it.
    const virt_addr region = mm::vmalloc_reserve((stack_pages + 1) * page_size);
    if (region == 0) {
        kfree(thread);
        return nullptr;
    }

    for (usize i = 0; i < stack_pages; ++i) {
        const phys_addr frame = mm::alloc_page();
        if (frame == 0) {
            mm::vmalloc_release(region, (stack_pages + 1) * page_size);
            kfree(thread);
            return nullptr;
        }

        mm::AddressSpace::kernel().map(region + (i + 1) * page_size, frame, page_size,
                                       mm::PageFlags::Write | mm::PageFlags::NoExecute);
    }

    thread->stack_base_ = region + page_size;
    thread->stack_pages_ = stack_pages;

    Scheduler::prepare(thread, name, entry, argument);

    IrqGuard guard(sched_lock);
    Scheduler::adopt(thread);
    return thread;
}

void Thread::wake()
{
    IrqGuard guard(sched_lock);

    if (state_ == ThreadState::Blocked || state_ == ThreadState::Sleeping) {
        Scheduler::detach_from_sleepers(this);
        Scheduler::enqueue(this);
    }
}

Thread* current_thread()
{
    return static_cast<Thread*>(arch::this_cpu().current_thread);
}

void yield()
{
    if (!running)
        return;

    const u64 flags = sched_lock.lock();

    Scheduler::wake_expired_sleepers(monotonic_ns());

    Thread* next = Scheduler::dequeue();
    Thread* self = current_thread();

    if (next == nullptr) {
        sched_lock.unlock(flags);
        return;
    }

    if (self != nullptr && self != Scheduler::idle_thread_of(arch::this_cpu()))
        Scheduler::enqueue(self);

    Scheduler::switch_to(next);
    sched_lock.unlock(flags);
}

void thread_sleep_ns(u64 nanoseconds)
{
    if (!running) {
        mdelay(nanoseconds / 1000000);
        return;
    }

    const u64 flags = sched_lock.lock();

    Thread* self = current_thread();
    Scheduler::park_sleeper_at(self, monotonic_ns() + nanoseconds);

    Thread* next = Scheduler::dequeue();
    if (next == nullptr)
        next = Scheduler::idle_thread_of(arch::this_cpu());

    Scheduler::switch_to(next);
    sched_lock.unlock(flags);
}

void thread_sleep_ms(u64 milliseconds)
{
    thread_sleep_ns(milliseconds * 1000000);
}

void thread_exit()
{
    const u64 flags = sched_lock.lock();

    Thread* self = current_thread();
    Scheduler::mark_dead(self);

    Thread* next = Scheduler::dequeue();
    if (next == nullptr)
        next = Scheduler::idle_thread_of(arch::this_cpu());

    Scheduler::switch_to(next);
    sched_lock.unlock(flags);

    panic("thread %s came back from the dead", self->name());
}

void sched_tick()
{
    if (!running || !preempt_allowed())
        return;

    yield();
}

void WaitQueue::wait()
{
    const u64 flags = sched_lock.lock();

    Thread* self = current_thread();
    Scheduler::block_on(head_, self);

    Thread* next = Scheduler::dequeue();
    if (next == nullptr)
        next = Scheduler::idle_thread_of(arch::this_cpu());

    Scheduler::switch_to(next);
    sched_lock.unlock(flags);
}

void WaitQueue::wake_one()
{
    IrqGuard guard(sched_lock);

    if (Thread* thread = Scheduler::take_from(head_); thread != nullptr)
        Scheduler::enqueue(thread);
}

void WaitQueue::wake_all()
{
    IrqGuard guard(sched_lock);

    while (Thread* thread = Scheduler::take_from(head_))
        Scheduler::enqueue(thread);
}

void preempt_disable()
{
    ++arch::this_cpu().preempt_count;
}

void preempt_enable()
{
    if (arch::this_cpu().preempt_count > 0)
        --arch::this_cpu().preempt_count;
}

bool preempt_allowed()
{
    return arch::this_cpu().preempt_count == 0;
}

usize thread_count()
{
    return threads_alive;
}

Thread* thread_at(usize index)
{
    Thread* thread = all_threads;
    for (usize i = 0; thread != nullptr && i < index; ++i)
        thread = Scheduler::next_in_list(thread);
    return thread;
}

const char* thread_state_name(ThreadState state)
{
    switch (state) {
    case ThreadState::Ready:    return "ready";
    case ThreadState::Running:  return "running";
    case ThreadState::Sleeping: return "sleeping";
    case ThreadState::Blocked:  return "blocked";
    case ThreadState::Dead:     return "dead";
    }
    return "unknown";
}

// Each CPU turns its boot path into a thread of its own, which is what gives
// the scheduler somewhere to switch back to.
void sched_start_cpu()
{
    arch::PerCpu& cpu = arch::this_cpu();

    auto* idle = static_cast<Thread*>(kzalloc(sizeof(Thread)));
    if (idle == nullptr)
        panic("sched: no memory for the idle thread of cpu %u", cpu.index);

    Scheduler::make_idle(cpu, idle);
}

void sched_init()
{
    sched_start_cpu();
    running = true;

    pr_info("sched: round robin, preemption on the timer\n");
}

} // namespace eris
