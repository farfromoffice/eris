// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/atomic.hpp>
#include <eris/compiler.hpp>
#include <eris/io.hpp>
#include <eris/types.hpp>

namespace eris {

class SpinLock {
public:
    constexpr SpinLock() = default;

    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;

    void lock()
    {
        while (locked_.exchange(true))
            cpu_relax();
    }

    bool try_lock() { return !locked_.exchange(true); }

    void unlock() { locked_.store(false); }

    bool held() const { return locked_.load(); }

private:
    Atomic<bool> locked_{false};
};

// The one to reach for when an interrupt handler touches the same data, which
// in this kernel is most of it.
class IrqSpinLock {
public:
    constexpr IrqSpinLock() = default;

    IrqSpinLock(const IrqSpinLock&) = delete;
    IrqSpinLock& operator=(const IrqSpinLock&) = delete;

    u64 lock()
    {
        const u64 flags = save_flags();
        arch::cli();

        const u32 self = current_cpu_index();
        if (inner_.held() && owner_.load() == self)
            lock_recursion_panic();

        inner_.lock();
        owner_.store(self);
        return flags;
    }

    void unlock(u64 flags)
    {
        owner_.store(no_owner);
        inner_.unlock();
        restore_flags(flags);
    }

    bool held() const { return inner_.held(); }

private:
    static u64 save_flags()
    {
        u64 flags;
        asm volatile("pushfq\n pop %0" : "=r"(flags) : : "memory");
        return flags;
    }

    static void restore_flags(u64 flags)
    {
        if ((flags & (1ULL << 9)) != 0)
            arch::sti();
    }

    static constexpr u32 no_owner = 0xFFFFFFFF;

    static u32 current_cpu_index();
    ERIS_NORETURN static void lock_recursion_panic();

    SpinLock inner_;
    Atomic<u32> owner_{no_owner};
};

// The console is written to from inside functions that already hold it, so it
// needs the one lock that lets the same CPU back in.
class RecursiveIrqLock {
public:
    constexpr RecursiveIrqLock() = default;

    RecursiveIrqLock(const RecursiveIrqLock&) = delete;
    RecursiveIrqLock& operator=(const RecursiveIrqLock&) = delete;

    u64 lock()
    {
        const u64 flags = save_flags();
        arch::cli();

        const u32 self = current_cpu_index();
        if (depth_ > 0 && owner_.load() == self) {
            ++depth_;
            return flags;
        }

        inner_.lock();
        owner_.store(self);
        depth_ = 1;
        return flags;
    }

    void unlock(u64 flags)
    {
        if (--depth_ == 0) {
            owner_.store(no_owner);
            inner_.unlock();
        }

        restore_flags(flags);
    }

private:
    static constexpr u32 no_owner = 0xFFFFFFFF;

    static u64 save_flags()
    {
        u64 flags;
        asm volatile("pushfq\n pop %0" : "=r"(flags) : : "memory");
        return flags;
    }

    static void restore_flags(u64 flags)
    {
        if ((flags & (1ULL << 9)) != 0)
            arch::sti();
    }

    static u32 current_cpu_index();

    SpinLock inner_;
    Atomic<u32> owner_{no_owner};
    u32 depth_ = 0;
};

class RecursiveGuard {
public:
    explicit RecursiveGuard(RecursiveIrqLock& lock)
        : lock_(lock)
        , flags_(lock.lock())
    {
    }

    ~RecursiveGuard() { lock_.unlock(flags_); }

    RecursiveGuard(const RecursiveGuard&) = delete;
    RecursiveGuard& operator=(const RecursiveGuard&) = delete;

private:
    RecursiveIrqLock& lock_;
    u64 flags_;
};

template<typename Lock>
class Guard {
public:
    explicit Guard(Lock& lock)
        : lock_(lock)
    {
        lock_.lock();
    }

    ~Guard() { lock_.unlock(); }

    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;

private:
    Lock& lock_;
};

class IrqGuard {
public:
    explicit IrqGuard(IrqSpinLock& lock)
        : lock_(lock)
        , flags_(lock.lock())
    {
    }

    ~IrqGuard() { lock_.unlock(flags_); }

    IrqGuard(const IrqGuard&) = delete;
    IrqGuard& operator=(const IrqGuard&) = delete;

private:
    IrqSpinLock& lock_;
    u64 flags_;
};

} // namespace eris
