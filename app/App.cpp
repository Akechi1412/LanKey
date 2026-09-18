#include "app/App.h"

#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <utility>

#include "app/Log.h"

namespace lankey::app {

using core::model::FocusContext;
using core::model::InputMethod;
using core::model::KeyEvent;
using core::model::Settings;
using core::pipeline::InputPipeline;
using core::pipeline::PopupState;

namespace {

constexpr wchar_t kUiClassName[] = L"LanKeyUiWindow";
constexpr UINT kMsgPopup = WM_APP + 20;        // lParam = PopupState* (owned by receiver)
constexpr UINT kMsgPopupPending = WM_APP + 23; // lParam = generation (no payload copy)
constexpr UINT kMsgLanguage = WM_APP + 21;     // wParam = enabled
constexpr UINT kMsgEraseDone = WM_APP + 22;    // wParam = success
constexpr UINT_PTR kIdleTimer = 1;
constexpr UINT_PTR kNoticeTimer = 2;

std::filesystem::path appDataDir() {
    PWSTR path = nullptr;
    std::filesystem::path dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path))) {
        dir = std::filesystem::path(path) / L"LanKey";
    } else {
        dir = std::filesystem::temp_directory_path() / L"LanKey";
    }
    if (path != nullptr) CoTaskMemFree(path);
    return dir;
}

} // namespace

App::App() = default;

App::~App() {
    shutdown();
}

int App::run(HINSTANCE instance) {
    if (!initialise(instance)) {
        shutdown();
        return 1;
    }
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    shutdown();
    return static_cast<int>(msg.wParam);
}

