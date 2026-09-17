#pragma once

#include <atomic>
#include <cstdint>

#include "core/interfaces/ITextSink.h"

#include "platform/win32/Win32.h"

namespace lankey::platform::win32 {

// ITextSink via SendInput. Deletes with VK_BACK presses, inserts with KEYEVENTF_UNICODE
// (layout-independent, handles surrogate pairs), and stamps every event with kLanKeyMagic
// so the hook lets them through.
//
// Two strategies: one SendInput call for the whole replacement (fast, minimal flicker) or
// one call per key event with a tiny pause (for applications that lose batched input:
// some games, old Java/Electron, remote desktops).
//
// Threading: called on the hook thread, from inside the hook callback. That is allowed and
// is what every Vietnamese IME on Windows does; the injected events re-enter the hook and
// are recognised by their magic.
class InputSender final : public core::ITextSink {
public:
    enum class Strategy { Batch, KeyByKey };

    void apply(const core::model::TextReplacement& replacement) override;

    void setStrategy(Strategy s) noexcept { strategy_.store(s); }
    [[nodiscard]] Strategy strategy() const noexcept { return strategy_.load(); }
    [[nodiscard]] std::uint64_t eventsSent() const noexcept { return eventsSent_.load(); }

private:
    std::atomic<Strategy> strategy_{Strategy::Batch};
    std::atomic<std::uint64_t> eventsSent_{0};
};

} // namespace lankey::platform::win32
