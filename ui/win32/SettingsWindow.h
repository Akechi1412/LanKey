#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/model/Settings.h"

#include "platform/win32/Win32.h"
#include "ui/win32/CustomMethodDialog.h"

struct tagLVDISPINFOW; // <commctrl.h>; the virtual list asks for one cell at a time

namespace lankey::ui::win32 {

// The Settings window (PLAN 4.3.2): a navigation rail on the left, one page of plain Win32
// controls on the right, drawn in the LanKey palette (cobalt accent, slate text, white
// content) so it reads as the same product as the tray icon and the popup.
//
// It owns a working copy of Settings, applies every change immediately through
// Callbacks::onApply and never touches core objects itself: dictionary rows and counters
// arrive through setDictionary()/setStats()/setRecentCorrections() after the owner has
// fetched them on the right thread.
//
// Threading: UI thread only.
class SettingsWindow {
public:
    struct DictionaryEntry {
        std::wstring phrase;
        std::uint32_t frequency = 0;
        std::int64_t lastUsedAt = 0; // unix seconds
        bool blocked = false;
        bool pinned = false;
    };
    struct RecentCorrection {
        std::wstring original;
        std::wstring corrected;
        bool undone = false;
    };
    struct Stats {
        unsigned long long keys = 0;
        unsigned long long commits = 0;
        unsigned long long corrections = 0;
        unsigned long long undone = 0;
        unsigned long long suggestionsShown = 0;
        unsigned long long suggestionsSelected = 0;
        unsigned long long hookReinstalls = 0;
        unsigned long long hookExceptions = 0;
        unsigned long long lastDeadGapMs = 0; // silence that triggered the last reinstall
        unsigned int maxCallbackMicros = 0;
        unsigned long long lexiconEntries = 0;
    };
    struct Runtime {
        std::wstring version;
        std::wstring dataDir;
        bool runAtStartup = false;
    };
    struct Callbacks {
        std::function<void(const core::model::Settings&)> onApply;
        std::function<void()> onLoadDictionary; // -> setDictionary
        std::function<void(std::vector<std::wstring>)> onRemoveEntries;
        std::function<void(std::vector<std::wstring>, bool)> onSetBlocked;
        std::function<void(std::vector<std::wstring>, bool)> onSetPinned;
        std::function<void(const std::wstring& path)> onExport;
        std::function<void()> onEraseAll;
        std::function<void()> onShowDataPolicy;
        std::function<void()> onRefreshStats; // -> setStats, setRecentCorrections
        std::function<void(bool)> onRunAtStartup;
        std::function<void()> onOpenDataFolder;
        std::function<void()> onOpenSettingsFile; // settings.json in the user's editor
        std::function<void()> onResetDefaults;    // Settings{} -> onApply + setSettings
        // A hotkey field has/lost the focus: while it has, the global hotkeys must not
        // fire, or the chord being recorded would be swallowed (or worse, acted on).
        std::function<void(bool)> onHotkeyCapture;
    };

    SettingsWindow() = default;
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    void show(HINSTANCE instance, const core::model::Settings& settings, const Runtime& runtime,
              Callbacks callbacks);
    void destroy();
    [[nodiscard]] bool visible() const noexcept {
        return hwnd_ != nullptr && IsWindowVisible(hwnd_);
    }

    // Owner -> window. Safe to call while hidden (ignored).
    void setSettings(const core::model::Settings& settings); // external change (hotkey)
    void setDictionary(std::vector<DictionaryEntry> entries);
    void setRecentCorrections(std::vector<RecentCorrection> recent);
    void setStats(const Stats& stats);

private:
    enum class Page { Typing, Options, Smart, Dictionary, Privacy, Shortcuts, Advanced, About };
    static constexpr int kPageCount = 8;

    // A control and where it goes, in 96-dpi units relative to the content area. `stretch`
    // controls take the remaining height when the page is laid out (the dictionary list).
    struct Placed {
        HWND hwnd = nullptr;
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        bool stretch = false;
        bool rightAligned = false; // x measured from the right edge
        bool fullWidth = false;    // w ignored: content width minus x
    };

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handle(UINT msg, WPARAM wParam, LPARAM lParam);
    void build(HINSTANCE instance);
    void buildTyping();
    void buildOptions();
    void buildSmart();
    void buildDictionary();
    void buildPrivacy();
    void buildShortcuts();
    void buildAdvanced();
    void buildAbout();