bool App::initialise(HINSTANCE instance) {
    // One LanKey per session: two hooks would fight over every key.
    singleInstance_ = CreateMutexW(nullptr, TRUE, L"Local\\LanKey.SingleInstance");
    if (singleInstance_ != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"LanKey đang chạy rồi (xem biểu tượng ở khay hệ thống).", L"LanKey",
                    MB_OK | MB_ICONINFORMATION);
        return false;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // UI Automation (caret lookup) runs on this thread and needs a COM apartment.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    dataDir_ = appDataDir();
    std::error_code ec;
    std::filesystem::create_directories(dataDir_, ec);
    Log::instance().open(dataDir_ / L"lankey.log");
    logf("startup: version 0.1.0-alpha");

    settingsStore_ =
        std::make_unique<core::storage::JsonSettingsStore>((dataDir_ / L"settings.json").string());
    if (auto loaded = settingsStore_->load()) {
        settings_ = *loaded;
    } else {
        logf("settings: %s - using defaults", loaded.error().message.c_str());
    }
    engine_.configure(settings_.engine);

    // The lexicon is sealed with DPAPI (ADR-012): user_lexicon.enc is unreadable outside
    // this Windows account; an older plain user_lexicon.db is imported and removed.
    store_ = std::make_unique<core::storage::SqliteLexiconStore>(
        (dataDir_ / L"user_lexicon.enc").string(), &protector_);
    if (auto opened = store_->open(); !opened) {
        logf("database: %s", opened.error().message.c_str());
        MessageBoxW(nullptr,
                    (L"Không mở được từ điển cá nhân:\n" +
                     platform::win32::fromUtf8(opened.error().message))
                        .c_str(),
                    L"LanKey", MB_OK | MB_ICONERROR);
        return false;
    }

    suggestions_.setSettings(settings_.suggestions);
    corrector_.setSettings(settings_.autoCorrect);
    worker_ = std::make_unique<core::smart::SmartWorker>(
        core::smart::SmartWorker::Dependencies{*store_, suggestions_, corrector_, clock_},
        settings_);
    // Worker thread -> hook thread: the pipeline alone may touch the screen, and it
    // checks the generation before doing so.
    worker_->setCorrectionHandler([this](core::model::Correction c) {
        hook_.post([this, c = std::move(c)] { pipeline_->applyCorrection(c); });
    });

    InputPipeline::Handlers handlers;
    handlers.onCommit = [this](core::model::SyllableCommitted&& c) {
        worker_->onCommit(std::move(c));
    };
    handlers.onSelection = [this](const core::model::Phrase& p) { worker_->onSelection(p); };
    handlers.onCorrectionApplied = [this](const std::u32string& w) {
        worker_->onCorrectionApplied(w);
    };
    handlers.onCorrectionRejected = [this](const std::u32string& w, const std::u32string& c) {
        worker_->onCorrectionRejected(w, c);
    };
    handlers.onPopup = [this](const PopupState& state) {
        // Hook thread -> UI thread. A pending list only needs to start the idle timer, so
        // it travels as a bare generation (no allocation on the hook thread per key); the
        // items are copied only when they are actually going to be drawn.
        if (state.pending) {
            PostMessageW(uiWindow_, kMsgPopupPending, 0, static_cast<LPARAM>(state.generation));
            return;
        }
        auto* copy = new PopupState(state);
        if (!PostMessageW(uiWindow_, kMsgPopup, 0, reinterpret_cast<LPARAM>(copy))) delete copy;
    };
    pipeline_ = std::make_unique<InputPipeline>(
        InputPipeline::Dependencies{engine_, sender_, focus_, clock_, &suggestions_},
        std::move(handlers));
    pipeline_->setVietnameseEnabled(settings_.vietnameseEnabled);

    if (!createUiWindow(instance)) return false;

    ui::win32::TrayIcon::Callbacks tray;
    tray.onToggleVietnamese = [this] { applyVietnameseEnabled(!pipeline_->vietnameseEnabled()); };
    tray.onInputMethod = [this](InputMethod m) { setInputMethod(m); };
    tray.onSuggestionsEnabled = [this](bool on) { setSuggestionsEnabled(on); };
    tray.onAutoCorrectEnabled = [this](bool on) { setAutoCorrectEnabled(on); };
    tray.onEraseAllData = [this] { confirmEraseAllData(); };
    tray.onShowData = [this] { showDataDialog(); };
    tray.onQuit = [] { PostQuitMessage(0); };
    if (!tray_.create(instance, std::move(tray))) {
        logf("tray: creation failed %s", platform::win32::lastErrorMessage(GetLastError()).c_str());
    }
    refreshTray();
    if (!popup_.create(instance)) {
        logf("popup: creation failed %s",
             platform::win32::lastErrorMessage(GetLastError()).c_str());
    }

    // Focus events arrive on this (UI) thread; the pipeline lives on the hook thread.
    focus_.onChange([this](const FocusContext& ctx) {
        hook_.post([this, ctx] { pipeline_->onFocusChanged(ctx); });
    });
    focus_.install();

    worker_->start();

    hook_.setHandler([this](const KeyEvent& key) noexcept -> bool {
        if (toggle_.onKey(key)) {
            const bool enabled = !pipeline_->vietnameseEnabled();
            pipeline_->setVietnameseEnabled(enabled);
            PostMessageW(uiWindow_, kMsgLanguage, enabled ? 1 : 0, 0);
        }
        return pipeline_->onKey(key);
    });
    hook_.setPointerHandler([this] { pipeline_->onPointerClick(); });
    hook_.start();
    if (!hook_.running()) {
        logf("hook: SetWindowsHookEx failed %s",
             platform::win32::lastErrorMessage(GetLastError()).c_str());
        MessageBoxW(nullptr, L"Không cài được hook bàn phím.", L"LanKey", MB_OK | MB_ICONERROR);
        return false;
    }
    logf("startup complete");
    return true;
}

