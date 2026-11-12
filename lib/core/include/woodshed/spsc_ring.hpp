// SPDX-FileCopyrightText: 2026 kebabroyal
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WOODSHED_SPSC_RING_HPP
#define WOODSHED_SPSC_RING_HPP

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstddef>
#include <memory>

namespace woodshed
{

// Lock-free, one writer thread, one reader thread. Real-time safe.
template <typename T>
class spsc_ring
{
public:
    explicit spsc_ring(std::size_t min_capacity)
        : capacity_{std::bit_ceil(std::max<std::size_t>(min_capacity, 2))},
          mask_{capacity_ - 1},
          data_{std::make_unique<T[]>(capacity_)}
    {
    }

    std::size_t capacity() const { return capacity_; }

    // Writer. Returns items written.
    std::size_t write(const T *items, std::size_t n)
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        n = std::min(n, capacity_ - (head - tail));
        const std::size_t start = head & mask_;
        const std::size_t first = std::min(n, capacity_ - start);
        std::copy_n(items, first, data_.get() + start);
        std::copy_n(items + first, n - first, data_.get());
        head_.store(head + n, std::memory_order_release);
        return n;
    }

    // Reader. Returns items read.
    std::size_t read(T *items, std::size_t n)
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        n = std::min(n, head - tail);
        const std::size_t start = tail & mask_;
        const std::size_t first = std::min(n, capacity_ - start);
        std::copy_n(data_.get() + start, first, items);
        std::copy_n(data_.get(), n - first, items + first);
        tail_.store(tail + n, std::memory_order_release);
        return n;
    }

    // Reader. Drops up to n items.
    std::size_t skip(std::size_t n)
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        n = std::min(n, head_.load(std::memory_order_acquire) - tail);
        tail_.store(tail + n, std::memory_order_release);
        return n;
    }

    // Reader.
    void clear()
    {
        tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release);
    }

    std::size_t readable() const
    {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }

    std::size_t writable() const { return capacity_ - readable(); }

private:
    const std::size_t capacity_;
    const std::size_t mask_;
    std::unique_ptr<T[]> data_;
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

}

#endif
