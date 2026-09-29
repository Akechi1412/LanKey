#include "app/App.h"

#include <atomic>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <shellapi.h>
#include <shlobj.h>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "core/text/Utf.h"
#include "core/text/WidthConverter.h"

#include "app/Log.h"
#include "ui/win32/Theme.h"

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
constexpr UINT kMsgDictionary = WM_APP + 24;   // lParam = vector<DictionaryEntry>* (owned)
constexpr UINT kMsgStats = WM_APP + 25;        // lParam = StatsBundle* (owned)
constexpr UINT kMsgShowSettings = WM_APP + 26; // from a second lankey.exe: open Settings
constexpr UINT kMsgHotkey = WM_APP + 27;       // wParam = HotkeyAction, from the hook thread
constexpr UINT kMsgSettingsFile = WM_APP + 28;
constexpr const wchar_t* kVersion = L"0.1.0-alpha";
constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kRunValue = L"LanKey";

struct StatsBundle {
    ui::win32::SettingsWindow::Stats stats;
    std::vector<ui::win32::SettingsWindow::RecentCorrection> recent;
};
constexpr UINT_PTR kIdleTimer = 1;
constexpr UINT_PTR kNoticeTimer = 2;
// An editor saves by writing a temporary file and renaming it: several notifications per
// save, and the file may be briefly incomplete. Settle before reading it.
constexpr UINT_PTR kSettingsFileTimer = 3;
constexpr UINT_PTR kSnippetInsertTimer = 6;
// Long enough for the window the picker took the focus from to finish activating, short
// enough that the snippet still feels like it appeared on Enter.
constexpr UINT kSnippetSettleMs = 80;
constexpr UINT kSettingsFileSettleMs = 250;

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
        // Settings and its dialogs are plain windows of standard controls: give them the
        // dialog keyboard (Tab between controls, Enter/Esc for the default buttons).
        const HWND root = msg.hwnd != nullptr ? GetAncestor(msg.hwnd, GA_ROOT) : nullptr;
        if (ui::win32::wantsDialogNavigation(root) && IsDialogMessageW(root, &msg)) continue;
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
        // Launching LanKey again means "show me": ask the running instance to open Settings
        // (its UI window is message-only, so it is found under HWND_MESSAGE, not broadcast).
        const HWND running = FindWindowExW(HWND_MESSAGE, nullptr, kUiClassName, nullptr);
        if (running != nullptr) {
            PostMessageW(running, kMsgShowSettings, 0, 0);
        } else {
            MessageBoxW(nullptr, L"LanKey đang chạy rồi (xem biểu tượng ở khay hệ thống).",
                        L"LanKey", MB_OK | MB_ICONINFORMATION);
        }
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
        saveSettings(); // normalise: keys added since the file was written appear in it
    } else {
        logf("settings: %s - using defaults", loaded.error().message.c_str());
    }
    engine_.configure(settings_.engine);
    lastSavedSettings_ = core::storage::JsonSettingsStore::serialize(settings_);

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
        InputPipeline::Dependencies{engine_, sender_, focus_, clock_, &suggestions_, &snippets_},
        std::move(handlers));
    pipeline_->setVietnameseEnabled(settings_.vietnameseEnabled);

    if (!createUiWindow(instance)) return false;
    // Edit settings.json in an editor and save: LanKey applies it without being asked.
    // Started after the UI window exists - that is where the notification is delivered.
    settingsWatcher_.start(dataDir_, L"settings.json",
                           [this] { PostMessageW(uiWindow_, kMsgSettingsFile, 0, 0); });
    // The glossary and the snippets live in settings.json now, so the watcher that is
    // already running on that file covers them too - one file, one watcher, one save.
    if (settings_.schemaVersion < core::model::Settings::kSchemaVersion) {
        if (settings_.glossary.empty() && settings_.snippets.items.empty()) importLegacyFiles();
        if (settings_.glossary.empty() && settings_.snippets.items.empty()) seedStarterContent();
        settings_.schemaVersion = core::model::Settings::kSchemaVersion;
        saveSettings();
    }
    applyGlossary();
    applySnippets();

    ui::win32::TrayIcon::Callbacks tray;
    tray.onToggleVietnamese = [this] { applyVietnameseEnabled(!pipeline_->vietnameseEnabled()); };
    tray.onInputMethod = [this](InputMethod m) { setInputMethod(m); };
    tray.onSuggestionsEnabled = [this](bool on) { setSuggestionsEnabled(on); };
    tray.onAutoCorrectEnabled = [this](bool on) { setAutoCorrectEnabled(on); };
    tray.onEraseAllData = [this] { confirmEraseAllData(); };
    tray.onShowData = [this] { showDataDialog(); };
    tray.onSettings = [this] { showSettings(); };
    tray.onUndoToggle = [this] {
        applyVietnameseEnabled(!pipeline_->vietnameseEnabled(), /*quiet=*/true);
    };
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
        std::string app = ctx.appName;
        for (auto& ch : app)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (app != focusedApp_) {
            focusedApp_ = app;
            restoreLanguageForApp(app);
        }
    });
    pipeline_->setSelectWithDigits(settings_.suggestions.selectWithDigits);
    pipeline_->setSelectWithEnter(settings_.suggestions.selectWithEnter);
    sender_.setStrategy(settings_.advanced.sendKeys == core::model::SendKeysMode::KeyByKey
                            ? platform::win32::InputSender::Strategy::KeyByKey
                            : platform::win32::InputSender::Strategy::Batch);
    focus_.install();

    worker_->start();

    hotkeys_.configure(settings_.hotkeys);
    hook_.setHandler([this](const KeyEvent& key) noexcept -> bool {
        if (const auto action = hotkeyCapture_ ? std::nullopt : hotkeys_.onKey(key)) {
            // The text is about to change under the caret: drop the half-typed syllable.
            pipeline_->onPointerClick();
            PostMessageW(uiWindow_, kMsgHotkey, static_cast<WPARAM>(*action), 0);
            return true; // the chord never reaches the application
        }
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
    // The control panel is the face of the application: show it on launch rather than
    // leaving a new user to find the tray icon. Closing it leaves LanKey running.
    showSettings();
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
    snippetPicker_.destroy();
    popup_.destroy();
    tray_.destroy();
    if (uiWindow_ != nullptr) {
        if (clipboardListener_) {
            RemoveClipboardFormatListener(uiWindow_);
            clipboardListener_ = false;
        }
        DestroyWindow(uiWindow_);
        uiWindow_ = nullptr;
    }
    if (store_) store_->close();
    settingsWatcher_.stop();
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
        if (wParam == kSettingsFileTimer) {
            KillTimer(uiWindow_, kSettingsFileTimer);
            onSettingsFileChanged();
        }
        if (wParam == kSnippetInsertTimer) {
            KillTimer(uiWindow_, kSnippetInsertTimer);
            insertSnippet(pendingSnippet_);
            pendingSnippet_.clear();
        }
        return 0;
    case kMsgLanguage:
        // Ctrl+Shift on the hook thread already flipped the pipeline; mirror it here and
        // tell the user - a toast, not a sound.
        settings_.vietnameseEnabled = wParam != 0;
        rememberLanguageForFocusedApp(settings_.vietnameseEnabled);
        refreshTray();
        toast_.show(GetModuleHandleW(nullptr), settings_.vietnameseEnabled);
        settingsWindow_.setSettings(settings_);
        saveSettings();
        return 0;
    case kMsgDictionary: {
        std::unique_ptr<std::vector<ui::win32::SettingsWindow::DictionaryEntry>> entries(
            reinterpret_cast<std::vector<ui::win32::SettingsWindow::DictionaryEntry>*>(lParam));
        if (entries) settingsWindow_.setDictionary(std::move(*entries));
        return 0;
    }
    case kMsgStats: {
        std::unique_ptr<StatsBundle> bundle(reinterpret_cast<StatsBundle*>(lParam));
        if (bundle) {
            settingsWindow_.setStats(bundle->stats);
            settingsWindow_.setRecentCorrections(std::move(bundle->recent));
        }
        return 0;
    }
    case WM_CLOSE:
    case WM_QUERYENDSESSION:
    case WM_ENDSESSION:
        // Graceful exit from the outside (logoff, `taskkill` without /F, tooling):
        // shutdown() then flushes learning and saves settings.
        PostQuitMessage(0);
        return msg == WM_QUERYENDSESSION ? TRUE : 0;
    case kMsgShowSettings:
        showSettings();
        return 0;
    case kMsgHotkey:
        onHotkey(static_cast<core::model::HotkeyAction>(wParam));
        return 0;
    case kMsgSettingsFile:
        // Restart the timer on every notification: one save produces several.
        SetTimer(uiWindow_, kSettingsFileTimer, kSettingsFileSettleMs, nullptr);
        return 0;
    case WM_CLIPBOARDUPDATE:
        onClipboardChanged();
        return 0;
    case kMsgEraseDone:
        tray_.showBalloon(L"LanKey", wParam != 0 ? L"Đã xoá toàn bộ dữ liệu đã học."
                                                 : L"Không xoá được dữ liệu - xem lankey.log.");
        if (settingsWindow_.visible()) loadDictionaryAsync();
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
    // Nothing on screen is taking these keystrokes: the user clicked out of the text box
    // and the focus fell back to something read-only (in a browser, the page itself).
    // Checked here rather than earlier because this is the one place every list passes
    // through - the ones that wait for the idle timer and the glossary and snippet rows
    // that appear at once. The pipeline is told as well, or it would go on swallowing Tab
    // for a popup nobody can see.
    const auto focus = caret_.textFocus();
    if (focus == platform::win32::CaretResolver::TextFocus::NotText) {
        logf("popup: suppressed, focus takes no text (type %d) in %s", caret_.lastControlType(),
             focusedApp_.c_str());
        hook_.post([this] { pipeline_->dismissPopup(); });
        return;
    }
    const auto caret = caret_.resolve();
    if (!caret) {
        // Only the anomaly is worth a line: no caret means the popup lands in a screen
        // corner instead of next to the text. A line per popup would say nothing.
        logf("popup: no caret in %s (focus %s)", focusedApp_.c_str(),
             platform::win32::CaretResolver::focusName(focus));
    }
    popup_.show(state.items, state.selected, caret);
}

void App::onIdleTimer() {
    KillTimer(uiWindow_, kIdleTimer);
    const std::uint64_t generation = pendingGeneration_;
    pendingGeneration_ = 0;
    if (generation == 0) return;
    hook_.post([this, generation] { pipeline_->promotePending(generation); });
}

void App::applyVietnameseEnabled(bool enabled, bool quiet) {
    pipeline_->setVietnameseEnabled(enabled);
    settings_.vietnameseEnabled = enabled;
    rememberLanguageForFocusedApp(enabled);
    refreshTray();
    // `quiet`: undoing the switch a double click caused. The user never asked for the
    // language to change, so they should not be told that it did.
    if (!quiet) toast_.show(GetModuleHandleW(nullptr), enabled);
    settingsWindow_.setSettings(settings_);
    saveSettings();
}

void App::rememberLanguageForFocusedApp(bool vietnamese) {
    if (!settings_.languageMemory.enabled || focusedApp_.empty()) return;
    auto& perApp = settings_.languageMemory.perApp;
    for (auto& [app, mode] : perApp) {
        if (app == focusedApp_) {
            mode = vietnamese;
            return;
        }
    }
    perApp.emplace_back(focusedApp_, vietnamese);
}

void App::restoreLanguageForApp(const std::string& app) {
    if (!settings_.languageMemory.enabled) return;
    for (const auto& [name, vietnamese] : settings_.languageMemory.perApp) {
        if (name != app || vietnamese == settings_.vietnameseEnabled) continue;
        pipeline_->setVietnameseEnabled(vietnamese);
        settings_.vietnameseEnabled = vietnamese;
        refreshTray();
        toast_.show(GetModuleHandleW(nullptr), vietnamese);
        settingsWindow_.setSettings(settings_);
        return;
    }
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

// Replaces the selection with its `target` column from the user's glossary. One of these
// per language: the user says which one they want, rather than pressing a cycling key
// until the right language comes round.
void App::convertSelectionTo(core::model::Language target) {
    using Result = platform::win32::SelectionTransformer::Result;
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const ULONGLONG started = GetTickCount64();
    const Result r = selection_.transform(
        [this, target](std::u32string_view s) -> std::optional<std::u32string> {
            // Trim what a double click usually takes with the word.
            std::u32string_view word = s;
            while (!word.empty() && (word.front() == U' ' || word.front() == U'\n'))
                word.remove_prefix(1);
            while (!word.empty() && (word.back() == U' ' || word.back() == U'\n'))
                word.remove_suffix(1);
            if (!conversions_) return std::nullopt;
            const auto found = conversions_->translate(word, target);
            if (!found.found()) return std::nullopt;
            return found.text;
        });
    const wchar_t* chip = target == core::model::Language::Japanese ? L"JA" : L"EN";
    if (r == Result::Replaced) {
        toast_.showText(instance, chip, ui::win32::kBrandVietnamese,
                        L"Đã chuyển theo từ điển của bạn");
    } else if (r == Result::NoSelection) {
        toast_.showText(instance, L"!", ui::win32::kBrandEnglish, L"Hãy bôi đen văn bản trước");
    } else if (r == Result::Unchanged) {
        // Covers all three "nothing to do" cases: not in the glossary, that row has no
        // such column, or the selection already is that language.
        toast_.showText(instance, L"?", ui::win32::kBrandEnglish,
                        target == core::model::Language::Japanese
                            ? L"Từ điển của bạn chưa có tiếng Nhật cho từ này"
                            : L"Từ điển của bạn chưa có tiếng Anh cho từ này");
    }
    logf("hotkey: convert to %s -> %d in %llu ms",
         target == core::model::Language::Japanese ? "ja" : "en", static_cast<int>(r),
         static_cast<unsigned long long>(GetTickCount64() - started));
}

void App::onHotkey(core::model::HotkeyAction action) {
    using core::model::HotkeyAction;
    using Result = platform::win32::SelectionTransformer::Result;
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    switch (action) {
    case HotkeyAction::ConvertWidth: {
        bool toFull = false;
        const ULONGLONG started = GetTickCount64();
        const Result r =
            selection_.transform([&toFull](std::u32string_view s) -> std::optional<std::u32string> {
                std::u32string out = core::text::toggleWidth(s);
                toFull = out != s && out == core::text::toFullWidth(s);
                return out;
            });
        if (r == Result::Replaced) {
            toast_.showText(instance, toFull ? L"全" : L"半", ui::win32::kBrandVietnamese,
                            toFull ? L"Đã chuyển sang full-width" : L"Đã chuyển sang half-width");
        } else if (r == Result::NoSelection) {
            toast_.showText(instance, L"!", ui::win32::kBrandEnglish, L"Hãy bôi đen văn bản trước");
        } else if (r == Result::Unchanged) {
            toast_.showText(instance, L"=", ui::win32::kBrandEnglish, L"Không có gì để chuyển đổi");
        }
        logf("hotkey: convertWidth -> %d in %llu ms", static_cast<int>(r),
             static_cast<unsigned long long>(GetTickCount64() - started));
        return;
    }
    case HotkeyAction::ConvertEnglish:
        convertSelectionTo(core::model::Language::English);
        return;
    case HotkeyAction::ConvertJapanese:
        convertSelectionTo(core::model::Language::Japanese);
        return;
    case HotkeyAction::SnippetPicker:
        showSnippetPicker();
        return;
    case HotkeyAction::ClipboardHistory:
        toast_.showText(instance, L"…", ui::win32::kBrandEnglish,
                        L"Tính năng này đang được phát triển");
        return;
    }
}

namespace {

// How to hand a file to an editor. Electron editors (VS Code and its forks) ship a CLI
// wrapper in bin\\*.cmd: launching Code.exe with a file argument does nothing on some
// installs, while the wrapper always opens it in the running window. A .cmd needs a hidden
// console, hence `console`.
struct EditorLaunch {
    std::wstring command;
    bool console = false; // launch hidden: the .cmd would flash a console otherwise
};

// The editors worth preferring for a JSON file, in order, with the CLI wrapper to use when
// the editor ships one (relative to the executable's folder).
struct EditorCandidate {
    const wchar_t* exe;
    const wchar_t* cli; // nullptr: launch the executable itself
};
constexpr EditorCandidate kEditors[] = {
    {L"Code.exe", L"bin\\code.cmd"},
    {L"Code - Insiders.exe", L"bin\\code-insiders.cmd"},
    {L"Cursor.exe", L"resources\\app\\bin\\cursor.cmd"},
    {L"windsurf.exe", L"bin\\windsurf.cmd"},
    {L"sublime_text.exe", nullptr},
    {L"notepad++.exe", nullptr},
};

std::wstring appPathsEntry(const wchar_t* exe) {
    const std::wstring key =
        std::wstring(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\") + exe;
    for (const HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        wchar_t value[MAX_PATH] = {};
        DWORD bytes = sizeof(value);
        if (RegGetValueW(root, key.c_str(), nullptr, RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr,
                         value, &bytes) == ERROR_SUCCESS &&
            value[0] != 0) {
            return value;
        }
    }
    return {};
}

// A code editor to edit JSON with, or nothing when none is installed.
std::optional<EditorLaunch> findCodeEditor() {
    std::vector<std::filesystem::path> exePaths;
    const auto consider = [&](const EditorCandidate& candidate, std::filesystem::path exe) {
        std::error_code ec;
        if (exe.empty() || !std::filesystem::exists(exe, ec)) return false;
        if (candidate.cli != nullptr) {
            const std::filesystem::path cli = exe.parent_path() / candidate.cli;
            if (std::filesystem::exists(cli, ec)) return true;
        }
        return true;
    };
    for (const auto& candidate : kEditors) {
        std::filesystem::path exe = appPathsEntry(candidate.exe);
        if (exe.empty() && std::wstring_view(candidate.exe) == L"Code.exe") {
            // A user install of VS Code does not always register App Paths.
            wchar_t dir[MAX_PATH] = {};
            if (GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH) != 0) {
                exe = std::filesystem::path(dir) / L"Programs" / L"Microsoft VS Code" / L"Code.exe";
            }
            std::error_code ec;
            if (!std::filesystem::exists(exe, ec) &&
                GetEnvironmentVariableW(L"ProgramFiles", dir, MAX_PATH) != 0) {
                exe = std::filesystem::path(dir) / L"Microsoft VS Code" / L"Code.exe";
            }
        }
        if (!consider(candidate, exe)) continue;
        std::error_code ec;
        if (candidate.cli != nullptr) {
            const std::filesystem::path cli = exe.parent_path() / candidate.cli;
            if (std::filesystem::exists(cli, ec)) return EditorLaunch{cli.wstring(), true};
        }
        if (std::filesystem::exists(exe, ec)) return EditorLaunch{exe.wstring(), false};
    }
    return std::nullopt;
}

} // namespace

// Builds the glossary index from settings_ and publishes it to everything that reads it.
// Called at startup and after any change, whoever made it - the settings window, or the
// user editing settings.json in their editor.
void App::applyGlossary() {
    auto next = std::make_shared<core::convert::ConversionIndex>();
    next->load(settings_.glossary);
    logf("glossary: %zu of %zu rows usable", next->size(), settings_.glossary.size());
    // Published, not mutated: the hook thread may be reading the previous index right now.
    suggestions_.publishConversions(next);
    // The corrector needs it too, for the opposite reason: a term the user declared is
    // never a typo to repair.
    corrector_.publishConversions(next);
    conversions_ = std::move(next);
    publishGlossaryToWindow();
}

// Hands the glossary rows to the settings window, if it is open. The rows come from
// settings_ rather than from the index so that a row the index dropped - one the user has
// only half filled in - is still there to finish.
void App::publishGlossaryToWindow() {
    std::vector<ui::win32::SettingsWindow::GlossaryRow> rows;
    rows.reserve(settings_.glossary.size());
    for (const auto& e : settings_.glossary) {
        rows.push_back({platform::win32::fromUtf8(e.vi), platform::win32::fromUtf8(e.en),
                        platform::win32::fromUtf8(e.ja), platform::win32::fromUtf8(e.note)});
    }
    settingsWindow_.setGlossary(std::move(rows));
}

// The same for the snippets, plus the one side effect they have: whether the clipboard
// needs watching.
void App::applySnippets() {
    auto next = std::make_shared<core::snippet::SnippetIndex>();
    next->load(settings_.snippets);
    logf("snippets: %zu of %zu usable", next->size(), settings_.snippets.items.size());
    const bool wantsClipboard = next->usesClipboard();
    snippets_.publish(std::move(next));

    // The clipboard is watched only while a snippet actually asks for one. It holds
    // passwords often enough that keeping a copy should be something the user asked for,
    // not something LanKey does by default.
    if (wantsClipboard != clipboardListener_ && uiWindow_ != nullptr) {
        if (wantsClipboard) {
            clipboardListener_ = AddClipboardFormatListener(uiWindow_) != 0;
        } else {
            RemoveClipboardFormatListener(uiWindow_);
            clipboardListener_ = false;
            snippets_.setClipboard({}); // and drop what was already held
        }
        logf("snippets: clipboard watch %s", clipboardListener_ ? "on" : "off");
    }
    if (clipboardListener_) onClipboardChanged(); // seed with whatever is on it now
    publishSnippetsToWindow();
}

// Hands the abbreviations to the settings window, if it is open.
void App::publishSnippetsToWindow() {
    std::vector<ui::win32::SettingsWindow::SnippetRow> rows;
    rows.reserve(settings_.snippets.items.size());
    for (const auto& e : settings_.snippets.items) {
        rows.push_back(
            {platform::win32::fromUtf8(e.abbr), platform::win32::fromUtf8(e.body), e.autoExpand});
    }
    settingsWindow_.setSnippets(std::move(rows));

    std::vector<std::pair<std::wstring, std::wstring>> variables;
    variables.reserve(settings_.snippets.variables.size());
    for (const auto& [name, value] : settings_.snippets.variables) {
        variables.emplace_back(platform::win32::fromUtf8(name), platform::win32::fromUtf8(value));
    }
    settingsWindow_.setSnippetVariables(std::move(variables));
}

// The clipboard changed and some snippet uses {clipboard}. Read it here, on the UI thread,
// and hand the hook thread a copy: opening the clipboard from inside the keyboard hook
// would make a keystroke wait on whichever process happens to hold it.
void App::onClipboardChanged() {
    if (!clipboardListener_) return;
    // Password managers mark their entries so the clipboard history skips them. If they
    // went to that trouble, LanKey keeps no copy either.
    static const UINT kExcluded =
        RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
    if (kExcluded != 0 && IsClipboardFormatAvailable(kExcluded) != 0) {
        snippets_.setClipboard({});
        return;
    }
    const auto text = platform::win32::SelectionTransformer::readClipboardText();
    snippets_.setClipboard(text ? platform::win32::fromUtf16(*text) : std::u32string{});
}

// Older versions kept these in dictionary.csv and snippets.json. Read them once, into
// settings.json, and leave the files where they are: they are the user's copy of their own
// words, and deleting them to celebrate a file format change would be rude.
//
// Guarded by schemaVersion, not by "is the list empty": a user who deleted every row is
// not a user who wants them all back on the next start.
void App::importLegacyFiles() {
    const auto readFile = [](const std::filesystem::path& path) -> std::optional<std::string> {
        std::ifstream in(path, std::ios::binary);
        if (!in) return std::nullopt;
        std::stringstream buf;
        buf << in.rdbuf();
        return buf.str();
    };

    if (const auto csv = readFile(dataDir_ / L"dictionary.csv")) {
        core::convert::ConversionIndex index;
        if (const auto r = index.load(*csv); r) {
            index.forEach([&](const core::convert::Entry& e) {
                settings_.glossary.push_back({core::text::toUtf8(e.vi), core::text::toUtf8(e.en),
                                              core::text::toUtf8(e.ja),
                                              core::text::toUtf8(e.note)});
            });
            logf("import: %zu glossary rows from dictionary.csv", settings_.glossary.size());
        } else {
            logf("import: dictionary.csv unreadable (%s)", r.error().message.c_str());
        }
    }
    if (const auto json = readFile(dataDir_ / L"snippets.json")) {
        core::snippet::SnippetIndex index;
        if (const auto r = index.load(*json); r) {
            for (const auto& [name, value] : index.variables()) {
                settings_.snippets.variables.emplace_back(core::text::toUtf8(name),
                                                          core::text::toUtf8(value));
            }
            index.forEach([&](const core::snippet::Snippet& s) {
                settings_.snippets.items.push_back(
                    {core::text::toUtf8(s.abbr), core::text::toUtf8(s.body), s.autoExpand});
            });
            logf("import: %zu snippets from snippets.json", settings_.snippets.items.size());
        } else {
            logf("import: snippets.json unreadable (%s)", r.error().message.c_str());
        }
    }
}

// What a brand-new install starts with, so neither feature looks broken on first sight.
void App::seedStarterContent() {
    settings_.glossary = {
        {"đăng nhập", "login", "ログイン", ""},
        {"lỗi", "bug", "バグ", ""},
        {"kiểm thử", "test", "テスト", ""},
    };
    settings_.snippets.variables = {{"email", "ban@congty.com"}, {"ten", "Tên của bạn"}};
    settings_.snippets.items = {
        {"ce", "{email}", false},
        {"nay", "{date}", false},
        {"gio", "{time}", false},
        {"todo", "// TODO({ten} {date}): {cursor}", false},
        {"tks", "Cảm ơn bạn,\n{ten}", false},
    };
}

// Ctrl+Alt+S: the abbreviations by name, for when the abbreviation is what you forgot.
void App::showSnippetPicker() {
    const auto index = snippets_.index();
    if (!index || index->size() == 0) {
        toast_.showText(GetModuleHandleW(nullptr), L"…", ui::win32::kBrandEnglish,
                        L"Chưa có đoạn gõ tắt nào");
        return;
    }
    std::vector<ui::win32::SnippetPicker::Item> items;
    index->forEach([&](const core::snippet::Snippet& s) {
        items.push_back({platform::win32::toUtf16(s.abbr),
                         platform::win32::toUtf16(core::snippet::firstLine(s.body))});
    });
    ui::win32::SnippetPicker::Callbacks cb;
    cb.onChoose = [this](const std::wstring& abbr) {
        // Not inserted here: the focus has only just been handed back and the window it
        // went to has not finished activating. A short timer lets the message loop keep
        // running while that settles, which a Sleep on this thread would not.
        pendingSnippet_ = abbr;
        SetTimer(uiWindow_, kSnippetInsertTimer, kSnippetSettleMs, nullptr);
    };
    snippetPicker_.show(GetModuleHandleW(nullptr), std::move(items), std::move(cb));
}

// Sends a snippet body to whatever now has the focus. Nothing is deleted: unlike the
// typing path there is no abbreviation on screen to replace.
void App::insertSnippet(const std::wstring& abbr) {
    const auto rendered = snippets_.expand(platform::win32::fromUtf16(abbr));
    if (!rendered) return;
    core::model::TextReplacement cmd;
    cmd.insert = rendered->text;
    cmd.caretLeft = rendered->cursorOffsetFromEnd;
    cmd.reason = core::model::ReplacementReason::Macro;
    sender_.apply(cmd);
    // The length, not the abbreviation: DATA-POLICY promises this file never holds what
    // the user wrote, and an abbreviation is something they wrote.
    logf("snippets: inserted %zu chars", rendered->text.size());
}

// A code editor first: these are text files the user is meant to edit. Then whatever is
// associated with the extension, and Notepad as the last resort (a bare ShellExecute on an
// unassociated file only pops the "How do you want to open" picker).
void App::openFileInEditor(const std::filesystem::path& file) {
    const std::wstring path = file.wstring();
    const std::wstring quoted = L"\"" + path + L"\"";
    if (const auto editor = findCodeEditor()) {
        const auto rc = reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, L"open", editor->command.c_str(), quoted.c_str(), nullptr,
                          editor->console ? SW_HIDE : SW_SHOWNORMAL));
        logf("open file %s (%s) -> %lld",
             platform::win32::toUtf8(file.filename().wstring()).c_str(),
             platform::win32::toUtf8(editor->command).c_str(), static_cast<long long>(rc));
        if (rc > 32) return;
    }
    wchar_t exe[MAX_PATH] = {};
    const bool associated =
        reinterpret_cast<INT_PTR>(FindExecutableW(path.c_str(), nullptr, exe)) > 32;
    const auto rc = reinterpret_cast<INT_PTR>(
        associated ? ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)
                   : ShellExecuteW(nullptr, L"open", L"notepad.exe", quoted.c_str(), nullptr,
                                   SW_SHOWNORMAL));
    logf("open file %s (%s) -> %lld", platform::win32::toUtf8(file.filename().wstring()).c_str(),
         associated ? "associated editor" : "notepad", static_cast<long long>(rc));
}

