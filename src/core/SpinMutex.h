// Copyright 2020 WindTerm, 2026 FastShell contributors. Apache-2.0.
//
// Small spin lock for very short critical sections. Meets the C++ Lockable
// requirements, so use it with std::lock_guard / std::scoped_lock.
//
// Changes from the original: test-and-test-and-set (spins on a plain load, so
// the cache line is not hammered), a CPU pause hint, and falls back to
// std::atomic::wait instead of sleeping for a fixed time.
#pragma once

#include <atomic>
#include <thread>

#if defined(_MSC_VER) || defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#define FASTSHELL_CPU_RELAX() _mm_pause()
#elif defined(__aarch64__)
#define FASTSHELL_CPU_RELAX() asm volatile("yield")
#else
#define FASTSHELL_CPU_RELAX() ((void)0)
#endif

class SpinMutex {
public:
    SpinMutex() = default;
    SpinMutex(const SpinMutex &) = delete;
    SpinMutex &operator=(const SpinMutex &) = delete;

    void lock() noexcept {
        for (int spins = 0;; ++spins) {
            if (!m_locked.exchange(true, std::memory_order_acquire)) {
                return;
            }
            while (m_locked.load(std::memory_order_relaxed)) {
                if (spins < 64) {
                    FASTSHELL_CPU_RELAX();
                    ++spins;
                } else {
                    m_locked.wait(true, std::memory_order_relaxed);
                }
            }
        }
    }

    bool try_lock() noexcept {
        return !m_locked.load(std::memory_order_relaxed)
            && !m_locked.exchange(true, std::memory_order_acquire);
    }

    void unlock() noexcept {
        m_locked.store(false, std::memory_order_release);
        m_locked.notify_one();
    }

private:
    std::atomic<bool> m_locked{false};
};
