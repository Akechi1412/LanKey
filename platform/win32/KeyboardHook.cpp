#include "platform/win32/KeyboardHook.h"

#include <chrono>
#include <utility>

namespace lankey::platform::win32 {

using core::model::KeyEvent;
using core::model::Modifier;
using core::model::VirtualKey;

namespace {

constexpr UINT kMsgReinstall = WM_APP + 1;
constexpr UINT kMsgTask = WM_APP + 2;
constexpr int kWatchdogPeriodMs = 5000;
// Input seen by the OS but by neither hook for this long = the hooks are gone.
constexpr ULONGLONG kDeadAfterMs = 10000;

bool isDown(int vk) noexcept {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

VirtualKey mapVirtualKey(DWORD vk, char32_t unicode) noexcept {
    if (vk >= 'A' && vk <= 'Z') return static_cast<VirtualKey>(vk);
    if (vk >= '0' && vk <= '9') return static_cast<VirtualKey>(vk);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        return static_cast<VirtualKey>('0' + (vk - VK_NUMPAD0));
    }
    switch (vk) {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
        return VirtualKey::Shift;
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
        return VirtualKey::Control;
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
        return VirtualKey::Alt;
    case VK_BACK:
        return VirtualKey::Backspace;
    case VK_TAB:
        return VirtualKey::Tab;
    case VK_RETURN:
        return VirtualKey::Enter;
    case VK_ESCAPE:
        return VirtualKey::Escape;
    case VK_SPACE:
        return VirtualKey::Space;
    case VK_END:
        return VirtualKey::End;
    case VK_HOME:
        return VirtualKey::Home;
    case VK_LEFT:
        return VirtualKey::ArrowLeft;
    case VK_UP:
        return VirtualKey::ArrowUp;
    case VK_RIGHT:
        return VirtualKey::ArrowRight;
    case VK_DOWN:
        return VirtualKey::ArrowDown;
    case VK_DELETE:
        return VirtualKey::Delete;
    default:
        break;
    }
    // Anything else that produces a printable character is punctuation to the pipeline.
    return unicode >= 0x20 ? VirtualKey::Punctuation : VirtualKey::Unknown;
}

} // namespace

KeyboardHook* KeyboardHook::instance_ = nullptr;

KeyboardHook::KeyboardHook() {
    // A second instance would steal the static hook procedure; fail loudly in debug.
    instance_ = this;
}

KeyboardHook::~KeyboardHook() {
    stop();
    if (instance_ == this) instance_ = nullptr;
}

void KeyboardHook::setHandler(Handler handler) {
    handler_ = std::move(handler);
}

void KeyboardHook::setPointerHandler(PointerHandler handler) {
    pointerHandler_ = std::move(handler);
}

void KeyboardHook::start() {
    if (hookThread_.joinable()) return;
    stopping_.store(false);
    hookThread_ = std::thread([this] { hookThreadMain(); });
    // Wait until the thread has installed the hooks (or given up) so callers can rely on
    // keys being captured as soon as start() returns.
    while (threadId_.load() == 0 && !stopping_.load())
        Sleep(1);
    watchdog_ = std::thread([this] { watchdogMain(); });
}

void KeyboardHook::stop() {
    if (!hookThread_.joinable()) return;
    stopping_.store(true);
    if (const DWORD tid = threadId_.load(); tid != 0) PostThreadMessageW(tid, WM_QUIT, 0, 0);
    hookThread_.join();
    if (watchdog_.joinable()) watchdog_.join();
    threadId_.store(0);
}

void KeyboardHook::post(std::function<void()> task) {
    {
        const std::lock_guard lock(tasksMutex_);
        tasks_.push_back(std::move(task));
    }
    if (const DWORD tid = threadId_.load(); tid != 0) {
        PostThreadMessageW(tid, kMsgTask, 0, 0);
    }
}

bool KeyboardHook::installHooks() {
    removeHooks();
    const HINSTANCE self = GetModuleHandleW(nullptr);
    keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, &KeyboardHook::keyboardProc, self, 0);
    mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, &KeyboardHook::mouseProc, self, 0);
    const ULONGLONG now = GetTickCount64();
    lastKeyboardTick_.store(now);
    lastMouseTick_.store(now);
    // Whatever was held when the old hook stopped receiving keys, its release was missed.
    trackedModifiers_ = Modifier::None;
    return keyboardHook_ != nullptr;
}

void KeyboardHook::removeHooks() {
    if (keyboardHook_ != nullptr) UnhookWindowsHookEx(keyboardHook_);
    if (mouseHook_ != nullptr) UnhookWindowsHookEx(mouseHook_);
    keyboardHook_ = nullptr;
    mouseHook_ = nullptr;
}