void App::shutdown() {
    // Reverse order: stop taking keys, then stop producing/consuming events, then persist.
    if (hook_.running()) {
        const auto& h = hook_.stats();
        logf("hook stats: callbacks=%llu swallowed=%llu reinstalls=%llu exceptions=%llu "
             "maxCallbackMicros=%u",
             static_cast<unsigned long long>(h.keyCallbacks.load()),
             static_cast<unsigned long long>(h.swallowed.load()),
             static_cast<unsigned long long>(h.reinstalls.load()),
             static_cast<unsigned long long>(h.exceptions.load()), h.maxCallbackMicros.load());
    }
    if (pipeline_) {
        const auto& p = pipeline_->stats();
        logf("pipeline stats: keys=%llu commits=%llu replacements=%llu dropped=%llu "
             "exceptions=%llu suggestionsShown=%llu selected=%llu corrections=%llu "
             "undone=%llu correctionsDropped=%llu retypes=%llu",
             static_cast<unsigned long long>(p.keys), static_cast<unsigned long long>(p.commits),
             static_cast<unsigned long long>(p.replacementsApplied),
             static_cast<unsigned long long>(p.replacementsDroppedByGeneration),
             static_cast<unsigned long long>(p.exceptionsSwallowed),
             static_cast<unsigned long long>(p.suggestionsShown),
             static_cast<unsigned long long>(p.suggestionsSelected),
             static_cast<unsigned long long>(p.correctionsApplied),
             static_cast<unsigned long long>(p.correctionsUndone),
             static_cast<unsigned long long>(p.correctionsDropped),
             static_cast<unsigned long long>(p.retypesDetected));
    }
    if (worker_) {
        const auto& w = worker_->stats();
        logf("worker stats: queued=%llu dropped=%llu processed=%llu privacyRejected=%llu "
             "flushes=%llu rebuilds=%llu storeErrors=%llu correctionsProposed=%llu "
             "manualFixes=%llu rejected=%llu",
             static_cast<unsigned long long>(w.eventsQueued.load()),
             static_cast<unsigned long long>(w.eventsDropped.load()),
             static_cast<unsigned long long>(w.eventsProcessed.load()),
             static_cast<unsigned long long>(w.rejectedByPrivacy.load()),
             static_cast<unsigned long long>(w.flushes.load()),
             static_cast<unsigned long long>(w.rebuilds.load()),
             static_cast<unsigned long long>(w.storeErrors.load()),
             static_cast<unsigned long long>(w.correctionsProposed.load()),
             static_cast<unsigned long long>(w.manualFixesLearned.load()),
             static_cast<unsigned long long>(w.correctionsRejected.load()));
    }
    hook_.stop();
    focus_.uninstall();
    if (worker_) worker_->stop();
    popup_.destroy();
    tray_.destroy();
    if (uiWindow_ != nullptr) {
        DestroyWindow(uiWindow_);
        uiWindow_ = nullptr;
    }
    if (store_) store_->close();
    if (settingsStore_) saveSettings();
    if (singleInstance_ != nullptr) {
        CloseHandle(singleInstance_);
        singleInstance_ = nullptr;
    }
    CoUninitialize();
    logf("shutdown complete");
    Log::instance().close();
}

bool App::createUiWindow(HINSTANCE instance) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = &App::uiWndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kUiClassName;
    RegisterClassW(&wc);
    uiWindow_ = CreateWindowExW(0, kUiClassName, L"LanKey", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                instance, this);
    return uiWindow_ != nullptr;
}

LRESULT CALLBACK App::uiWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        // The member is not set yet while CreateWindowEx is still running, but the handler
        // already needs a valid HWND for DefWindowProc (returning 0 here aborts creation).
        static_cast<App*>(cs->lpCreateParams)->uiWindow_ = hwnd;
    }
    auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) return self->onUiMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT App::onUiMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case kMsgPopup: {
        std::unique_ptr<PopupState> state(reinterpret_cast<PopupState*>(lParam));
        if (state) showPopup(*state);
        return 0;
    }
    case kMsgPopupPending:
        pendingGeneration_ = static_cast<std::uint64_t>(lParam);
        SetTimer(uiWindow_, kIdleTimer, static_cast<UINT>(settings_.suggestions.idleDelayMs),
                 nullptr);
        return 0;
    case WM_TIMER:
        if (wParam == kIdleTimer) onIdleTimer();
        if (wParam == kNoticeTimer) {
            KillTimer(uiWindow_, kNoticeTimer);
            popup_.hideNotice();
        }
        return 0;
    case kMsgLanguage:
        // Ctrl+Shift on the hook thread already flipped the pipeline; mirror it here and
        // tell the user - a toast, not a sound.
        settings_.vietnameseEnabled = wParam != 0;
        refreshTray();
        toast_.show(GetModuleHandleW(nullptr), settings_.vietnameseEnabled);
        saveSettings();
        return 0;
    case WM_CLOSE:
    case WM_QUERYENDSESSION:
    case WM_ENDSESSION:
        // Graceful exit from the outside (logoff, `taskkill` without /F, tooling):
        // shutdown() then flushes learning and saves settings.
        PostQuitMessage(0);
        return msg == WM_QUERYENDSESSION ? TRUE : 0;
    case kMsgEraseDone:
        tray_.showBalloon(L"LanKey", wParam != 0 ? L"Đã xoá toàn bộ dữ liệu đã học."
                                                 : L"Không xoá được dữ liệu - xem lankey.log.");
        return 0;
    default:
        return DefWindowProcW(uiWindow_, msg, wParam, lParam);
    }
}

