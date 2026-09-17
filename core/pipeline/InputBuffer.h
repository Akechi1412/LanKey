#pragma once

#include <atomic>
#include <cstdint>

#include "core/model/ComposedText.h"

namespace lankey::core::pipeline {

// The hook thread's view of "what is being typed right now" plus the generation counter
// that every asynchronous TextReplacement is checked against.
//
// Threading: written on the hook thread only. `generation()` may be read from any thread
// (workers stamp it into the commands they produce); everything else is hook-thread only.
class InputBuffer {
public:
    // Called for every event that can change what is on screen or where the caret is:
    // each key down, reset, focus change, mouse click. Returns the new generation.
    std::uint64_t bump() noexcept {
        return generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
    }

    [[nodiscard]] std::uint64_t generation() const noexcept {
        return generation_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool matches(std::uint64_t expected) const noexcept {
        return expected == generation();
    }

    // Composition of the syllable being typed, as reported by the engine after the last key.
    [[nodiscard]] const model::ComposedText& composed() const noexcept { return composed_; }
    void setComposed(model::ComposedText composed) { composed_ = std::move(composed); }
    void clearComposed() noexcept {
        composed_.text.clear();
        composed_.vietnameseTransformApplied = false;
    }

private:
    std::atomic<std::uint64_t> generation_{0};
    model::ComposedText composed_;
};

} // namespace lankey::core::pipeline
