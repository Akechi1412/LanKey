#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "core/interfaces/IFocusObserver.h"
#include "core/threading/TaskThread.h"

#include "platform/win32/Win32.h"

namespace lankey::platform::win32 {

// IFocusObserver via SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_OBJECT_FOCUS).
//
// The executable name is resolved immediately on the event (cheap, cached per process id)
// and published together with the change notification. Whether the focused control is a
// password field is resolved on a private thread through UI Automation (slow, tens of ms)
// and folded into the snapshot silently: the pipeline reads current() on every key, so it
// sees the answer without another context reset.
//
// Threading: install() must be called on a thread that pumps messages (the UI thread) and
// the WinEvent callbacks arrive on that thread. current() is safe from the hook thread.
class FocusWatcher final : public core::IFocusObserver {
public:
    FocusWatcher();
    ~FocusWatcher() override;

    FocusWatcher(const FocusWatcher&) = delete;
    FocusWatcher& operator=(const FocusWatcher&) = delete;

    void install();
    void uninstall();

    [[nodiscard]] std::shared_ptr<const core::model::FocusContext> current() const override;
    void onChange(ChangeHandler handler) override;

    // Resolve the current foreground state now (at startup, before any event arrives).
    void refresh();

private:
    static void CALLBACK winEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject,
                                      LONG idChild, DWORD thread, DWORD time);
    void onFocusEvent(HWND hwnd);
    void publish(core::model::FocusContext ctx, bool notify);
    void resolvePasswordAsync(HWND hwnd, std::uint64_t generation);
    [[nodiscard]] static std::string processImageName(HWND hwnd, DWORD& processIdOut);

    static FocusWatcher* instance_;

    HWINEVENTHOOK foregroundHook_ = nullptr;
    HWINEVENTHOOK focusHook_ = nullptr;
    ChangeHandler handler_;
    std::shared_ptr<const core::model::FocusContext> current_;
    DWORD cachedProcessId_ = 0;
    std::string cachedAppName_;
    std::atomic<std::uint64_t> generation_{0};
    core::threading::TaskThread resolver_{"lankey-focus"};
};

} // namespace lankey::platform::win32
