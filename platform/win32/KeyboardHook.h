#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

#include "core/interfaces/IKeySource.h"

#include "platform/win32/Win32.h"

namespace lankey::platform::win32 {

// IKeySource on a WH_KEYBOARD_LL hook (plus WH_MOUSE_LL for click detection).
//
// The hooks are installed from a dedicated thread with its own message loop: a low-level
// hook callback runs on the thread that installed it, and if that were the UI thread a
// slow paint would stall every keystroke in the system. The callback itself is wrapped
// noexcept and must stay far below the LowLevelHooksTimeout (~300 ms) or Windows silently
// unhooks us - hence the watchdog, which reinstalls the hooks when input keeps arriving
// but the callbacks have gone quiet.
//
// Only one instance may exist per process (the hook procedure is static).
class KeyboardHook final : public core::IKeySource {
public:
    using PointerHandler = std::function<void()>;

    struct Stats {
        std::atomic<std::uint64_t> keyCallbacks{0};
        std::atomic<std::uint64_t> swallowed{0};
        std::atomic<std::uint64_t> reinstalls{0};
        std::atomic<std::uint64_t> exceptions{0};
        std::atomic<std::uint32_t> maxCallbackMicros{0};
    };

    KeyboardHook();
    ~KeyboardHook() override;

    KeyboardHook(const KeyboardHook&) = delete;
    KeyboardHook& operator=(const KeyboardHook&) = delete;

    void setHandler(Handler handler) override;
    // Mouse button down anywhere: the caret moved, the composition is over.
    void setPointerHandler(PointerHandler handler);

    void start() override;
    void stop() override;

    // Run `task` on the hook thread, between hook callbacks. This is how the rest of the
    // app touches InputPipeline (focus changes, settings changes) without a data race: the
    // pipeline is single-threaded by construction, and this is its thread.
    void post(std::function<void()> task);

    [[nodiscard]] bool running() const noexcept { return threadId_.load() != 0; }
    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

private:
    static LRESULT CALLBACK keyboardProc(int code, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK mouseProc(int code, WPARAM wParam, LPARAM lParam);

    void hookThreadMain();
    void watchdogMain();
    bool installHooks();
    void removeHooks();
    [[nodiscard]] bool onKey(WPARAM message, const KBDLLHOOKSTRUCT& k) noexcept;
    [[nodiscard]] core::model::KeyEvent translate(WPARAM message, const KBDLLHOOKSTRUCT& k) const;

    static KeyboardHook* instance_;

    Handler handler_;
    PointerHandler pointerHandler_;
    std::thread hookThread_;
    std::thread watchdog_;
    std::atomic<DWORD> threadId_{0};
    std::atomic<bool> stopping_{false};
    HHOOK keyboardHook_ = nullptr;
    HHOOK mouseHook_ = nullptr;
    std::atomic<ULONGLONG> lastKeyboardTick_{0};
    std::atomic<ULONGLONG> lastMouseTick_{0};
    std::mutex tasksMutex_;
    std::deque<std::function<void()>> tasks_;
    Stats stats_;
};

} // namespace lankey::platform::win32
