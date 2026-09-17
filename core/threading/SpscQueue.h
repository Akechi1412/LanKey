#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>

namespace lankey::core::threading {

// Single-producer / single-consumer ring buffer. The producer (hook thread) never blocks
// and never allocates: a full queue drops the event and the caller counts it. Capacity must
// be a power of two.
template <class T, std::size_t Capacity>
class SpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    // Producer side. Returns false (and leaves `item` untouched) when full.
    template <class U>
    [[nodiscard]] bool tryPush(U&& item) noexcept(std::is_nothrow_constructible_v<T, U&&>) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & kMask;
        if (next == tail_.load(std::memory_order_acquire)) return false;
        slots_[head] = std::forward<U>(item);
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer side.
    [[nodiscard]] std::optional<T> tryPop() {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return std::nullopt;
        std::optional<T> out(std::move(slots_[tail]));
        tail_.store((tail + 1) & kMask, std::memory_order_release);
        return out;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

private:
    static constexpr std::size_t kMask = Capacity - 1;
    std::array<T, Capacity> slots_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

} // namespace lankey::core::threading