void KeyboardHook::hookThreadMain() {
    // Keystrokes must never wait behind a busy UI or worker thread.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    // Force the creation of this thread's message queue before publishing the id, so a
    // PostThreadMessage from stop() cannot be lost.
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    if (!installHooks()) {
        stopping_.store(true);
        return;
    }
    threadId_.store(GetCurrentThreadId());

    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == kMsgReinstall) {
            installHooks();
            stats_.reinstalls.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        if (msg.message == kMsgTask) {
            std::deque<std::function<void()>> batch;
            {
                const std::lock_guard lock(tasksMutex_);
                batch.swap(tasks_);
            }
            for (auto& task : batch) {
                try {
                    task();
                } catch (...) {
                    stats_.exceptions.fetch_add(1, std::memory_order_relaxed);
                }
            }
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    removeHooks();
}

void KeyboardHook::watchdogMain() {
    while (!stopping_.load()) {
        for (int i = 0; i < kWatchdogPeriodMs / 100 && !stopping_.load(); ++i)
            Sleep(100);
        if (stopping_.load()) break;

        // Input typed on another desktop - the lock screen, a UAC prompt - advances the
        // system's idle timer but is invisible to a hook on ours. Treating that as a dead
        // hook reinstalled it for no reason (31 times in 4.4 h of ordinary use, measured
        // 2026-09-24, on a machine locked with Win+L whenever its owner stepped away).
        if (const HDESK input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS); input != nullptr) {
            CloseDesktop(input);
        } else {
            // Not our desktop. Start the clock again so the time spent away is not
            // counted as silence from the hook.
            const ULONGLONG now = GetTickCount64();
            lastKeyboardTick_.store(now);
            lastMouseTick_.store(now);
            continue;
        }

        LASTINPUTINFO info{};
        info.cbSize = sizeof(info);
        if (!GetLastInputInfo(&info)) continue;
        // GetLastInputInfo is a 32-bit tick; compare in the same domain.
        const DWORD lastInput = info.dwTime;
        const DWORD lastSeen =
            static_cast<DWORD>((std::max)(lastKeyboardTick_.load(), lastMouseTick_.load()));
        if (lastInput > lastSeen && (lastInput - lastSeen) > kDeadAfterMs) {
            stats_.lastDeadGapMs.store(lastInput - lastSeen, std::memory_order_relaxed);
            if (const DWORD tid = threadId_.load(); tid != 0) {
                PostThreadMessageW(tid, kMsgReinstall, 0, 0);
            }
        }
    }
}

