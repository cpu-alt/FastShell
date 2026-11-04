// Copyright 2020 WindTerm, 2026 FastShell contributors. Apache-2.0.
//
// Fixed-capacity ring buffer addressed by an ever-growing absolute index.
// Old entries are overwritten once the capacity is reached.
//
// Changes from the original: stores real T values (the original always
// allocated qint64), rounds capacity correctly with std::bit_ceil, works with
// non-trivial types, and adds size()/back()/popBack().
#pragma once

#include <bit>
#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>

template <typename T>
class CircularBuffer {
public:
    explicit CircularBuffer(std::size_t capacity)
        : m_capacity(std::bit_ceil(capacity < 1 ? std::size_t{1} : capacity))
        , m_mask(m_capacity - 1)
        , m_buffer(std::make_unique<T[]>(m_capacity)) {}

    CircularBuffer(const CircularBuffer &) = delete;
    CircularBuffer &operator=(const CircularBuffer &) = delete;
    CircularBuffer(CircularBuffer &&) noexcept = default;
    CircularBuffer &operator=(CircularBuffer &&) noexcept = default;

    // Appends a value and returns its absolute index.
    std::int64_t append(T value) {
        ++m_currentIndex;
        if (m_currentIndex - m_minIndex >= static_cast<std::int64_t>(m_capacity)) {
            ++m_minIndex;  // oldest entry is overwritten
        }
        m_buffer[static_cast<std::size_t>(m_currentIndex) & m_mask] = std::move(value);
        return m_currentIndex;
    }

    // Removes the newest value (if any) and returns it.
    T popBack() {
        assert(!empty());
        T value = std::move(m_buffer[static_cast<std::size_t>(m_currentIndex) & m_mask]);
        --m_currentIndex;
        return value;
    }

    const T &at(std::int64_t index) const {
        assert(contains(index));
        return m_buffer[static_cast<std::size_t>(index) & m_mask];
    }
    T &at(std::int64_t index) {
        assert(contains(index));
        return m_buffer[static_cast<std::size_t>(index) & m_mask];
    }
    const T &operator[](std::int64_t index) const { return at(index); }
    T &operator[](std::int64_t index) { return at(index); }

    const T &back() const { return at(m_currentIndex); }

    void clear() {
        m_currentIndex = -1;
        m_minIndex = 0;
    }

    bool contains(std::int64_t index) const { return index >= minIndex() && index <= m_currentIndex; }
    std::size_t capacity() const { return m_capacity; }
    std::int64_t currentIndex() const { return m_currentIndex; }
    std::int64_t minIndex() const { return m_minIndex; }
    std::size_t size() const { return static_cast<std::size_t>(m_currentIndex + 1 - minIndex()); }
    bool empty() const { return m_currentIndex < minIndex(); }

private:
    std::size_t m_capacity;
    std::size_t m_mask;
    std::unique_ptr<T[]> m_buffer;
    std::int64_t m_currentIndex = -1;
    std::int64_t m_minIndex = 0;  // popBack() must not resurrect overwritten slots
};
