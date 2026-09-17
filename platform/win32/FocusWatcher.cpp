#include "platform/win32/FocusWatcher.h"

#include <algorithm>
#include <cctype>
#include <objbase.h>
#include <uiautomation.h>
#include <utility>

namespace lankey::platform::win32 {

using core::model::FocusContext;

FocusWatcher* FocusWatcher::instance_ = nullptr;

namespace {

// UI Automation client, created lazily on the resolver thread (one COM apartment there).
class UiaClient {
public:
    UiaClient() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation,
                         reinterpret_cast<void**>(&automation_));
    }
    ~UiaClient() {
        if (automation_ != nullptr) automation_->Release();
        CoUninitialize();
    }
    UiaClient(const UiaClient&) = delete;
    UiaClient& operator=(const UiaClient&) = delete;

    // True only when UIA positively says the focused element is a password control.
    [[nodiscard]] bool focusedIsPassword() const {
        if (automation_ == nullptr) return false;
        IUIAutomationElement* element = nullptr;
        if (FAILED(automation_->GetFocusedElement(&element)) || element == nullptr) return false;
        BOOL isPassword = FALSE;
        const HRESULT hr = element->get_CurrentIsPassword(&isPassword);
        element->Release();
        return SUCCEEDED(hr) && isPassword != FALSE;
    }

private:
    IUIAutomation* automation_ = nullptr;
};

// Cheap fallback for classic Win32 edit controls (ES_PASSWORD), no COM involved.
bool focusedEditIsPassword(HWND foreground) {
    if (foreground == nullptr) return false;
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    if (!GetGUIThreadInfo(GetWindowThreadProcessId(foreground, nullptr), &info)) return false;
    if (info.hwndFocus == nullptr) return false;
    wchar_t cls[32] = {};
    GetClassNameW(info.hwndFocus, cls, 32);
    if (_wcsicmp(cls, L"Edit") != 0) return false;
    return (GetWindowLongPtrW(info.hwndFocus, GWL_STYLE) & ES_PASSWORD) != 0;
}

} // namespace

FocusWatcher::FocusWatcher() : current_(std::make_shared<const FocusContext>()) {
    instance_ = this;
}

FocusWatcher::~FocusWatcher() {
    uninstall();
    resolver_.stop();
    if (instance_ == this) instance_ = nullptr;
}

void FocusWatcher::install() {
    if (foregroundHook_ != nullptr) return;
    resolver_.start();
    foregroundHook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                                      &FocusWatcher::winEventProc, 0, 0,
                                      WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    focusHook_ = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS, nullptr,
                                 &FocusWatcher::winEventProc, 0, 0,
                                 WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    refresh();
}

void FocusWatcher::uninstall() {
    if (foregroundHook_ != nullptr) UnhookWinEvent(foregroundHook_);
    if (focusHook_ != nullptr) UnhookWinEvent(focusHook_);
    foregroundHook_ = nullptr;
    focusHook_ = nullptr;
}

std::shared_ptr<const FocusContext> FocusWatcher::current() const {
    return std::atomic_load(&current_);
}

void FocusWatcher::onChange(ChangeHandler handler) {
    handler_ = std::move(handler);
}

void FocusWatcher::refresh() {
    onFocusEvent(GetForegroundWindow());
}

void CALLBACK FocusWatcher::winEventProc(HWINEVENTHOOK /*hook*/, DWORD event, HWND hwnd,
                                         LONG idObject, LONG /*idChild*/, DWORD /*thread*/,
                                         DWORD /*time*/) {
    FocusWatcher* self = instance_;
    if (self == nullptr) return;
    if (event == EVENT_OBJECT_FOCUS && idObject != OBJID_WINDOW && idObject != OBJID_CLIENT) return;
    // Focus events name the control; the application is whatever owns the foreground.
    self->onFocusEvent(GetForegroundWindow() != nullptr ? GetForegroundWindow() : hwnd);
}

std::string FocusWatcher::processImageName(HWND hwnd, DWORD& processIdOut) {
    processIdOut = 0;
    if (hwnd == nullptr) return {};
    GetWindowThreadProcessId(hwnd, &processIdOut);
    if (processIdOut == 0) return {};
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processIdOut);
    if (process == nullptr) return {};
    wchar_t path[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    std::string name;
    if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        std::wstring full(path, size);
        const auto slash = full.find_last_of(L"\\/");
        name = toUtf8(slash == std::wstring::npos ? full : full.substr(slash + 1));
        std::ranges::transform(name, name.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }
    CloseHandle(process);
    return name;
}

void FocusWatcher::onFocusEvent(HWND hwnd) {
    DWORD pid = 0;
    // Resolving the image name needs OpenProcess; cache per process id since focus events
    // between controls of one window are frequent.
    std::string app;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != 0 && pid == cachedProcessId_) {
        app = cachedAppName_;
    } else {
        app = processImageName(hwnd, pid);
        cachedProcessId_ = pid;
        cachedAppName_ = app;
    }

    FocusContext ctx;
    ctx.appName = app;
    ctx.windowId = reinterpret_cast<std::uintptr_t>(hwnd);
    ctx.isPasswordField = focusedEditIsPassword(hwnd); // instant answer for classic edits

    const auto previous = current();
    const bool changed = previous == nullptr || previous->windowId != ctx.windowId ||
                         previous->appName != ctx.appName;
    publish(ctx, changed);
    resolvePasswordAsync(hwnd, generation_.fetch_add(1) + 1);
}

void FocusWatcher::publish(FocusContext ctx, bool notify) {
    auto snapshot = std::make_shared<const FocusContext>(std::move(ctx));
    std::atomic_store(&current_, snapshot);
    if (notify && handler_) handler_(*snapshot);
}

void FocusWatcher::resolvePasswordAsync(HWND hwnd, std::uint64_t generation) {
    resolver_.post([this, hwnd, generation] {
        static thread_local UiaClient uia; // one COM init per resolver thread
        const bool isPassword = uia.focusedIsPassword();
        // Only the newest request may write: focus may have moved on while UIA was busy.
        if (generation_.load() != generation) return;
        const auto cur = current();
        if (cur == nullptr || cur->windowId != reinterpret_cast<std::uintptr_t>(hwnd)) return;
        if (cur->isPasswordField == isPassword) return;
        FocusContext updated = *cur;
        updated.isPasswordField = updated.isPasswordField || isPassword;
        publish(std::move(updated), /*notify=*/false);
    });
}

} // namespace lankey::platform::win32
