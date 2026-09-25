#pragma once

#include <atomic>
#include <filesystem>
#include <memory>

#include "core/engine/OpenKeyEngineAdapter.h"
#include "core/model/Settings.h"
#include "core/pipeline/HotkeyDetector.h"
#include "core/pipeline/InputPipeline.h"
#include "core/pipeline/ToggleHotkey.h"
#include "core/smart/SmartWorker.h"
#include "core/smart/correct/AutoCorrectEngine.h"
#include "core/smart/suggest/SuggestionEngine.h"
#include "core/storage/JsonSettingsStore.h"
#include "core/storage/SqliteLexiconStore.h"
#include "core/util/SystemClock.h"

#include "platform/win32/CaretResolver.h"
#include "platform/win32/DpapiProtector.h"
#include "platform/win32/FileWatcher.h"
#include "platform/win32/FocusWatcher.h"
#include "platform/win32/InputSender.h"
#include "platform/win32/KeyboardHook.h"
#include "platform/win32/SelectionTransformer.h"
#include "platform/win32/Win32.h"
#include "ui/win32/DataDialog.h"
#include "ui/win32/LanguageToast.h"
#include "ui/win32/SettingsWindow.h"
#include "ui/win32/SuggestionPopup.h"
#include "ui/win32/TrayIcon.h"

namespace lankey::app {

// Manual dependency injection: creates every object, connects the interfaces, owns the
// thread lifecycle. This is the only file that knows about every layer at once.
//
// Threads (PLAN 7.2):
//   hook   - KeyboardHook's thread: InputPipeline, engine, InputSender, ToggleHotkey
//   worker - SmartWorker: PrivacyFilter, LearningRecorder
//   db     - SmartWorker's TaskThread: SqliteLexiconStore, snapshot builds
//   ui     - this thread: message loop, TrayIcon, SuggestionPopup, FocusWatcher events
// Cross-thread traffic: hook -> ui via PostMessage (popup state), ui -> hook via
// KeyboardHook::post (focus / settings), hook -> worker via SmartWorker's SPSC queue.
class App {
public:
    App();
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Returns the process exit code.
    int run(HINSTANCE instance);

private:
    bool initialise(HINSTANCE instance);
    void shutdown();
    bool createUiWindow(HINSTANCE instance);
    static LRESULT CALLBACK uiWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT onUiMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // UI-thread actions
    void applyVietnameseEnabled(bool enabled, bool quiet = false);
    void setInputMethod(core::model::InputMethod method);
    void setSuggestionsEnabled(bool enabled);
    void setAutoCorrectEnabled(bool enabled);
    void showDataDialog();
    void openDataFolder();
    void onHotkey(core::model::HotkeyAction action);
    void openSettingsFile();
    void reloadSettingsFile();
    void onSettingsFileChanged(); // the watcher saw a save: apply it if it is not ours
    void showSettings();
    // Every settings change from the UI goes through here: diff against the current
    // settings, push each part to the thread that owns it, save.
    // persist = false when the change came from settings.json itself: writing it back
    // would fight the editor for the file and reformat what the user is editing.
    void applySettings(const core::model::Settings& next, bool persist = true);
    void loadDictionaryAsync();
    void refreshStatsAsync();
    void rememberLanguageForFocusedApp(bool vietnamese);
    void restoreLanguageForApp(const std::string& app);
    [[nodiscard]] static bool runAtStartup();
    static void setRunAtStartup(bool enabled);
    void confirmEraseAllData();
    void saveSettings();
    void refreshTray();
    void showPopup(const core::pipeline::PopupState& state);
    void onIdleTimer();

    std::filesystem::path dataDir_;
    core::model::Settings settings_;
    std::unique_ptr<core::storage::JsonSettingsStore> settingsStore_;

    core::util::SystemClock clock_;
    core::engine::OpenKeyEngineAdapter engine_;
    platform::win32::InputSender sender_;
    platform::win32::SelectionTransformer selection_;
    platform::win32::FileWatcher settingsWatcher_;
    // What we last wrote to settings.json. A notification whose content matches this is our
    // own save echoing back, not the user editing the file.
    std::string lastSavedSettings_;
    platform::win32::FocusWatcher focus_;
    platform::win32::CaretResolver caret_;
    core::smart::SuggestionEngine suggestions_;
    core::smart::AutoCorrectEngine corrector_;
    platform::win32::DpapiProtector protector_;
    std::unique_ptr<core::storage::SqliteLexiconStore> store_;
    std::unique_ptr<core::smart::SmartWorker> worker_;
    std::unique_ptr<core::pipeline::InputPipeline> pipeline_;
    core::pipeline::ToggleHotkey toggle_;
    core::pipeline::HotkeyDetector hotkeys_;
    bool hotkeyCapture_ = false; // hook thread only: set through KeyboardHook::post
    platform::win32::KeyboardHook hook_;

    ui::win32::TrayIcon tray_;
    ui::win32::SuggestionPopup popup_;
    ui::win32::DataDialog dataDialog_;
    ui::win32::LanguageToast toast_;
    ui::win32::SettingsWindow settingsWindow_;
    std::string focusedApp_; // lowercase executable name with focus (UI thread)
    std::atomic<unsigned long long> lexiconEntries_{0}; // from the last dictionary load
    HWND uiWindow_ = nullptr;
    HANDLE singleInstance_ = nullptr;
    std::uint64_t pendingGeneration_ = 0; // list waiting for the idle timer
};

} // namespace lankey::app