void App::openSettingsFile() {
    saveSettings(); // make sure the file reflects what is running before the user edits it
    openFileInEditor(dataDir_ / L"settings.json");
}

void App::reloadSettingsFile() {
    auto loaded = settingsStore_->load();
    if (!loaded) {
        logf("settings: reload failed: %s", loaded.error().message.c_str());
        // A dialog would be wrong here: the save came from the editor, not from a button,
        // and the file is briefly invalid while the user types. A toast says why nothing
        // happened without stealing the caret.
        toast_.showText(GetModuleHandleW(nullptr), L"!", ui::win32::kBrandEnglish,
                        L"settings.json chưa hợp lệ — chưa áp dụng");
        return;
    }
    if (*loaded == settings_) {
        logf("settings: file matches what is running");
        return;
    }
    applySettings(*loaded, /*persist=*/false); // the file already holds what we just read
    settingsWindow_.setSettings(settings_);
    logf("settings: applied from file");
    toast_.showText(GetModuleHandleW(nullptr), L"⤓", ui::win32::kBrandVietnamese,
                    L"Đã áp dụng settings.json");
}

void App::onSettingsFileChanged() {
    // Our own save echoing back? The watcher fires for every write, including ours.
    std::ifstream in(settingsStore_->path(), std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (content == lastSavedSettings_) return;
    reloadSettingsFile();
}

void App::showSettings() {
    ui::win32::SettingsWindow::Runtime runtime;
    runtime.version = kVersion;
    runtime.dataDir = dataDir_.wstring();
    runtime.runAtStartup = runAtStartup();
    ui::win32::SettingsWindow::Callbacks cb;
    cb.onApply = [this](const core::model::Settings& next) { applySettings(next); };
    cb.onLoadDictionary = [this] { loadDictionaryAsync(); };
    cb.onGlossaryEdited = [this](std::vector<ui::win32::SettingsWindow::GlossaryRow> rows) {
        settings_.glossary.clear();
        settings_.glossary.reserve(rows.size());
        for (const auto& r : rows) {
            settings_.glossary.push_back(
                {platform::win32::toUtf8(r.vi), platform::win32::toUtf8(r.en),
                 platform::win32::toUtf8(r.ja), platform::win32::toUtf8(r.note)});
        }
        // Rebuilt and saved on the spot. A term is added because it is about to be used.
        applyGlossary();
        saveSettings();
    };
    cb.onSnippetsEdited = [this](std::vector<ui::win32::SettingsWindow::SnippetRow> rows) {
        settings_.snippets.items.clear();
        settings_.snippets.items.reserve(rows.size());
        for (const auto& r : rows) {
            settings_.snippets.items.push_back(
                {platform::win32::toUtf8(r.abbr), platform::win32::toUtf8(r.body), r.autoExpand});
        }
        applySnippets();
        saveSettings();
    };
    cb.onVariablesEdited = [this](std::vector<std::pair<std::wstring, std::wstring>> rows) {
        settings_.snippets.variables.clear();
        settings_.snippets.variables.reserve(rows.size());
        for (const auto& [name, value] : rows) {
            settings_.snippets.variables.emplace_back(platform::win32::toUtf8(name),
                                                      platform::win32::toUtf8(value));
        }
        // Sorted by name, the way settings.json stores them: a save must not reshuffle the
        // file, and the user's typing order carries no meaning here.
        std::sort(settings_.snippets.variables.begin(), settings_.snippets.variables.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        applySnippets();
        saveSettings();
    };
    cb.onRemoveEntries = [this](std::vector<std::wstring> phrases) {
        worker_->withStore([phrases = std::move(phrases)](core::ILexiconStore& store) {
            std::vector<std::u32string> keys;
            for (const auto& p : phrases)
                keys.push_back(platform::win32::fromUtf16(p));
            if (!store.removeEntries(keys)) logf("dictionary: remove failed");
        });
        worker_->requestRebuild();
        loadDictionaryAsync();
    };
    cb.onSetBlocked = [this](std::vector<std::wstring> phrases, bool blocked) {
        worker_->withStore([phrases = std::move(phrases), blocked](core::ILexiconStore& store) {
            std::vector<std::u32string> keys;
            for (const auto& p : phrases)
                keys.push_back(platform::win32::fromUtf16(p));
            if (!store.setBlocked(keys, blocked)) logf("dictionary: block failed");
        });
        worker_->requestRebuild();
        loadDictionaryAsync();
    };
    cb.onSetPinned = [this](std::vector<std::wstring> phrases, bool pinned) {
        worker_->withStore([phrases = std::move(phrases), pinned](core::ILexiconStore& store) {
            std::vector<std::u32string> keys;
            for (const auto& p : phrases)
                keys.push_back(platform::win32::fromUtf16(p));
            if (!store.setPinned(keys, pinned)) logf("dictionary: pin failed");
        });
        loadDictionaryAsync();
    };
    cb.onExport = [this](const std::wstring& path) {
        worker_->withStore([path](core::ILexiconStore& store) {
            const auto all = store.loadAll();
            if (!all) {
                logf("export: %s", all.error().message.c_str());
                return;
            }
            std::ofstream out(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
            out << "# LanKey - tu dien ca nhan (cum tu\tso lan\tlan cuoi)\n";
            for (const auto& e : *all) {
                out << core::text::toUtf8(e.phrase.joined()) << '\t' << e.frequency << '\t'
                    << e.lastUsedAt << '\n';
            }
        });
    };
    cb.onEraseAll = [this] { confirmEraseAllData(); };
    cb.onShowDataPolicy = [this] { showDataDialog(); };
    cb.onRefreshStats = [this] { refreshStatsAsync(); };
    cb.onRunAtStartup = [](bool on) { setRunAtStartup(on); };
    cb.onOpenDataFolder = [this] { openDataFolder(); };
    cb.onOpenSettingsFile = [this] { openSettingsFile(); };
    cb.onHotkeyCapture = [this](bool capturing) {
        hook_.post([this, capturing] { hotkeyCapture_ = capturing; });
    };
    cb.onResetDefaults = [this] {
        applySettings(core::model::Settings{});
        settingsWindow_.setSettings(settings_);
        logf("settings: reset to defaults");
    };
    settingsWindow_.show(GetModuleHandleW(nullptr), settings_, runtime, std::move(cb));
}

void App::applySettings(const core::model::Settings& next, bool persist) {
    const core::model::Settings previous = settings_;
    settings_ = next;
    if (next.engine != previous.engine) {
        const auto engineSettings = next.engine;
        hook_.post([this, engineSettings] {
            engine_.configure(engineSettings);
            pipeline_->onPointerClick(); // drop the half-typed syllable: its rules changed
        });
    }
    if (next.vietnameseEnabled != previous.vietnameseEnabled) {
        pipeline_->setVietnameseEnabled(next.vietnameseEnabled);
        rememberLanguageForFocusedApp(next.vietnameseEnabled);
        toast_.show(GetModuleHandleW(nullptr), next.vietnameseEnabled);
    }
    if (next.suggestions != previous.suggestions || next.autoCorrect != previous.autoCorrect ||
        next.privacy != previous.privacy) {
        worker_->updateSettings(settings_);
        pipeline_->setSelectWithDigits(next.suggestions.selectWithDigits);
        pipeline_->setSelectWithEnter(next.suggestions.selectWithEnter);
    }
    if (next.hotkeys != previous.hotkeys) {
        hook_.post([this, hotkeys = next.hotkeys] { hotkeys_.configure(hotkeys); });
    }
    if (next.advanced != previous.advanced) {
        sender_.setStrategy(next.advanced.sendKeys == core::model::SendKeysMode::KeyByKey
                                ? platform::win32::InputSender::Strategy::KeyByKey
                                : platform::win32::InputSender::Strategy::Batch);
    }
    refreshTray();
    if (persist) saveSettings();
}

void App::loadDictionaryAsync() {
    worker_->withStore([this](core::ILexiconStore& store) {
        const auto all = store.loadAll();
        if (!all) return;
        auto entries = std::make_unique<std::vector<ui::win32::SettingsWindow::DictionaryEntry>>();
        entries->reserve(all->size());
        for (const auto& e : *all) {
            entries->push_back({platform::win32::toUtf16(e.phrase.joined()), e.frequency,
                                e.lastUsedAt, e.blocked, e.pinned});
        }
        lexiconEntries_.store(all->size());
        if (PostMessageW(uiWindow_, kMsgDictionary, 0, reinterpret_cast<LPARAM>(entries.get()))) {
            (void)entries.release();
        }
    });
}

void App::refreshStatsAsync() {
    // Pipeline counters and the recent-correction ring belong to the hook thread.
    hook_.post([this] {
        auto bundle = std::make_unique<StatsBundle>();
        const auto& p = pipeline_->stats();
        const auto& h = hook_.stats();
        bundle->stats.keys = p.keys;
        bundle->stats.commits = p.commits;
        bundle->stats.corrections = p.correctionsApplied;
        bundle->stats.undone = p.correctionsUndone;
        bundle->stats.suggestionsShown = p.suggestionsShown;
        bundle->stats.suggestionsSelected = p.suggestionsSelected;
        bundle->stats.hookReinstalls = h.reinstalls.load();
        bundle->stats.hookExceptions = h.exceptions.load();
        bundle->stats.lastDeadGapMs = h.lastDeadGapMs.load();
        bundle->stats.maxCallbackMicros = h.maxCallbackMicros.load();
        bundle->stats.lexiconEntries = lexiconEntries_.load();
        for (const auto& r : pipeline_->recentCorrections()) {
            bundle->recent.push_back({platform::win32::toUtf16(r.original),
                                      platform::win32::toUtf16(r.corrected), r.undone});
        }
        if (PostMessageW(uiWindow_, kMsgStats, 0, reinterpret_cast<LPARAM>(bundle.get()))) {
            (void)bundle.release();
        }
    });
}

bool App::runAtStartup() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    const bool present =
        RegQueryValueExW(key, kRunValue, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(key);
    return present;
}

void App::setRunAtStartup(bool enabled) {
    // HKCU\...\Run needs no elevation and is visible in Task Manager > Startup: the
    // opposite of a hidden auto-start.
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
        return;
    }
    if (enabled) {
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const std::wstring quoted = L"\"" + std::wstring(exe) + L"\"";
        RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(quoted.c_str()),
                       static_cast<DWORD>((quoted.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, kRunValue);
    }
    RegCloseKey(key);
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
    lastSavedSettings_ = core::storage::JsonSettingsStore::serialize(settings_);
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