void App::showPopup(const PopupState& state) {
    if (!state.notice.empty()) {
        KillTimer(uiWindow_, kIdleTimer);
        pendingGeneration_ = 0;
        popup_.showNotice(state.notice, caret_.resolve());
        SetTimer(uiWindow_, kNoticeTimer,
                 static_cast<UINT>(core::model::Thresholds::kCorrectionNoticeMs), nullptr);
        return;
    }
    KillTimer(uiWindow_, kNoticeTimer);
    if (state.items.empty()) {
        // Hide, and drop any idle timer still counting for an older pending list.
        KillTimer(uiWindow_, kIdleTimer);
        pendingGeneration_ = 0;
        popup_.hide();
        return;
    }
    if (state.pending) {
        // Flicker guard: draw nothing until the user pauses. When the timer fires we ask
        // the pipeline (on its own thread) to promote exactly this list; it refuses if
        // anything was typed meanwhile.
        pendingGeneration_ = state.generation;
        SetTimer(uiWindow_, kIdleTimer, static_cast<UINT>(settings_.suggestions.idleDelayMs),
                 nullptr);
        return;
    }
    KillTimer(uiWindow_, kIdleTimer);
    popup_.show(state.items, state.selected, caret_.resolve());
}

void App::onIdleTimer() {
    KillTimer(uiWindow_, kIdleTimer);
    const std::uint64_t generation = pendingGeneration_;
    pendingGeneration_ = 0;
    if (generation == 0) return;
    hook_.post([this, generation] { pipeline_->promotePending(generation); });
}

void App::applyVietnameseEnabled(bool enabled) {
    pipeline_->setVietnameseEnabled(enabled);
    settings_.vietnameseEnabled = enabled;
    refreshTray();
    toast_.show(GetModuleHandleW(nullptr), enabled);
    saveSettings();
}

void App::setInputMethod(InputMethod method) {
    settings_.engine.inputMethod = method;
    const auto engineSettings = settings_.engine;
    // The engine belongs to the hook thread.
    hook_.post([this, engineSettings] {
        engine_.configure(engineSettings);
        pipeline_->onPointerClick(); // drop the half-typed syllable: its rules changed
    });
    refreshTray();
    saveSettings();
}

void App::setSuggestionsEnabled(bool enabled) {
    settings_.suggestions.enabled = enabled;
    worker_->updateSettings(settings_);
    refreshTray();
    saveSettings();
}

void App::setAutoCorrectEnabled(bool enabled) {
    // Off <-> the safe default; the finer levels come with the settings UI (Phase 4).
    settings_.autoCorrect.level =
        enabled ? core::model::AutoCorrectLevel::Cautious : core::model::AutoCorrectLevel::Off;
    worker_->updateSettings(settings_);
    refreshTray();
    saveSettings();
}

namespace {
// DATA-POLICY.md, verbatim (see app/CMakeLists.txt).
constexpr const wchar_t* kDataPolicyLines[] = {
#include "lankey/DataPolicy.inc"
};
} // namespace

void App::showDataDialog() {
    std::wstring text;
    for (const auto* line : kDataPolicyLines)
        text += line;
    ui::win32::DataDialog::Callbacks callbacks;
    callbacks.onOpenFolder = [this] { openDataFolder(); };
    callbacks.onEraseAllData = [this] { confirmEraseAllData(); };
    dataDialog_.show(GetModuleHandleW(nullptr), text, std::move(callbacks));
}

void App::openDataFolder() {
    ShellExecuteW(nullptr, L"open", dataDir_.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void App::confirmEraseAllData() {
    const int answer =
        MessageBoxW(nullptr,
                    L"Xoá toàn bộ cụm từ LanKey đã học từ thói quen gõ của bạn?\n\n"
                    L"Việc này không thể hoàn tác.",
                    L"LanKey", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_TOPMOST);
    if (answer != IDYES) return;
    worker_->eraseAllData([this](lk::expected<void> r) {
        // DB thread -> UI thread.
        if (!r) logf("eraseAll: %s", r.error().message.c_str());
        PostMessageW(uiWindow_, kMsgEraseDone, r.has_value() ? 1 : 0, 0);
    });
}

void App::saveSettings() {
    if (auto r = settingsStore_->save(settings_); !r) {
        logf("settings: save failed: %s", r.error().message.c_str());
    }
}

void App::refreshTray() {
    tray_.setState(settings_.vietnameseEnabled, settings_.engine.inputMethod,
                   settings_.suggestions.enabled,
                   settings_.autoCorrect.level != core::model::AutoCorrectLevel::Off);
}

} // namespace lankey::app
