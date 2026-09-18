// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

namespace eris {

// The narrow set the kernel needs, over the compiler builtins, so there is one
// place to look when the memory ordering turns out to be wrong.
template<typename T>
class Atomic {
public:
    constexpr Atomic() = default;
    constexpr explicit Atomic(T value)
        : value_(value)
    {
    }

    T load() const { return __atomic_load_n(&value_, __ATOMIC_ACQUIRE); }
    void store(T value) { __atomic_store_n(&value_, value, __ATOMIC_RELEASE); }

    T fetch_add(T delta) { return __atomic_fetch_add(&value_, delta, __ATOMIC_ACQ_REL); }
    T fetch_sub(T delta) { return __atomic_fetch_sub(&value_, delta, __ATOMIC_ACQ_REL); }

    T exchange(T value) { return __atomic_exchange_n(&value_, value, __ATOMIC_ACQ_REL); }

    bool compare_exchange(T& expected, T desired)
    {
        return __atomic_compare_exchange_n(&value_, &expected, desired, false,
                                           __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    }

    T operator++() { return fetch_add(1) + 1; }
    T operator--() { return fetch_sub(1) - 1; }

private:
    T value_{};
};

class RefCount {
public:
    constexpr RefCount() = default;

    void take() { count_.fetch_add(1); }

    // True when this was the reference that brought it back to zero.
    bool release()
    {
        u32 previous = count_.fetch_sub(1);
        return previous == 1;
    }

    u32 value() const { return count_.load(); }
    bool held() const { return count_.load() != 0; }

private:
    Atomic<u32> count_{0};
};

inline void memory_barrier()
{
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

inline void cpu_relax()
{
    asm volatile("pause" : : : "memory");
}

} // namespace eris