LRESULT CALLBACK KeyboardHook::keyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    KeyboardHook* self = instance_;
    if (code == HC_ACTION && self != nullptr && lParam != 0) {
        const auto& k = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        if (self->onKey(wParam, k)) return 1;
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT CALLBACK KeyboardHook::mouseProc(int code, WPARAM wParam, LPARAM lParam) {
    KeyboardHook* self = instance_;
    if (code == HC_ACTION && self != nullptr) {
        self->lastMouseTick_.store(GetTickCount64(), std::memory_order_relaxed);
        switch (wParam) {
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN:
            if (self->pointerHandler_) {
                try {
                    self->pointerHandler_();
                } catch (...) {
                    self->stats_.exceptions.fetch_add(1, std::memory_order_relaxed);
                }
            }
            break;
        default:
            break;
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

bool KeyboardHook::onKey(WPARAM message, const KBDLLHOOKSTRUCT& k) noexcept {
    lastKeyboardTick_.store(GetTickCount64(), std::memory_order_relaxed);
    stats_.keyCallbacks.fetch_add(1, std::memory_order_relaxed);
    if (!handler_) return false;

    const auto started = std::chrono::steady_clock::now();
    bool swallow = false;
    try {
        swallow = handler_(translate(message, k));
    } catch (...) {
        // The handler is noexcept by contract; this is belt and braces. Let the key through.
        stats_.exceptions.fetch_add(1, std::memory_order_relaxed);
        swallow = false;
    }
    const auto micros =
        static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                       std::chrono::steady_clock::now() - started)
                                       .count());
    std::uint32_t prev = stats_.maxCallbackMicros.load(std::memory_order_relaxed);
    while (micros > prev && !stats_.maxCallbackMicros.compare_exchange_weak(
                                prev, micros, std::memory_order_relaxed)) {
    }
    if (swallow) stats_.swallowed.fetch_add(1, std::memory_order_relaxed);
    return swallow;
}

// Which modifier a virtual key belongs to, or None.
Modifier KeyboardHook::modifierFor(DWORD vk) noexcept {
    switch (vk) {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
        return Modifier::Shift;
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
        return Modifier::Control;
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
        return Modifier::Alt;
    case VK_LWIN:
    case VK_RWIN:
        return Modifier::Win;
    default:
        return Modifier::None;
    }
}

// Drop anything the tracked set believes is held but the OS reports as up. The key this
// event is about is left alone: on its own key-down the async state has not caught up yet,
// which is the whole reason the tracked set exists.
void KeyboardHook::reconcileModifiers(DWORD vk) noexcept {
    const Modifier own = modifierFor(vk);
    auto bits = static_cast<std::uint8_t>(trackedModifiers_);
    const auto drop = [&](Modifier m, bool physicallyDown) {
        if (m == own || physicallyDown) return;
        bits &= static_cast<std::uint8_t>(~static_cast<std::uint8_t>(m));
    };
    drop(Modifier::Shift, isDown(VK_SHIFT));
    drop(Modifier::Control, isDown(VK_CONTROL));
    drop(Modifier::Alt, isDown(VK_MENU));
    drop(Modifier::Win, isDown(VK_LWIN) || isDown(VK_RWIN));
    trackedModifiers_ = static_cast<Modifier>(bits);
}

void KeyboardHook::trackModifier(WPARAM message, DWORD vk) noexcept {
    Modifier m = Modifier::None;
    switch (vk) {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
        m = Modifier::Shift;
        break;
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
        m = Modifier::Control;
        break;
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
        m = Modifier::Alt;
        break;
    case VK_LWIN:
    case VK_RWIN:
        m = Modifier::Win;
        break;
    default:
        return;
    }
    const bool down = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN);
    const auto bits = static_cast<std::uint8_t>(trackedModifiers_);
    const auto bit = static_cast<std::uint8_t>(m);
    trackedModifiers_ = static_cast<Modifier>(down ? (bits | bit) : (bits & ~bit));
}

KeyEvent KeyboardHook::translate(WPARAM message, const KBDLLHOOKSTRUCT& k) {
    KeyEvent ev;
    ev.isDown = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN);
    ev.timestampMs = static_cast<std::int64_t>(k.time);
    ev.injectedBySelf = (k.flags & LLKHF_INJECTED) != 0 && k.dwExtraInfo == kLanKeyMagic;

    // The tracked set exists only because GetAsyncKeyState lags behind injected input.
    // It must never outlive the real key: a key-up this hook never saw leaves a modifier
    // stuck down, every later key then looks like a shortcut, and LanKey stops composing
    // entirely - typing goes English until that modifier happens to be pressed again.
    //
    // Win+L is the everyday way to hit this: both keys go down on our desktop and come up
    // on the lock screen, where no hook of ours can see them.
    trackModifier(message, k.vkCode);
    reconcileModifiers(k.vkCode);
    const auto tracked = [this](Modifier m) { return has(trackedModifiers_, m); };
    if (tracked(Modifier::Shift) || isDown(VK_SHIFT)) ev.modifiers = ev.modifiers | Modifier::Shift;
    if (tracked(Modifier::Control) || isDown(VK_CONTROL)) {
        ev.modifiers = ev.modifiers | Modifier::Control;
    }
    if (tracked(Modifier::Alt) || isDown(VK_MENU)) ev.modifiers = ev.modifiers | Modifier::Alt;
    if (tracked(Modifier::Win) || isDown(VK_LWIN) || isDown(VK_RWIN)) {
        ev.modifiers = ev.modifiers | Modifier::Win;
    }
    if ((GetKeyState(VK_CAPITAL) & 1) != 0) ev.modifiers = ev.modifiers | Modifier::CapsLock;

    if (!ev.isDown) {
        // Key-ups only matter for their identity (hotkey chords, swallowed releases); skip
        // the layout lookup and ToUnicodeEx - half of all callbacks.
        ev.key = mapVirtualKey(k.vkCode, 0);
        return ev;
    }

    // Translate to a character with a COPY of the keyboard state. Flag bit 2 (0x4) tells
    // ToUnicodeEx not to touch the kernel's dead-key state (Windows 10 1607+); without it,
    // merely looking at a key would eat pending dead keys in layouts that have them.
    BYTE state[256] = {};
    if (has(ev.modifiers, Modifier::Shift)) state[VK_SHIFT] = 0x80;
    if (has(ev.modifiers, Modifier::CapsLock)) state[VK_CAPITAL] = 0x01;
    // Ctrl/Alt are deliberately NOT set: with them ToUnicodeEx yields control characters,
    // and the pipeline ignores system-modifier chords anyway (AltGr layouts are P2).
    wchar_t buffer[8] = {};
    const HWND foreground = GetForegroundWindow();
    const HKL layout = GetKeyboardLayout(
        foreground != nullptr ? GetWindowThreadProcessId(foreground, nullptr) : 0);
    const int n = ToUnicodeEx(k.vkCode, k.scanCode, state, buffer, 8, 0x4, layout);
    if (n == 1) {
        ev.unicode = static_cast<char32_t>(buffer[0]);
    } else if (n == 2) {
        ev.unicode = fromUtf16(std::wstring_view(buffer, 2))[0];
    }
    // Control characters from Enter/Tab/Esc/Backspace are not "typed text".
    if (ev.unicode < 0x20) ev.unicode = 0;

    ev.key = mapVirtualKey(k.vkCode, ev.unicode);
    return ev;
}

} // namespace lankey::platform::win32