    // Page-building vocabulary. Each keeps a per-page cursor `y_` moving down.
    HWND add(Page page, const wchar_t* cls, const wchar_t* text, DWORD style, int id, int x, int w,
             int h, DWORD exStyle = 0);
    void heading(Page page, const wchar_t* text);         // section title + hairline
    void note(Page page, const wchar_t* text, int lines); // dim explanatory text
    void pathNote(Page page, const wchar_t* text);        // shortened in the middle
    HWND check(Page page, const wchar_t* text, int id);
    HWND radio(Page page, const wchar_t* text, int id, bool first);
    HWND numberField(Page page, const wchar_t* label, int id);
    HWND button(Page page, const wchar_t* text, int id, int w, bool sameRow);
    HWND multiline(Page page, int id, int lines);

    // Check boxes and radio buttons are owner-drawn (Theme::drawToggle) so they match the
    // rest of the window; their state lives here, not in the control.
    struct Toggle {
        HWND hwnd = nullptr;
        int id = 0;
        bool radio = false;
        int group = 0; // radios: the set that excludes each other
        bool checked = false;
    };
    [[nodiscard]] Toggle* toggleFor(HWND hwnd);
    [[nodiscard]] bool isChecked(int id) const;
    void setChecked(int id, bool on);
    void drawToggle(const DRAWITEMSTRUCT& item);
    void drawStats(const DRAWITEMSTRUCT& item);
    void fitDictionaryColumns(); // the phrase column takes the width left over
    void updateDictionaryStatus();
    void dictionaryDispInfo(tagLVDISPINFOW& info);
    void shortcutRow(Page page, const wchar_t* keys, const wchar_t* what);

    void selectPage(Page page);
    void layout();
    // Pages that grew past the window scroll; the ones built around a stretching control
    // (the dictionary list) adapt to the height instead and never scroll.
    [[nodiscard]] int pageContentHeight(Page page) const; // 96-dpi units, 0 = do not scroll
    void updateScrollBar();
    void scrollTo(int offsetPx);
    [[nodiscard]] RECT scrollArea() const; // the part of the window that scrolls
    void rebuildFonts();
    void onDpiChanged(UINT dpi, const RECT& suggested);
    void paintChrome(HDC dc, const RECT& client);
    void drawNavItem(const DRAWITEMSTRUCT& item);
    void loadControls();      // settings_ -> controls
    void applyFromControls(); // controls -> settings_ -> onApply
    void onCommand(int id, int code, HWND from);
    void fillDictionaryList();
    std::vector<std::wstring> selectedPhrases() const;
    void applyAppLists();
    void applyHotkeys(int changedId); // a HotkeyField changed: reject duplicates, apply
    void showCustomKeys();            // the "Phím: ..." summary next to the user-defined method
    void defineCustomKeys();          // opens CustomMethodDialog
    int px(int v) const;
    RECT contentRect() const;

    HWND hwnd_ = nullptr;
    HWND nav_ = nullptr;
    HFONT font_ = nullptr;
    HFONT semibold_ = nullptr;
    HFONT title_ = nullptr;
    HFONT navFont_ = nullptr;
    HFONT statFont_ = nullptr; // the numbers on the diagnostics card
    HBRUSH contentBrush_ = nullptr;
    HBRUSH navBrush_ = nullptr;
    UINT dpi_ = 96;
    HINSTANCE instance_ = nullptr;
    Callbacks callbacks_;
    core::model::Settings settings_;
    Runtime runtime_;
    bool loading_ = false; // suppress apply while controls are being filled
    std::vector<Placed> pages_[kPageCount];
    std::vector<HWND> headings_;   // drawn with the semibold font, hairline underneath
    std::vector<HWND> titles_;     // page titles, drawn with the title font
    std::vector<HWND> dimStatics_; // drawn in the dim colour
    std::vector<Toggle> toggles_;
    int radioGroup_ = 0;
    int scrollY_ = 0; // device pixels the current page is scrolled down by
    CustomMethodDialog customMethod_;
    int y_[kPageCount] = {};
    int rowX_ = 0; // for sameRow buttons
    Page current_ = Page::Typing;

    std::vector<DictionaryEntry> dictionary_;
    std::vector<int> dictionaryIndex_; // rows shown -> dictionary_ index (after filtering)
    std::vector<std::wstring> folded_; // lowercased phrases, for searching without
                                       // re-folding 22k strings on every keystroke
    std::wstring dispBuf_;             // one cell of text, handed to the list view
    bool dictionaryLoading_ = false;   // waiting for the database thread
    std::vector<RecentCorrection> recent_;
    Stats stats_;
};

} // namespace lankey::ui::win32
