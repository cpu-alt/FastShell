// Copyright 2020 WindTerm, 2026 WindTerm NG contributors. Apache-2.0.
//
// Runs a callable when the guard goes out of scope, unless cancelled.
// Templated on the callable, so there is no std::function allocation.
#pragma once

#include <type_traits>
#include <utility>

template <typename F>
class [[nodiscard]] ScopeGuard {
public:
    explicit ScopeGuard(F release) noexcept(std::is_nothrow_move_constructible_v<F>)
        : m_release(std::move(release)) {}

    // Compatible with the original two-callable form: acquire runs immediately.
    template <typename A>
    ScopeGuard(A &&acquire, F release) : m_release(std::move(release)) {
        std::forward<A>(acquire)();
    }

    ScopeGuard(ScopeGuard &&other) noexcept(std::is_nothrow_move_constructible_v<F>)
        : m_release(std::move(other.m_release)), m_active(std::exchange(other.m_active, false)) {}

    ScopeGuard(const ScopeGuard &) = delete;
    ScopeGuard &operator=(const ScopeGuard &) = delete;
    ScopeGuard &operator=(ScopeGuard &&) = delete;

    ~ScopeGuard() {
        if (m_active) {
            m_release();
        }
    }

    void cancel() noexcept { m_active = false; }

private:
    F m_release;
    bool m_active = true;
};

template <typename A, typename F>
ScopeGuard(A &&, F) -> ScopeGuard<F>;
