#pragma once

#include <memory>

namespace lankey::core::model {

// Publish/read pattern for data shared between the hook thread (reader) and the worker
// (writer): the writer builds a complete new T and swaps the pointer; readers keep using
// the old one until they release it. No lock is ever held by the reader.
//
// Implemented with the std::atomic_load/atomic_store overloads for shared_ptr rather than
// std::atomic<std::shared_ptr<T>>, because libc++ (the llvm-mingw toolchain) does not
// provide the latter. The free functions exist on every standard library we build with.
template <class T>
class AtomicSnapshot {
public:
    using Pointer = std::shared_ptr<const T>;

    AtomicSnapshot() = default;
    explicit AtomicSnapshot(Pointer initial) : ptr_(std::move(initial)) {}

    AtomicSnapshot(const AtomicSnapshot&) = delete;
    AtomicSnapshot& operator=(const AtomicSnapshot&) = delete;

    // Reader side (hook thread). May return nullptr before the first store().
    [[nodiscard]] Pointer load() const noexcept { return std::atomic_load(&ptr_); }

    // Writer side (worker thread).
    void store(Pointer next) noexcept { std::atomic_store(&ptr_, std::move(next)); }

private:
    Pointer ptr_;
};

} // namespace lankey::core::model
