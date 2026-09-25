#include "ui/win32/SettingsWindow.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <commctrl.h>
#include <commdlg.h>
#include <cstddef>
#include <ctime>
#include <iterator>
#include <optional>
#include <shellscalingapi.h>
#include <string>
#include <string_view>

#include "ui/win32/HotkeyField.h"
#include "ui/win32/Theme.h"

namespace lankey::ui::win32 {

using core::model::AutoCorrectLevel;
using core::model::CodeTable;
using core::model::InputMethod;
using core::model::SendKeysMode;
using core::model::Settings;

namespace {

constexpr wchar_t kClassName[] = L"LanKeySettingsWindow";
constexpr UINT kMsgRedraw = WM_APP + 1; // posted after a DPI change: repaint once settled
// Layout in 96-dpi pixels.
constexpr int kWidth = 860;
constexpr int kHeight = 660;
constexpr int kNavWidth = 208;
constexpr int kNavItemHeight = 40;
constexpr int kNavTop = 72; // below the product mark
constexpr int kContentMargin = 32;
constexpr int kTitleHeight = 36;
constexpr int kRowHeight = 26;
constexpr int kRowGap = 6;
constexpr int kButtonHeight = 30;
// The diagnostics card: two rows of tiles plus the hook-health line, at 96 dpi.
constexpr int kStatsRowHeight = 50;
// Dictionary columns at 96 dpi; the first is stretched to fill (fitDictionaryColumns).
constexpr int kDictColumnWidths[] = {260, 70, 110, 90};
constexpr int kStatsCardHeight = 18 + kStatsRowHeight * 2 + 26 + 18;

// The light face of the brand palette: the popup and toast are dark cards over other
// people's windows; a full window of our own reads better light, with the same cobalt.
constexpr COLORREF kNavBg = RGB(0xF3, 0xF5, 0xF8);
constexpr COLORREF kContentBg = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF kText = RGB(0x1B, 0x1F, 0x26);
constexpr COLORREF kDim = RGB(0x6B, 0x73, 0x80);
constexpr COLORREF kAccent = kBrandVietnamese;
constexpr COLORREF kTint = RGB(0xEA, 0xF1, 0xFD);
constexpr COLORREF kHairline = RGB(0xE3, 0xE7, 0xEC);

enum Id : int {
    kNav = 10,
    // Typing
    kTelex = 100,
    kVni,
    kSimpleTelex,
    kCustomMethod,
    kCustomKeys, // summary static
    kDefineKeys,
    kCodeUnicode,
    kCodeCompound,
    kCodeTcvn3,
    kCodeVni,
    kVietnameseOn,
    kModernTone,
    kSpellCheck,
    kQuickTelex,
    kRememberPerApp,
    // Smart
    kSuggestionsOn = 200,
    kIdleDelay,
    kMinPrefix,
    kDigits,
    kLevelOff,
    kLevelCautious,
    kLevelBalanced,
    kLevelAggressive,
    kRecentList,
    kRefreshRecent,
    // Explicit, past the block above: a control added here must not renumber the rest.
    kSelectWithEnter = 210,
    // Dictionary
    kSearch = 300,
    kDictList,
    kRemove,
    kBlock,
    kUnblock,
    kPin,
    kUnpin,
    kExport,
    kEraseAll,
    kDictStatus,
    kReloadDict,
    // Privacy
    kExcludedApps = 400,
    kSuggestionsOffApps,
    kAutoCorrectOffApps,
    kApplyLists,
    kShowPolicy,
    // Advanced
    // Explicit numbers: the UI test scripts address controls by id, so removing one control
    // must not renumber the others.
    kRunAtStartup = 500,
    kSendBatch = 501,
    kSendKeyByKey = 502,
    kStatsText = 503,
    kRefreshStats = 504,
    kOpenFolder = 505,
    kOpenSettingsFile = 506,
    kResetDefaults = 508,
    // Shortcuts (one field per HotkeyAction, in kAllHotkeyActions order)
    kHotkeyFirst = 600,
    kHotkeyConvertLanguage = kHotkeyFirst,
    kHotkeyConvertWidth,
    kHotkeyClipboard,
    kHotkeySnippet,
    kHotkeyDefaults = 620,
};

int hotkeyFieldId(core::model::HotkeyAction a) {
    return kHotkeyFirst + static_cast<int>(a);
}

const wchar_t* hotkeyActionLabel(core::model::HotkeyAction a) {
    using core::model::HotkeyAction;
    switch (a) {
    case HotkeyAction::ConvertLanguage:
        return L"Chuyển đổi ngôn ngữ vùng bôi đen (VI → EN → JA)";
    case HotkeyAction::ConvertWidth:
        return L"Half-width ↔ full-width vùng bôi đen";
    case HotkeyAction::ClipboardHistory:
        return L"Mở clipboard history";
    case HotkeyAction::SnippetPicker:
        return L"Mở bảng chọn gõ tắt";
    }
    return L"";
}

const wchar_t* kPageTitles[] = {L"Kiểu gõ",         L"Tuỳ chọn gõ",    L"Gợi ý & Tự sửa",
                                L"Từ điển cá nhân", L"Quyền riêng tư", L"Phím tắt",
                                L"Nâng cao",        L"Giới thiệu"};

std::wstring lines(const std::vector<std::string>& apps) {
    std::wstring out;
    for (const auto& a : apps) {
        out += platform::win32::fromUtf8(a);
        out += L"\r\n";
    }
    return out;
}

std::vector<std::string> fromLines(HWND edit) {
    const int len = GetWindowTextLengthW(edit);
    std::wstring text(static_cast<std::size_t>(len) + 1, L'\0');
    GetWindowTextW(edit, text.data(), len + 1);
    text.resize(static_cast<std::size_t>(len));
    std::vector<std::string> out;
    std::wstring line;
    const auto flush = [&] {
        while (!line.empty() && (line.back() == L' ' || line.back() == L'\t'))
            line.pop_back();
        std::size_t start = 0;
        while (start < line.size() && (line[start] == L' ' || line[start] == L'\t'))
            ++start;
        if (start < line.size()) out.push_back(platform::win32::toUtf8(line.substr(start)));
        line.clear();
    };
    for (const wchar_t c : text) {
        if (c == L'\n') {
            flush();
        } else if (c != L'\r') {
            line.push_back(c);
        }
    }
    flush();
    return out;
}

int intOf(HWND edit, int fallback) {
    wchar_t buf[16] = {};
    GetWindowTextW(edit, buf, 15);
    wchar_t* end = nullptr;
    const long v = std::wcstol(buf, &end, 10);
    return end == buf ? fallback : static_cast<int>(v);
}

std::wstring dateOf(std::int64_t unixSeconds) {
    if (unixSeconds <= 0) return L"";
    const auto t = static_cast<std::time_t>(unixSeconds);
    std::tm tm{};
    localtime_s(&tm, &t);
    wchar_t buf[32] = {};
    wcsftime(buf, 31, L"%d/%m/%Y", &tm);
    return buf;
}

void fillRect(HDC dc, const RECT& r, COLORREF colour) {
    const HBRUSH b = CreateSolidBrush(colour);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

} // namespace

SettingsWindow::~SettingsWindow() {
    destroy();
}

int SettingsWindow::px(int v) const {
    return MulDiv(v, static_cast<int>(dpi_), 96);
}

RECT SettingsWindow::contentRect() const {
    RECT client{};
    GetClientRect(hwnd_, &client);
    return RECT{px(kNavWidth) + px(kContentMargin), px(kContentMargin),
                client.right - px(kContentMargin), client.bottom - px(kContentMargin)};
}

void SettingsWindow::show(HINSTANCE instance, const Settings& settings, const Runtime& runtime,
                          Callbacks callbacks) {
    callbacks_ = std::move(callbacks);
    settings_ = settings;
    runtime_ = runtime;
    instance_ = instance;
    if (hwnd_ == nullptr) {
        // Creating a control makes it notify the parent exactly as a user edit would, and
        // the hotkey fields do it while their siblings do not exist yet: applyHotkeys then
        // read four empty boxes and saved them over the real assignments. Nothing is
        // applied from anything that happens while the window is being built.
        loading_ = true;
        build(instance);
        loading_ = false;
    }
    if (hwnd_ == nullptr) return;
    loadControls();
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    if (callbacks_.onLoadDictionary) {
        // The phrases come from the database thread; say so instead of showing an empty
        // list that looks like "you have learned nothing".
        dictionaryLoading_ = true;
        updateDictionaryStatus();
        callbacks_.onLoadDictionary();
    }
    if (callbacks_.onRefreshStats) callbacks_.onRefreshStats();
}

void SettingsWindow::destroy() {
    customMethod_.destroy();
    if (hwnd_ != nullptr) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    for (const HFONT f : {font_, semibold_, title_, navFont_, statFont_}) {
        if (f != nullptr) DeleteObject(f);
    }
    font_ = semibold_ = title_ = navFont_ = statFont_ = nullptr;
    if (contentBrush_ != nullptr) DeleteObject(contentBrush_);
    contentBrush_ = nullptr;
    if (navBrush_ != nullptr) DeleteObject(navBrush_);
    navBrush_ = nullptr;
}

void SettingsWindow::setSettings(const Settings& settings) {
    settings_ = settings;
    if (hwnd_ != nullptr) loadControls();
}

void SettingsWindow::setDictionary(std::vector<DictionaryEntry> entries) {
    dictionary_ = std::move(entries);
    std::sort(dictionary_.begin(), dictionary_.end(),
              [](const DictionaryEntry& a, const DictionaryEntry& b) {
                  return a.frequency != b.frequency ? a.frequency > b.frequency
                                                    : a.phrase < b.phrase;
              });
    folded_.clear();
    folded_.reserve(dictionary_.size());
    for (const auto& e : dictionary_) {
        std::wstring lower = e.phrase;
        for (auto& c : lower)
            c = static_cast<wchar_t>(towlower(c));
        folded_.push_back(std::move(lower));
    }
    dictionaryLoading_ = false;
    if (hwnd_ != nullptr) fillDictionaryList();
}

void SettingsWindow::setRecentCorrections(std::vector<RecentCorrection> recent) {
    recent_ = std::move(recent);
    if (hwnd_ == nullptr) return;
    const HWND list = GetDlgItem(hwnd_, kRecentList);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const auto& r : recent_) {
        std::wstring row = r.original + L"  →  " + r.corrected;
        if (r.undone) row += L"   (đã hoàn tác)";
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
    }
    if (recent_.empty()) {
        SendMessageW(list, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"Chưa có lần sửa nào trong phiên này."));
    }
}

void SettingsWindow::setStats(const Stats& stats) {
    stats_ = stats;
    if (hwnd_ == nullptr) return;
    // The card is owner-drawn from stats_; the control carries no text of its own.
    if (const HWND card = GetDlgItem(hwnd_, kStatsText); card != nullptr) {
        InvalidateRect(card, nullptr, TRUE);
    }
}

namespace {

// 18122 -> "18.122". Long numbers are the point of the card, and an ungrouped one is a
// smear of digits.
std::wstring grouped(unsigned long long value) {
    const std::wstring digits = std::to_wstring(value);
    const std::size_t lead = digits.size() % 3 == 0 ? 3 : digits.size() % 3;
    std::wstring out;
    out.reserve(digits.size() + digits.size() / 3);
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i >= lead && (i - lead) % 3 == 0) out.push_back(L'.');
        out.push_back(digits[i]);
    }
    return out;
}

// A hook callback is budgeted in microseconds; past a millisecond the useful unit changes.
std::wstring callbackTime(unsigned int micros) {
    if (micros < 1000) return grouped(micros) + L" µs";
    wchar_t buf[32];
    swprintf_s(buf, L"%.1f ms", static_cast<double>(micros) / 1000.0);
    return buf;
}

} // namespace

void SettingsWindow::drawStats(const DRAWITEMSTRUCT& item) {
    const HDC dc = item.hDC;
    const RECT card = item.rcItem;

    // A flat panel one shade off the page, so the numbers read as data rather than as
    // more body text.
    const HBRUSH fill = CreateSolidBrush(RGB(0xF8, 0xF9, 0xFC));
    const HPEN edge = CreatePen(PS_SOLID, 1, kHairline);
    const HGDIOBJ oldBrush = SelectObject(dc, fill);
    const HGDIOBJ oldPen = SelectObject(dc, edge);
    const int radius = px(8);
    RoundRect(dc, card.left, card.top, card.right, card.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(fill);
    DeleteObject(edge);

    struct Tile {
        const wchar_t* label;
        std::wstring value;
    };
    const Tile tiles[] = {
        {L"Phím đã xử lý", grouped(stats_.keys)},
        {L"Âm tiết đã xong", grouped(stats_.commits)},
        {L"Gợi ý đã hiện", grouped(stats_.suggestionsShown)},
        {L"Gợi ý đã chọn", grouped(stats_.suggestionsSelected)},
        {L"Lần tự sửa", grouped(stats_.corrections)},
        {L"Lần hoàn tác", grouped(stats_.undone)},
        {L"Cụm từ đã học", grouped(stats_.lexiconEntries)},
        {L"Callback lâu nhất", callbackTime(stats_.maxCallbackMicros)},
    };
    constexpr int kCols = 4;
    const int pad = px(18);
    const int tileW = ((card.right - card.left) - 2 * pad) / kCols;
    const int tileH = px(kStatsRowHeight);

    SetBkMode(dc, TRANSPARENT);
    for (int i = 0; i < static_cast<int>(std::size(tiles)); ++i) {
        const int x = card.left + pad + (i % kCols) * tileW;
        const int y = card.top + pad + (i / kCols) * tileH;
        SelectObject(dc, statFont_);
        SetTextColor(dc, kText);
        RECT value{x, y, x + tileW, y + px(26)};
        DrawTextW(dc, tiles[i].value.c_str(), -1, &value,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, font_);
        SetTextColor(dc, kDim);
        RECT label{x, y + px(27), x + tileW, y + px(45)};
        DrawTextW(dc, tiles[i].label, -1, &label,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    // The two counters that should never move get a sentence instead of two tiles: their
    // value is "still zero", and saying so is more use than two more numbers.
    const bool healthy = stats_.hookReinstalls == 0 && stats_.hookExceptions == 0;
    const std::wstring health =
        healthy ? std::wstring(L"Hook chạy ổn định — chưa phải cài lại lần nào, "
                               L"không có ngoại lệ nào.")
                : L"Hook cài lại: " + grouped(stats_.hookReinstalls) +
                      L"        Ngoại lệ trong hook: " + grouped(stats_.hookExceptions) +
                      (stats_.lastDeadGapMs != 0 ? L"        Im lặng lần cuối: " +
                                                       grouped(stats_.lastDeadGapMs / 1000) + L" s"
                                                 : std::wstring());
    SelectObject(dc, font_);
    SetTextColor(dc, healthy ? kDim : RGB(0xB4, 0x23, 0x18));
    RECT foot{card.left + pad, card.bottom - pad - px(18), card.right - pad, card.bottom - pad};
    DrawTextW(dc, health.c_str(), -1, &foot,
              DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

// -- page vocabulary
// -------------------------------------------------------------------------------

HWND SettingsWindow::add(Page page, const wchar_t* cls, const wchar_t* text, DWORD style, int id,
                         int x, int w, int h, DWORD exStyle) {
    const HWND hwnd =
        CreateWindowExW(exStyle, cls, text, WS_CHILD | style, 0, 0, 10, 10, hwnd_,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    Placed p;
    p.hwnd = hwnd;
    p.x = x;
    p.y = y_[static_cast<int>(page)];
    p.w = w;
    p.h = h;
    p.fullWidth = w == 0;
    pages_[static_cast<int>(page)].push_back(p);
    return hwnd;
}

void SettingsWindow::heading(Page page, const wchar_t* text) {
    auto& y = y_[static_cast<int>(page)];
    if (rowX_ > 0) {
        y += kButtonHeight + 10;
        rowX_ = 0;
    }
    if (y > kTitleHeight + 8) y += 10; // breathing room between sections
    const HWND h = add(page, L"STATIC", text, SS_LEFT | SS_NOPREFIX, 0, 0, 0, 22);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(semibold_), TRUE);
    headings_.push_back(h);
    y += 22 + 12;
}

// A file path, shortened in the middle when it does not fit ("C:\Users\...\settings.json")
// rather than cut off at the end, where the interesting part is.
void SettingsWindow::pathNote(Page page, const wchar_t* text) {
    auto& y = y_[static_cast<int>(page)];
    if (rowX_ > 0) {
        y += kButtonHeight + 10;
        rowX_ = 0;
    }
    dimStatics_.push_back(
        add(page, L"STATIC", text, SS_LEFT | SS_NOPREFIX | SS_PATHELLIPSIS, 0, 0, 0, 18));
    y += 18 + 8;
}

void SettingsWindow::note(Page page, const wchar_t* text, int linesOfText) {
    auto& y = y_[static_cast<int>(page)];
    if (rowX_ > 0) {
        y += kButtonHeight + 10;
        rowX_ = 0;
    }
    const int h = 18 * linesOfText;
    const HWND s = add(page, L"STATIC", text, SS_LEFT | SS_NOPREFIX, 0, 0, 0, h);
    dimStatics_.push_back(s);
    y += h + 8;
}

HWND SettingsWindow::check(Page page, const wchar_t* text, int id) {
    auto& y = y_[static_cast<int>(page)];
    if (rowX_ > 0) {
        y += kButtonHeight + 10;
        rowX_ = 0;
    }
    const HWND h = add(page, L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP, id, 0, 0, kRowHeight);
    toggles_.push_back({h, id, false, 0, false});
    y += kRowHeight + kRowGap;
    return h;
}

HWND SettingsWindow::radio(Page page, const wchar_t* text, int id, bool first) {
    auto& y = y_[static_cast<int>(page)];
    if (rowX_ > 0) {
        y += kButtonHeight + 10;
        rowX_ = 0;
    }
    if (first) ++radioGroup_;
    const HWND h = add(page, L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP, id, 0, 0, kRowHeight);
    toggles_.push_back({h, id, true, radioGroup_, false});
    y += kRowHeight + kRowGap;
    return h;
}

SettingsWindow::Toggle* SettingsWindow::toggleFor(HWND hwnd) {
    for (auto& t : toggles_) {
        if (t.hwnd == hwnd) return &t;
    }
    return nullptr;
}

bool SettingsWindow::isChecked(int id) const {
    for (const auto& t : toggles_) {
        if (t.id == id) return t.checked;
    }
    return false;
}

void SettingsWindow::setChecked(int id, bool on) {
    for (auto& t : toggles_) {
        if (t.id != id) continue;
        if (t.checked == on) return;
        t.checked = on;
        InvalidateRect(t.hwnd, nullptr, FALSE);
        if (on && t.radio) {
            for (auto& other : toggles_) {
                if (&other != &t && other.radio && other.group == t.group && other.checked) {
                    other.checked = false;
                    InvalidateRect(other.hwnd, nullptr, FALSE);
                }
            }
        }
        return;
    }
}

void SettingsWindow::drawToggle(const DRAWITEMSTRUCT& item) {
    const Toggle* t = toggleFor(item.hwndItem);
    if (t == nullptr) return;
    wchar_t label[256] = {};
    GetWindowTextW(item.hwndItem, label, 255);
    ToggleFace face;
    face.radio = t->radio;
    face.checked = t->checked;
    face.pressed = (item.itemState & ODS_SELECTED) != 0;
    face.disabled = (item.itemState & ODS_DISABLED) != 0;
    face.focused = (item.itemState & ODS_FOCUS) != 0 && (item.itemState & ODS_NOFOCUSRECT) == 0;
    win32::drawToggle(item.hDC, item.rcItem, label, face, kContentBg, kText, dpi_);
}

HWND SettingsWindow::numberField(Page page, const wchar_t* label, int id) {
    auto& y = y_[static_cast<int>(page)];
    if (rowX_ > 0) {
        y += kButtonHeight + 10;
        rowX_ = 0;
    }
    add(page, L"STATIC", label, SS_LEFT | SS_NOPREFIX, 0, 0, 400, kRowHeight);
    const HWND h = add(page, L"EDIT", L"", ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, id, 410, 72,
                       kRowHeight, WS_EX_CLIENTEDGE);
    y += kRowHeight + kRowGap + 2;
    return h;
}

HWND SettingsWindow::button(Page page, const wchar_t* text, int id, int w, bool sameRow) {
    auto& y = y_[static_cast<int>(page)];
    if (!sameRow && rowX_ > 0) {
        y += kButtonHeight + 10;
        rowX_ = 0;
    }
    const HWND h =
        add(page, L"BUTTON", text, BS_PUSHBUTTON | WS_TABSTOP, id, rowX_, w, kButtonHeight);
    rowX_ += w + 10;
    return h;
}

HWND SettingsWindow::multiline(Page page, int id, int linesOfText) {
    auto& y = y_[static_cast<int>(page)];
    if (rowX_ > 0) {
        y += kButtonHeight + 10;
        rowX_ = 0;
    }
    const int h = 19 * linesOfText + 10;
    const HWND e = add(page, L"EDIT", L"",
                       ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_TABSTOP, id,
                       0, 0, h, WS_EX_CLIENTEDGE);
    y += h + 10;
    return e;
}

// -- construction ---------------------------------------------------------------------------------

void SettingsWindow::build(HINSTANCE instance) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.lpfnWndProc = &SettingsWindow::wndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // painted by hand
    RegisterClassW(&wc);

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    // The window opens centred on the primary work area: size it for that monitor's DPI
    // (WM_DPICHANGED takes over when it is dragged elsewhere).
    {
        const POINT centre{(work.left + work.right) / 2, (work.top + work.bottom) / 2};
        UINT x = 96;
        UINT y = 96;
        if (GetDpiForMonitor(MonitorFromPoint(centre, MONITOR_DEFAULTTOPRIMARY), MDT_EFFECTIVE_DPI,
                             &x, &y) == S_OK) {
            dpi_ = x;
        } else {
            dpi_ = GetDpiForSystem();
        }
    }
    contentBrush_ = CreateSolidBrush(kContentBg);
    navBrush_ = CreateSolidBrush(kNavBg);
    rebuildFonts();

    // The pages are laid out at the design width; the scrollbar (which appears whenever a
    // page is taller than the window) must not eat into it.
    const int w = px(kWidth) + static_cast<int>(GetSystemMetricsForDpi(SM_CXVSCROLL, dpi_));
    const int h = px(kHeight);
    // No WS_EX_COMPOSITED: measured 2026-09-24, it puts the list view into a repaint loop
    // that burns 4.3 s of CPU every 5 s and flickers on screen. Batching the control
    // moves below is what the scrolling actually needed.
    hwnd_ = CreateWindowExW(0, kClassName, L"LanKey — Bảng điều khiển",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                                WS_MAXIMIZEBOX | WS_THICKFRAME | WS_VSCROLL | WS_CLIPCHILDREN,
                            (work.left + work.right - w) / 2, (work.top + work.bottom - h) / 2, w,
                            h, nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) return;
    setWindowIcons(hwnd_, instance);
    enableDialogNavigation(hwnd_);

    nav_ = CreateWindowExW(0, L"LISTBOX", L"",
                           WS_CHILD | WS_VISIBLE | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS |
                               LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                           0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(kNav), instance, nullptr);
    SendMessageW(nav_, WM_SETFONT, reinterpret_cast<WPARAM>(navFont_), TRUE);
    SendMessageW(nav_, LB_SETITEMHEIGHT, 0, static_cast<LPARAM>(px(kNavItemHeight)));
    for (const auto* title : kPageTitles)
        SendMessageW(nav_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(title));

    for (int p = 0; p < kPageCount; ++p) {
        y_[p] = 0;
        const HWND t = add(static_cast<Page>(p), L"STATIC", kPageTitles[p], SS_LEFT | SS_NOPREFIX,
                           0, 0, 0, kTitleHeight);
        SendMessageW(t, WM_SETFONT, reinterpret_cast<WPARAM>(title_), TRUE);
        titles_.push_back(t);
        y_[p] = kTitleHeight + 8;
    }
    buildTyping();
    buildOptions();
    buildSmart();
    buildDictionary();
    buildPrivacy();
    buildShortcuts();
    buildAdvanced();
    buildAbout();
    layout();
    selectPage(Page::Typing);
}

void SettingsWindow::buildTyping() {
    const Page p = Page::Typing;
    heading(p, L"Kiểu gõ");
    radio(p, L"Telex", kTelex, true);
    radio(p, L"VNI", kVni, false);
    radio(p, L"Telex đơn giản — không có phím tắt gõ nhanh", kSimpleTelex, false);
    radio(p, L"Tự định nghĩa", kCustomMethod, false);
    {
        auto& y = y_[static_cast<int>(p)];
        const HWND summary = add(p, L"STATIC", L"", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS,
                                 kCustomKeys, 28, -150, kRowHeight);
        pages_[static_cast<int>(p)].back().y += 4;
        dimStatics_.push_back(summary);
        add(p, L"BUTTON", L"Định nghĩa", BS_PUSHBUTTON | WS_TABSTOP, kDefineKeys, 140, 140,
            kButtonHeight);
        pages_[static_cast<int>(p)].back().rightAligned = true;
        y += kButtonHeight + 10;
    }
    heading(p, L"Bảng mã");
    radio(p, L"Unicode  (mặc định)", kCodeUnicode, true);
    radio(p, L"Unicode tổ hợp — chữ cái + dấu rời", kCodeCompound, false);
    radio(p, L"TCVN3 (ABC)", kCodeTcvn3, false);
    radio(p, L"VNI Windows", kCodeVni, false);
    note(p,
         L"Gợi ý cụm từ, dự đoán và tự sửa lỗi chỉ hoạt động với Unicode; với bảng mã cũ LanKey "
         L"chỉ gõ, không học gì.",
         2);
}

void SettingsWindow::buildOptions() {
    const Page p = Page::Options;
    heading(p, L"Tuỳ chọn");
    check(p, L"Bật tiếng Việt  (Ctrl+Shift để đổi nhanh)", kVietnameseOn);
    check(p, L"Đặt dấu kiểu mới: hoà, thuý  (thay vì hòa, thúy)", kModernTone);
    check(p, L"Kiểm tra chính tả khi gõ — khôi phục phím nếu âm tiết không hợp lệ", kSpellCheck);
    check(p, L"Gõ nhanh: cc→ch, gg→gi, kk→kh, nn→ng, qq→qu, pp→ph, tt→th, uu→ươ", kQuickTelex);
    heading(p, L"Theo ứng dụng");
    check(p, L"Nhớ Việt/Anh theo từng ứng dụng", kRememberPerApp);
    note(p,
         L"Khi bật, LanKey ghi nhớ lần đổi Việt/Anh gần nhất trong mỗi ứng dụng và tự đặt lại "
         L"khi bạn quay về ứng dụng đó.",
         2);
}

void SettingsWindow::buildSmart() {
    const Page p = Page::Smart;
    heading(p, L"Gợi ý cụm từ");
    check(p, L"Bật gợi ý — dự đoán từ tiếp theo sau dấu cách, hoàn thành cụm đang gõ",
          kSuggestionsOn);
    numberField(p, L"Hiện popup sau khi ngừng gõ (ms, 0–5000)", kIdleDelay);
    numberField(p, L"Số ký tự tối thiểu để hoàn thành cụm (1–4)", kMinPrefix);
    check(p, L"Chọn gợi ý bằng phím số 1–5  (ngoài Tab)", kDigits);
    check(p, L"Chọn gợi ý bằng phím Enter  (tắt: Enter xuống dòng như bình thường)",
          kSelectWithEnter);
    heading(p, L"Tự sửa lỗi chính tả");
    radio(p, L"Tắt", kLevelOff, true);
    radio(p, L"Thận trọng — chỉ sửa lỗi dấu/mũ khi ứng viên rõ ràng  (mặc định)", kLevelCautious,
          false);
    radio(p, L"Cân bằng — thêm lỗi thiếu hoặc thừa một chữ", kLevelBalanced, false);
    radio(p, L"Tích cực — sửa xa hơn một chút", kLevelAggressive, false);
    heading(p, L"Vừa sửa gần đây");
    note(p, L"Backspace hoặc Ctrl+Z ngay sau một lần sửa để hoàn tác.", 1);
    auto& y = y_[static_cast<int>(p)];
    add(p, L"LISTBOX", L"", LBS_NOSEL | LBS_NOINTEGRALHEIGHT | WS_VSCROLL, kRecentList, 0, 0, 96,
        WS_EX_CLIENTEDGE);
    y += 96 + 10;
    button(p, L"Làm mới", kRefreshRecent, 110, false);
}

void SettingsWindow::buildDictionary() {
    const Page p = Page::Dictionary;
    auto& page = pages_[static_cast<int>(p)];
    auto& y = y_[static_cast<int>(p)];
    // SS_CENTERIMAGE puts the single line of text on the box's own centre line instead of
    // at the top of its rectangle, which is what made the label look adrift.
    add(p, L"STATIC", L"Tìm kiếm", SS_LEFT | SS_NOPREFIX | SS_CENTERIMAGE, 0, 0, 72, kRowHeight);
    add(p, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, kSearch, 78, 300, kRowHeight,
        WS_EX_CLIENTEDGE);
    {
        const HWND status =
            add(p, L"STATIC", L"", SS_LEFT | SS_NOPREFIX | SS_CENTERIMAGE | SS_ENDELLIPSIS,
                kDictStatus, 390, 0, kRowHeight);
        dimStatics_.push_back(status);
    }
    y += kRowHeight + 6;
    // The search row was placed with explicit x positions, so the row is still "open";
    // note() would otherwise close it and add a button row's worth of space first.
    rowX_ = 0;
    note(p, L"Ctrl/Shift để chọn nhiều dòng", 1);
    const int listTop = y;
    {
        // LVS_OWNERDATA: the control keeps no rows of its own and asks for the text of the
        // handful it is about to paint. Filling it row by row cost 2.2 s for a search that
        // matched 7 229 of 22 805 phrases (measured 2026-09-25) - four window messages per
        // row, 29 000 of them, for twenty visible lines.
        const HWND list =
            add(p, WC_LISTVIEWW, L"", LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | WS_TABSTOP,
                kDictList, 0, -122, 100, WS_EX_CLIENTEDGE);
        page.back().stretch = true; // down to the bottom; -122 leaves the button column free
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        const wchar_t* headers[] = {L"Cụm từ", L"Số lần", L"Dùng lần cuối", L"Trạng thái"};
        for (int c = 0; c < 4; ++c) {
            LVCOLUMNW col{};
            col.mask = LVCF_TEXT | LVCF_WIDTH;
            col.pszText = const_cast<wchar_t*>(headers[c]);
            col.cx = px(kDictColumnWidths[c]);
            ListView_InsertColumn(list, c, &col);
        }
    }
    // Button column, right-aligned next to the list.
    const struct {
        const wchar_t* text;
        int id;
    } actions[] = {{L"Xoá", kRemove},        {L"Chặn gợi ý", kBlock}, {L"Bỏ chặn", kUnblock},
                   {L"Ghim", kPin},          {L"Bỏ ghim", kUnpin},    {L"Xuất", kExport},
                   {L"Tải lại", kReloadDict}};
    int by = listTop;
    for (const auto& a : actions) {
        y = by;
        add(p, L"BUTTON", a.text, BS_PUSHBUTTON | WS_TABSTOP, a.id, 112, 112, kButtonHeight);
        page.back().rightAligned = true;
        by += kButtonHeight + 8;
        if (a.id == kUnpin) by += 12;
    }
    y = -1; // bottom-anchored (see layout)
    add(p, L"BUTTON", L"Xoá toàn bộ", BS_PUSHBUTTON | WS_TABSTOP, kEraseAll, 112, 112,
        kButtonHeight);
    page.back().rightAligned = true;
}

void SettingsWindow::buildPrivacy() {
    const Page p = Page::Privacy;
    heading(p, L"Không học, không gợi ý, không sửa trong");
    note(p,
         L"Một tên .exe mỗi dòng. Trình quản lý mật khẩu, terminal và Remote Desktop luôn bị "
         L"loại trừ.",
         1);
    multiline(p, kExcludedApps, 3);
    heading(p, L"Không hiện gợi ý trong  (vẫn học và sửa)");
    multiline(p, kSuggestionsOffApps, 2);
    heading(p, L"Không tự sửa lỗi trong  (vẫn học và gợi ý)");
    multiline(p, kAutoCorrectOffApps, 2);
    button(p, L"Áp dụng danh sách", kApplyLists, 170, false);
    heading(p, L"Dữ liệu");
    note(p,
         L"Từ điển cá nhân được niêm phong bằng Windows Data Protection theo tài khoản của "
         L"bạn. LanKey không có mã kết nối mạng.",
         2);
    button(p, L"Dữ liệu của bạn", kShowPolicy, 170, false);
}

// One line of the shortcut tables: the keys on the left, what they do on the right.
// These used to be tab characters inside a single static, but a static expands tabs at
// fixed intervals, so a key name that reached past a stop pushed its description to the
// next one - the descriptions did not line up (reported 2026-09-25).
void SettingsWindow::shortcutRow(Page page, const wchar_t* keys, const wchar_t* what) {
    auto& y = y_[static_cast<int>(page)];
    if (rowX_ > 0) {
        y += kButtonHeight + 10;
        rowX_ = 0;
    }
    constexpr int kKeyColumn = 170;
    dimStatics_.push_back(
        add(page, L"STATIC", keys, SS_LEFT | SS_NOPREFIX, 0, 0, kKeyColumn - 8, 18));
    dimStatics_.push_back(add(page, L"STATIC", what, SS_LEFT | SS_NOPREFIX, 0, kKeyColumn, 0, 18));
    y += 18 + 6;
}

void SettingsWindow::buildShortcuts() {
    const Page p = Page::Shortcuts;
    heading(p, L"Luôn");
    shortcutRow(p, L"Ctrl+Shift", L"Đổi Tiếng Việt / English");
    heading(p, L"Khi popup gợi ý đang hiện");
    shortcutRow(p, L"Tab", L"Chèn gợi ý đang chọn (kèm dấu cách)");
    shortcutRow(p, L"↑  ↓", L"Đổi gợi ý");
    shortcutRow(p, L"Enter, 1–5", L"Chọn nhanh (nếu bật trong Gợi ý & Tự sửa)");
    shortcutRow(p, L"Esc", L"Ẩn popup cho tới hết từ đang gõ");
    heading(p, L"Ngay sau một lần tự sửa lỗi");
    shortcutRow(p, L"Backspace, Ctrl+Z", L"Hoàn tác, giữ nguyên chữ bạn đã gõ");
    heading(p, L"Đổi phím tắt");
    note(p,
         L"Bấm vào ô rồi nhấn tổ hợp mới (cần Ctrl/Alt/Shift/Win và một chữ hoặc số). "
         L"Backspace để bỏ gán, Esc để giữ nguyên. Ctrl+Shift (Việt/Anh) cố định.",
         2);
    for (const auto action : core::model::kAllHotkeyActions) {
        auto& y = y_[static_cast<int>(p)];
        add(p, L"STATIC", hotkeyActionLabel(action), SS_LEFT | SS_NOPREFIX, 0, 0, -170, kRowHeight);
        pages_[static_cast<int>(p)].back().y += 4;
        const HWND field = hotkeyField::create(hwnd_, hotkeyFieldId(action), instance_);
        SendMessageW(field, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        Placed placed;
        placed.hwnd = field;
        placed.x = 160;
        placed.y = y;
        placed.w = 160;
        placed.h = kRowHeight;
        placed.rightAligned = true;
        pages_[static_cast<int>(p)].push_back(placed);
        y += kRowHeight + kRowGap + 4;
    }
    button(p, L"Mặc định", kHotkeyDefaults, 120, false);
}

void SettingsWindow::buildAdvanced() {
    const Page p = Page::Advanced;
    heading(p, L"Khởi động");
    check(p, L"Chạy LanKey khi đăng nhập Windows  (hiện trong Task Manager › Startup)",
          kRunAtStartup);
    heading(p, L"Cách gửi phím thay thế vào ứng dụng");
    radio(p, L"Một gói — nhanh  (mặc định)", kSendBatch, true);
    radio(p, L"Từng phím — cho ứng dụng nuốt phím khi nhận nhiều cùng lúc", kSendKeyByKey, false);
    heading(p, L"Số liệu phiên hiện tại");
    auto& y = y_[static_cast<int>(p)];
    add(p, L"STATIC", L"", SS_OWNERDRAW, kStatsText, 0, 0, kStatsCardHeight);
    y += kStatsCardHeight + 12;
    button(p, L"Làm mới số liệu", kRefreshStats, 150, false);
    button(p, L"Mở thư mục dữ liệu", kOpenFolder, 170, true);
    heading(p, L"Tệp cài đặt");
    const std::wstring path = runtime_.dataDir + L"\\settings.json";
    pathNote(p, path.c_str());
    // Saving the file applies it; there is nothing to press afterwards.
    button(p, L"Mở settings.json", kOpenSettingsFile, 150, false);
    button(p, L"Đặt lại mặc định", kResetDefaults, 160, true);
}

void SettingsWindow::buildAbout() {
    const Page p = Page::About;
    constexpr int kLogo = 64;
    const HWND mark = add(p, L"STATIC", L"", SS_ICON | SS_CENTERIMAGE, 0, 0, kLogo, kLogo);
    if (const HICON logo = logoIcon(instance_, px(kLogo)); logo != nullptr) {
        SendMessageW(mark, STM_SETICON, reinterpret_cast<WPARAM>(logo), 0);
    }
    y_[static_cast<int>(p)] += kLogo + 6;
    const std::wstring version = L"Phiên bản " + runtime_.version;
    heading(p, version.c_str());
    note(p,
         L"Bộ gõ tiếng Việt mã nguồn mở học thói quen gõ của bạn — gợi ý cụm từ, dự đoán từ "
         L"tiếp theo và tự sửa lỗi chính tả, hoàn toàn trên máy này.",
         2);
    heading(p, L"Giấy phép và ghi công");
    note(p,
         L"LanKey: GNU GPL v3 trở lên.\r\n"
         L"Engine gõ: OpenKey (GPL-3.0).\r\n"
         L"Từ điển âm tiết: hunspell-vi / Hồ Ngọc Đức (GPL).",
         3);
    heading(p, L"Thư mục dữ liệu");
    pathNote(p, runtime_.dataDir.c_str());
}

// -- layout / paint -------------------------------------------------------------------------------

void SettingsWindow::rebuildFonts() {
    for (const HFONT f : {font_, semibold_, title_, navFont_, statFont_}) {
        if (f != nullptr) DeleteObject(f);
    }
    font_ = createUiFont(dpi_, 10, FW_NORMAL);
    semibold_ = createUiFont(dpi_, 10, FW_SEMIBOLD);
    title_ = createUiFont(dpi_, 16, FW_SEMIBOLD);
    navFont_ = createUiFont(dpi_, 10, FW_NORMAL);
    statFont_ = createUiFont(dpi_, 15, FW_SEMIBOLD);
    if (hwnd_ == nullptr) return;
    const auto setFont = [](HWND h, HFONT f) {
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE);
    };
    for (const auto& page : pages_) {
        for (const Placed& p : page)
            setFont(p.hwnd, font_);
    }
    for (const HWND h : headings_)
        setFont(h, semibold_);
    for (const HWND h : titles_)
        setFont(h, title_);
    setFont(nav_, navFont_);
    SendMessageW(nav_, LB_SETITEMHEIGHT, 0, static_cast<LPARAM>(px(kNavItemHeight)));
}

void SettingsWindow::onDpiChanged(UINT dpi, const RECT& suggested) {
    // Per-monitor v2: Windows only tells us; fonts, item heights and every control
    // rectangle are ours to redo at the new scale.
    dpi_ = dpi;
    rebuildFonts();
    SetWindowPos(hwnd_, nullptr, suggested.left, suggested.top, suggested.right - suggested.left,
                 suggested.bottom - suggested.top, SWP_NOZORDER | SWP_NOACTIVATE);
    fitDictionaryColumns();
    updateScrollBar();
    layout();
    // Every control, erased, now - and once more after the move settles: Windows shows
    // the old surface stretched until the window repaints, and a control that only
    // repainted mid-transition leaves fragments of its previous text behind.
    RedrawWindow(hwnd_, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    PostMessageW(hwnd_, kMsgRedraw, 0, 0);
}

int SettingsWindow::pageContentHeight(Page page) const {
    int bottom = 0;
    for (const Placed& p : pages_[static_cast<int>(page)]) {
        if (p.stretch || p.y < 0) return 0; // laid out against the window height
        bottom = (std::max)(bottom, p.y + p.h);
    }
    return bottom + kContentMargin; // breathing room under the last control
}

void SettingsWindow::updateScrollBar() {
    const RECT content = contentRect();
    const int view = content.bottom - content.top;
    const int needed = px(pageContentHeight(current_));
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    if (needed <= view || view <= 0) {
        scrollY_ = 0;
        si.nMin = 0;
        si.nMax = 0;
        si.nPage = 1;
        si.nPos = 0;
        SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
        ShowScrollBar(hwnd_, SB_VERT, FALSE);
        return;
    }
    ShowScrollBar(hwnd_, SB_VERT, TRUE);
    scrollY_ = std::clamp(scrollY_, 0, needed - view);
    si.nMin = 0;
    si.nMax = needed - 1;
    si.nPage = static_cast<UINT>(view);
    si.nPos = scrollY_;
    SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
}

RECT SettingsWindow::scrollArea() const {
    RECT client{};
    GetClientRect(hwnd_, &client);
    // Everything right of the rail's divider. The rail must stay outside: repainting it on
    // every scroll step makes its owner-drawn items flicker.
    return RECT{px(kNavWidth), 0, client.right, client.bottom};
}

void SettingsWindow::scrollTo(int offsetPx) {
    const RECT content = contentRect();
    const int view = content.bottom - content.top;
    const int needed = px(pageContentHeight(current_));
    const int clamped = needed <= view ? 0 : std::clamp(offsetPx, 0, needed - view);
    if (clamped == scrollY_) return;
    scrollY_ = clamped;
    SetScrollPos(hwnd_, SB_VERT, scrollY_, TRUE);
    layout();
    // Only the content area: repainting the whole window (what this used to do) erases and
    // redraws the navigation rail on every wheel notch, which reads as flicker.
    // ScrollWindowEx(SW_SCROLLCHILDREN) would be the classic alternative, but it leaves
    // the controls where they are in this window - measured, not assumed.
    // The chrome (the hairline under every heading) is drawn from live control rectangles,
    // so the strip has to be repainted when the controls move. The CONTROLS do not: their
    // pixels came along with them above. RDW_ALLCHILDREN would repaint all of them anyway,
    // on every notch - measured 2026-09-25 at 1.95 ms of CPU per step.
    const RECT area = scrollArea();
    RedrawWindow(hwnd_, &area, nullptr, RDW_INVALIDATE | RDW_ERASE);
}

// The three right-hand columns keep their width; "Cụm từ" takes what is left, so the
// last header is never cut off ("Trạ...") at any window size or scale.
void SettingsWindow::fitDictionaryColumns() {
    const HWND list = GetDlgItem(hwnd_, kDictList);
    if (list == nullptr) return;
    RECT r{};
    GetClientRect(list, &r);
    int fixed = 0;
    for (int c = 1; c < 4; ++c) {
        ListView_SetColumnWidth(list, c, px(kDictColumnWidths[c]));
        fixed += px(kDictColumnWidths[c]);
    }
    const int scrollbar = static_cast<int>(GetSystemMetricsForDpi(SM_CXVSCROLL, dpi_));
    const int available = static_cast<int>(r.right - r.left) - fixed - scrollbar;
    ListView_SetColumnWidth(list, 0, (std::max)(px(120), available));
}

void SettingsWindow::layout() {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const RECT content = contentRect();
    const int width = content.right - content.left;
    const int height = content.bottom - content.top;

    // One deferred batch for every control, committed in a single pass. Moving them one
    // MoveWindow at a time repaints each control where it lands while its neighbours are
    // still at the old offset, which is what a scroll looked like: a smear of half-moved
    // rows. SWP_NOCOPYBITS stops Windows blitting stale pixels into the new position.
    // Not SWP_NOREDRAW: it also leaves the ground a control moved off un-invalidated.
    std::size_t count = 1;
    for (const auto& page : pages_)
        count += page.size();
    HDWP dwp = BeginDeferWindowPos(static_cast<int>(count));
    // No SWP_NOCOPYBITS: with it, every control that moves is repainted from scratch
    // instead of having its pixels blitted to the new position - one full redraw of the
    // page per wheel notch, which is what the flicker was.
    constexpr UINT kFlags = SWP_NOZORDER | SWP_NOACTIVATE;
    const auto place = [&](HWND h, int x, int y, int w, int wh) {
        if (dwp != nullptr)
            dwp = DeferWindowPos(dwp, h, nullptr, x, y, w, wh, kFlags);
        else
            MoveWindow(h, x, y, w, wh, TRUE); // the batch failed: still lay out correctly
    };
    place(nav_, 0, px(kNavTop), px(kNavWidth), client.bottom - px(kNavTop));
    for (auto& page : pages_) {
        for (const Placed& p : page) {
            const int x = p.rightAligned ? width - px(p.x) : px(p.x);
            int w = px(p.w);
            if (p.fullWidth) w = width - px(p.x);
            if (p.w < 0) w = width - px(p.x) - px(-p.w);
            const int y = p.y < 0 ? height - px(p.h) : px(p.y) - scrollY_;
            int h = px(p.h);
            if (p.stretch) h = height - y;
            place(p.hwnd, content.left + x, content.top + y, w, h);
        }
    }
    if (dwp != nullptr) EndDeferWindowPos(dwp);
    fitDictionaryColumns();
}

void SettingsWindow::paintChrome(HDC dc, const RECT& client) {
    const RECT nav{0, 0, px(kNavWidth), client.bottom};
    fillRect(dc, nav, kNavBg);
    const RECT content{px(kNavWidth), 0, client.right, client.bottom};
    fillRect(dc, content, kContentBg);
    const RECT divider{px(kNavWidth) - 1, 0, px(kNavWidth), client.bottom};
    fillRect(dc, divider, kHairline);

    // Product mark: the logo and the name, above the navigation.
    const int tile = px(32);
    const RECT tileRect{px(20), px(20), px(20) + tile, px(20) + tile};
    if (const HICON logo = logoIcon(instance_, tile); logo != nullptr) {
        DrawIconEx(dc, tileRect.left, tileRect.top, logo, tile, tile, 0, nullptr, DI_NORMAL);
    } else {
        const HBRUSH fill = CreateSolidBrush(kAccent);
        RECT r = tileRect;
        FillRect(dc, &r, fill);
        DeleteObject(fill);
    }
    SetBkMode(dc, TRANSPARENT);
    const auto old = static_cast<HFONT>(SelectObject(dc, title_));
    SetTextColor(dc, kText);
    RECT name{tileRect.right + px(12), tileRect.top - px(4), px(kNavWidth),
              tileRect.bottom + px(4)};
    DrawTextW(dc, L"LanKey", -1, &name, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);

    // Hairlines under the section headings of the current page.
    const RECT area = contentRect();
    for (const HWND h : headings_) {
        if (!IsWindowVisible(h)) continue;
        RECT r{};
        GetWindowRect(h, &r);
        MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&r), 2);
        const RECT line{r.left, r.bottom + px(4), area.right, r.bottom + px(5)};
        fillRect(dc, line, kHairline);
    }
}

void SettingsWindow::drawNavItem(const DRAWITEMSTRUCT& item) {
    if (item.itemID >= static_cast<UINT>(kPageCount)) return;
    const bool selected = (item.itemState & ODS_SELECTED) != 0;
    const RECT r = item.rcItem;
    fillRect(item.hDC, r, kNavBg);
    if (selected) {
        const RECT pill{r.left + px(10), r.top + px(4), r.right - px(10), r.bottom - px(4)};
        fillRect(item.hDC, pill, kTint);
        const RECT bar{pill.left, pill.top + px(8), pill.left + px(3), pill.bottom - px(8)};
        fillRect(item.hDC, bar, kAccent);
    }
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, selected ? kAccent : kText);
    const auto old = static_cast<HFONT>(SelectObject(item.hDC, selected ? semibold_ : navFont_));
    RECT text{r.left + px(26), r.top, r.right - px(10), r.bottom};
    DrawTextW(item.hDC, kPageTitles[item.itemID], -1, &text,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(item.hDC, old);
}

void SettingsWindow::selectPage(Page page) {
    current_ = page;
    scrollY_ = 0;
    for (int p = 0; p < kPageCount; ++p) {
        for (const Placed& c : pages_[p])
            ShowWindow(c.hwnd, p == static_cast<int>(page) ? SW_SHOW : SW_HIDE);
    }
    SendMessageW(nav_, LB_SETCURSEL, static_cast<WPARAM>(static_cast<int>(page)), 0);
    updateScrollBar();
    layout();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

// -- settings <-> controls
// -------------------------------------------------------------------------

void SettingsWindow::loadControls() {
    loading_ = true;
    const auto tick = [&](int id, bool on) { setChecked(id, on); };
    const auto& s = settings_;
    tick(kTelex, s.engine.inputMethod == InputMethod::Telex);
    tick(kVni, s.engine.inputMethod == InputMethod::Vni);
    tick(kSimpleTelex, s.engine.inputMethod == InputMethod::SimpleTelex);
    tick(kCustomMethod, s.engine.inputMethod == InputMethod::Custom);
    showCustomKeys();
    tick(kCodeUnicode, s.engine.codeTable == CodeTable::Unicode);
    tick(kCodeCompound, s.engine.codeTable == CodeTable::UnicodeCompound);
    tick(kCodeTcvn3, s.engine.codeTable == CodeTable::Tcvn3);
    tick(kCodeVni, s.engine.codeTable == CodeTable::VniWindows);
    tick(kVietnameseOn, s.vietnameseEnabled);
    tick(kModernTone, s.engine.modernToneMark);
    tick(kSpellCheck, s.engine.spellCheck);
    tick(kQuickTelex, s.engine.quickTelex);
    tick(kRememberPerApp, s.languageMemory.enabled);

    tick(kSuggestionsOn, s.suggestions.enabled);
    SetDlgItemInt(hwnd_, kIdleDelay, static_cast<UINT>(s.suggestions.idleDelayMs), FALSE);
    SetDlgItemInt(hwnd_, kMinPrefix, static_cast<UINT>(s.suggestions.minPrefixLength), FALSE);
    tick(kDigits, s.suggestions.selectWithDigits);
    tick(kSelectWithEnter, s.suggestions.selectWithEnter);
    tick(kLevelOff, s.autoCorrect.level == AutoCorrectLevel::Off);
    tick(kLevelCautious, s.autoCorrect.level == AutoCorrectLevel::Cautious);
    tick(kLevelBalanced, s.autoCorrect.level == AutoCorrectLevel::Balanced);
    tick(kLevelAggressive, s.autoCorrect.level == AutoCorrectLevel::Aggressive);

    SetWindowTextW(GetDlgItem(hwnd_, kExcludedApps), lines(s.privacy.excludedApps).c_str());
    SetWindowTextW(GetDlgItem(hwnd_, kSuggestionsOffApps),
                   lines(s.privacy.suggestionsDisabledApps).c_str());
    SetWindowTextW(GetDlgItem(hwnd_, kAutoCorrectOffApps),
                   lines(s.autoCorrect.excludedApps).c_str());

    tick(kRunAtStartup, runtime_.runAtStartup);
    tick(kSendBatch, s.advanced.sendKeys == SendKeysMode::Batch);
    tick(kSendKeyByKey, s.advanced.sendKeys == SendKeysMode::KeyByKey);
    for (const auto action : core::model::kAllHotkeyActions) {
        hotkeyField::set(GetDlgItem(hwnd_, hotkeyFieldId(action)), s.hotkeys[action]);
    }
    loading_ = false;
}

void SettingsWindow::applyFromControls() {
    if (loading_) return;
    const auto checked = [&](int id) { return isChecked(id); };
    Settings s = settings_;
    if (checked(kTelex)) s.engine.inputMethod = InputMethod::Telex;
    if (checked(kVni)) s.engine.inputMethod = InputMethod::Vni;
    if (checked(kSimpleTelex)) s.engine.inputMethod = InputMethod::SimpleTelex;
    if (checked(kCustomMethod)) s.engine.inputMethod = InputMethod::Custom;
    if (checked(kCodeUnicode)) s.engine.codeTable = CodeTable::Unicode;
    if (checked(kCodeCompound)) s.engine.codeTable = CodeTable::UnicodeCompound;
    if (checked(kCodeTcvn3)) s.engine.codeTable = CodeTable::Tcvn3;
    if (checked(kCodeVni)) s.engine.codeTable = CodeTable::VniWindows;
    s.vietnameseEnabled = checked(kVietnameseOn);
    s.engine.modernToneMark = checked(kModernTone);
    s.engine.spellCheck = checked(kSpellCheck);
    s.engine.quickTelex = checked(kQuickTelex);
    s.languageMemory.enabled = checked(kRememberPerApp);

    s.suggestions.enabled = checked(kSuggestionsOn);
    s.suggestions.idleDelayMs =
        std::clamp(intOf(GetDlgItem(hwnd_, kIdleDelay), s.suggestions.idleDelayMs), 0, 5000);
    s.suggestions.minPrefixLength =
        std::clamp(intOf(GetDlgItem(hwnd_, kMinPrefix), s.suggestions.minPrefixLength), 1, 4);
    s.suggestions.selectWithDigits = checked(kDigits);
    s.suggestions.selectWithEnter = checked(kSelectWithEnter);
    if (checked(kLevelOff)) s.autoCorrect.level = AutoCorrectLevel::Off;
    if (checked(kLevelCautious)) s.autoCorrect.level = AutoCorrectLevel::Cautious;
    if (checked(kLevelBalanced)) s.autoCorrect.level = AutoCorrectLevel::Balanced;
    if (checked(kLevelAggressive)) s.autoCorrect.level = AutoCorrectLevel::Aggressive;

    s.advanced.sendKeys = checked(kSendKeyByKey) ? SendKeysMode::KeyByKey : SendKeysMode::Batch;

    if (s == settings_) return;
    settings_ = s;
    if (callbacks_.onApply) callbacks_.onApply(settings_);
}

void SettingsWindow::applyHotkeys(int changedId) {
    // Filling a field with SetWindowTextW makes the EDIT send EN_CHANGE just like a
    // keystroke does; acting on those would read half-filled fields and overwrite the
    // settings with them.
    if (loading_) return;
    using core::model::HotkeyAction;
    core::model::HotkeySettings next;
    for (const auto action : core::model::kAllHotkeyActions) {
        next[action] = hotkeyField::get(GetDlgItem(hwnd_, hotkeyFieldId(action)));
    }
    const auto changed = static_cast<HotkeyAction>(changedId - kHotkeyFirst);
    const core::model::Hotkey& value = next[changed];
    if (value.assigned()) {
        for (const auto other : core::model::kAllHotkeyActions) {
            if (other != changed && next[other] == value) {
                const std::wstring text =
                    L"Phím tắt " + platform::win32::fromUtf8(core::model::formatHotkey(value)) +
                    L" đã dùng cho: " + hotkeyActionLabel(other) + L".";
                MessageBoxW(hwnd_, text.c_str(), L"LanKey", MB_ICONINFORMATION | MB_OK);
                loading_ = true;
                hotkeyField::set(GetDlgItem(hwnd_, changedId), settings_.hotkeys[changed]);
                loading_ = false;
                return;
            }
        }
    }
    if (next == settings_.hotkeys) return;
    settings_.hotkeys = next;
    if (callbacks_.onApply) callbacks_.onApply(settings_);
}

void SettingsWindow::showCustomKeys() {
    // "Phím:  S  F  R  X  J  A  O  E  W[  D  Z" - one group per function.
    std::wstring text = L"Phím:";
    std::wstring group;
    const auto flush = [&] {
        text += group.empty() ? L"  –" : L"  " + group;
        group.clear();
    };
    for (const char c : settings_.engine.customKeys) {
        if (c == ',') {
            flush();
        } else {
            group += static_cast<wchar_t>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    flush();
    SetWindowTextW(GetDlgItem(hwnd_, kCustomKeys), text.c_str());
}

void SettingsWindow::defineCustomKeys() {
    customMethod_.show(instance_, hwnd_, settings_.engine.customKeys,
                       [this](const std::string& keys) {
                           Settings s = settings_;
                           s.engine.customKeys = keys;
                           s.engine.inputMethod = InputMethod::Custom;
                           if (s == settings_) return;
                           settings_ = s;
                           setChecked(kCustomMethod, true);
                           showCustomKeys();
                           if (callbacks_.onApply) callbacks_.onApply(settings_);
                       });
}

void SettingsWindow::applyAppLists() {
    if (hwnd_ == nullptr) return;
    Settings s = settings_;
    s.privacy.excludedApps = fromLines(GetDlgItem(hwnd_, kExcludedApps));
    s.privacy.suggestionsDisabledApps = fromLines(GetDlgItem(hwnd_, kSuggestionsOffApps));
    s.autoCorrect.excludedApps = fromLines(GetDlgItem(hwnd_, kAutoCorrectOffApps));
    if (s == settings_) return;
    settings_ = s;
    if (callbacks_.onApply) callbacks_.onApply(settings_);
}

// -- dictionary -----------------------------------------------------------------------------------

void SettingsWindow::fillDictionaryList() {
    const HWND list = GetDlgItem(hwnd_, kDictList);
    if (list == nullptr) return;
    wchar_t filterBuf[128] = {};
    GetDlgItemTextW(hwnd_, kSearch, filterBuf, 127);
    std::wstring filter(filterBuf);
    for (auto& c : filter)
        c = static_cast<wchar_t>(towlower(c));

    // Only the index of the matching rows is rebuilt; the list view is told how many there
    // are and asks for the text of the ones it paints (LVN_GETDISPINFO below).
    dictionaryIndex_.clear();
    dictionaryIndex_.reserve(dictionary_.size());
    for (std::size_t i = 0; i < dictionary_.size(); ++i) {
        if (!filter.empty() && folded_[i].find(filter) == std::wstring::npos) continue;
        dictionaryIndex_.push_back(static_cast<int>(i));
    }
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED); // the old selection means nothing now
    SendMessageW(list, LVM_SETITEMCOUNT, static_cast<WPARAM>(dictionaryIndex_.size()),
                 LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
    InvalidateRect(list, nullptr, TRUE);
    updateDictionaryStatus();
}

void SettingsWindow::updateDictionaryStatus() {
    if (hwnd_ == nullptr) return;
    if (dictionaryLoading_) {
        SetDlgItemTextW(hwnd_, kDictStatus, L"\u0110ang t\u1ea3i\u2026");
        return;
    }
    // Counts only. With the hint appended this never fitted beside the search box and was
    // shown cut off mid-word; the hint has a line of its own under it now.
    const bool filtered = dictionaryIndex_.size() != dictionary_.size();
    std::wstring status = std::to_wstring(dictionaryIndex_.size());
    if (filtered) status += L" / " + std::to_wstring(dictionary_.size());
    status += L" c\u1ee5m t\u1eeb";
    SetDlgItemTextW(hwnd_, kDictStatus, status.c_str());
}

// The text of one cell, on demand. Called only for rows the list view is about to paint.
void SettingsWindow::dictionaryDispInfo(tagLVDISPINFOW& info) {
    if ((info.item.mask & LVIF_TEXT) == 0 || info.item.pszText == nullptr) return;
    info.item.pszText[0] = 0;
    const auto row = static_cast<std::size_t>(info.item.iItem);
    if (info.item.iItem < 0 || row >= dictionaryIndex_.size()) return;
    const auto& e = dictionary_[static_cast<std::size_t>(dictionaryIndex_[row])];
    switch (info.item.iSubItem) {
    case 0:
        dispBuf_ = e.phrase;
        break;
    case 1:
        dispBuf_ = std::to_wstring(e.frequency);
        break;
    case 2:
        dispBuf_ = dateOf(e.lastUsedAt);
        break;
    case 3:
        dispBuf_.clear();
        if (e.pinned) dispBuf_ += L"ghim";
        if (e.blocked) dispBuf_ += dispBuf_.empty() ? L"ch\u1eb7n" : L", ch\u1eb7n";
        break;
    default:
        return;
    }
    // The list view copies out of this buffer before the next request, so one member is
    // enough - and it is the whole point: no per-row allocation while scrolling.
    wcsncpy_s(info.item.pszText, static_cast<std::size_t>(info.item.cchTextMax), dispBuf_.c_str(),
              _TRUNCATE);
}

std::vector<std::wstring> SettingsWindow::selectedPhrases() const {
    std::vector<std::wstring> out;
    const HWND list = GetDlgItem(hwnd_, kDictList);
    int row = -1;
    while ((row = ListView_GetNextItem(list, row, LVNI_SELECTED)) != -1) {
        if (row >= 0 && static_cast<std::size_t>(row) < dictionaryIndex_.size()) {
            const auto index =
                static_cast<std::size_t>(dictionaryIndex_[static_cast<std::size_t>(row)]);
            out.push_back(dictionary_[index].phrase);
        }
    }
    return out;
}

// -- messages -------------------------------------------------------------------------------------

LRESULT CALLBACK SettingsWindow::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* self = static_cast<SettingsWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) return DefWindowProcW(hwnd, msg, wParam, lParam);
    return self->handle(msg, wParam, lParam);
}

LRESULT SettingsWindow::handle(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        const HDC dc = BeginPaint(hwnd_, &ps);
        RECT client{};
        GetClientRect(hwnd_, &client);
        paintChrome(dc, client);
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_ERASEBKGND: {
        // Cheap flat fill so a resize never shows unpainted (black) client area even when
        // the WM_PAINT that follows is clipped; paintChrome() draws the details. GDI clips
        // to the update region, so scrolling (which only invalidates the content strip)
        // does not repaint the rail.
        RECT client{};
        GetClientRect(hwnd_, &client);
        const HDC dc = reinterpret_cast<HDC>(wParam);
        RECT clip{};
        const bool haveClip = GetClipBox(dc, &clip) != NULLREGION;
        if (!haveClip || clip.left < px(kNavWidth)) {
            fillRect(dc, RECT{0, 0, px(kNavWidth), client.bottom}, kNavBg);
        }
        fillRect(dc, RECT{px(kNavWidth), 0, client.right, client.bottom}, kContentBg);
        return 1;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        const HDC dc = reinterpret_cast<HDC>(wParam);
        const HWND ctl = reinterpret_cast<HWND>(lParam);
        SetBkMode(dc, TRANSPARENT);
        const bool dim =
            std::find(dimStatics_.begin(), dimStatics_.end(), ctl) != dimStatics_.end();
        SetTextColor(dc, dim ? kDim : kText);
        return reinterpret_cast<LRESULT>(contentBrush_);
    }
    case WM_CTLCOLORLISTBOX:
        if (reinterpret_cast<HWND>(lParam) == nav_) {
            // The list must erase itself: a hollow brush leaves whatever was on screen
            // (black after a resize) below the last item.
            return reinterpret_cast<LRESULT>(navBrush_);
        }
        break;
    case WM_MEASUREITEM: {
        auto* mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (mi->CtlID == kNav) {
            mi->itemHeight = static_cast<UINT>(px(kNavItemHeight));
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        const auto* di = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (di->CtlID == kNav) {
            drawNavItem(*di);
            return TRUE;
        }
        if (di->CtlID == kStatsText) {
            drawStats(*di);
            return TRUE;
        }
        if (di->CtlType == ODT_BUTTON) {
            drawToggle(*di);
            return TRUE;
        }
        break;
    }
    case WM_NOTIFY: {
        const auto* header = reinterpret_cast<const NMHDR*>(lParam);
        if (header != nullptr && header->idFrom == kDictList && header->code == LVN_GETDISPINFOW) {
            dictionaryDispInfo(*reinterpret_cast<NMLVDISPINFOW*>(lParam));
            return 0;
        }
        break;
    }
    case WM_LBUTTONDOWN:
        // Plain Win32 leaves the focus where it was when the background is clicked, so the
        // search box kept its caret for ever. Taking focus here is what the user means.
        SetFocus(hwnd_);
        return 0;
    case WM_SETCURSOR: {
        // Everything clickable gets the hand: navigation items, buttons, toggles.
        const auto over = reinterpret_cast<HWND>(wParam);
        if (over == nav_) {
            POINT pt{};
            GetCursorPos(&pt);
            ScreenToClient(nav_, &pt);
            const LRESULT hit = SendMessageW(nav_, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
            if (HIWORD(hit) == 0) {
                showHandCursor();
                return TRUE;
            }
        } else if (over != nullptr && over != hwnd_ && IsWindowEnabled(over)) {
            wchar_t cls[16] = {};
            GetClassNameW(over, cls, 15);
            if (_wcsicmp(cls, L"Button") == 0) {
                showHandCursor();
                return TRUE;
            }
        }
        break;
    }
    case WM_DPICHANGED:
        onDpiChanged(HIWORD(wParam), *reinterpret_cast<const RECT*>(lParam));
        return 0;
    case kMsgRedraw:
        RedrawWindow(hwnd_, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        return 0;
    case WM_SIZE:
        // The rect Windows applies while a window is dragged across monitors can land
        // after WM_DPICHANGED returns: lay out on the size that actually took effect.
        if (nav_ != nullptr && wParam != SIZE_MINIMIZED) {
            updateScrollBar();
            layout();
            InvalidateRect(hwnd_, nullptr, TRUE);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        // The pages are laid out at fixed 96-dpi coordinates with no horizontal reflow, so
        // the window never gets narrower than it was designed for (labels and the
        // right-aligned buttons would be cut off); the height may shrink freely because
        // the content scrolls.
        auto* mm = reinterpret_cast<MINMAXINFO*>(lParam);
        mm->ptMinTrackSize.x = px(kWidth) + GetSystemMetricsForDpi(SM_CXVSCROLL, dpi_);
        mm->ptMinTrackSize.y = px(360);
        return 0;
    }
    case WM_VSCROLL: {
        const int line = px(24);
        const RECT content = contentRect();
        const int page = (std::max)(line, static_cast<int>(content.bottom - content.top) - line);
        SCROLLINFO si{};
        si.cbSize = sizeof(si);
        si.fMask = SIF_TRACKPOS;
        GetScrollInfo(hwnd_, SB_VERT, &si);
        switch (LOWORD(wParam)) {
        case SB_LINEUP:
            scrollTo(scrollY_ - line);
            break;
        case SB_LINEDOWN:
            scrollTo(scrollY_ + line);
            break;
        case SB_PAGEUP:
            scrollTo(scrollY_ - page);
            break;
        case SB_PAGEDOWN:
            scrollTo(scrollY_ + page);
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            scrollTo(si.nTrackPos);
            break;
        case SB_TOP:
            scrollTo(0);
            break;
        case SB_BOTTOM:
            scrollTo(INT_MAX);
            break;
        default:
            break;
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        UINT lines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        if (lines == 0) lines = 3;
        scrollTo(scrollY_ - delta * px(24) * static_cast<int>(lines) / WHEEL_DELTA);
        return 0;
    }
    case WM_COMMAND:
        onCommand(LOWORD(wParam), HIWORD(wParam), reinterpret_cast<HWND>(lParam));
        return 0;
    case WM_CLOSE:
        if (callbacks_.onHotkeyCapture) callbacks_.onHotkeyCapture(false);
        applyAppLists(); // the one page that applies on demand: do not lose edits on close
        ShowWindow(hwnd_, SW_HIDE);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

void SettingsWindow::onCommand(int id, int code, HWND from) {
    if (Toggle* t = toggleFor(from); t != nullptr && code == BN_CLICKED) {
        if (t->radio) {
            setChecked(id, true);
        } else {
            setChecked(id, !t->checked);
        }
    }
    switch (id) {
    case IDCANCEL: // Esc through IsDialogMessage: nothing to cancel, keep the window
        return;
    case kDefineKeys:
        defineCustomKeys();
        return;
    case kHotkeyConvertLanguage:
    case kHotkeyConvertWidth:
    case kHotkeyClipboard:
    case kHotkeySnippet:
        if (code == EN_CHANGE) applyHotkeys(id);
        if (code == EN_SETFOCUS && callbacks_.onHotkeyCapture) callbacks_.onHotkeyCapture(true);
        if (code == EN_KILLFOCUS && callbacks_.onHotkeyCapture) callbacks_.onHotkeyCapture(false);
        return;
    case kHotkeyDefaults: {
        core::model::HotkeySettings defaults;
        if (defaults == settings_.hotkeys) return;
        settings_.hotkeys = defaults;
        loading_ = true;
        for (const auto action : core::model::kAllHotkeyActions) {
            hotkeyField::set(GetDlgItem(hwnd_, hotkeyFieldId(action)), defaults[action]);
        }
        loading_ = false;
        if (callbacks_.onApply) callbacks_.onApply(settings_);
        return;
    }
    case kNav:
        if (code == LBN_SELCHANGE) {
            const auto sel = static_cast<int>(SendMessageW(nav_, LB_GETCURSEL, 0, 0));
            if (sel >= 0 && sel < kPageCount) selectPage(static_cast<Page>(sel));
        }
        return;
    case kSearch:
        if (code == EN_CHANGE) fillDictionaryList();
        return;
    case kIdleDelay:
    case kMinPrefix:
        if (code == EN_KILLFOCUS) applyFromControls();
        return;
    case kOpenSettingsFile:
        if (callbacks_.onOpenSettingsFile) callbacks_.onOpenSettingsFile();
        return;
    case kResetDefaults:
        if (MessageBoxW(hwnd_,
                        L"Đưa mọi cài đặt về mặc định?\n\nTừ điển cá nhân và dữ liệu học được "
                        L"giữ nguyên; chỉ các tuỳ chọn trong cửa sổ này được đặt lại.",
                        L"LanKey", MB_ICONQUESTION | MB_OKCANCEL | MB_DEFBUTTON2) == IDOK &&
            callbacks_.onResetDefaults) {
            callbacks_.onResetDefaults();
        }
        return;
    case kExcludedApps:
    case kSuggestionsOffApps:
    case kAutoCorrectOffApps:
        return; // applied by the button / on close
    case kApplyLists:
        applyAppLists();
        return;
    case kRefreshRecent:
    case kRefreshStats:
        if (callbacks_.onRefreshStats) callbacks_.onRefreshStats();
        return;
    case kReloadDict:
        if (callbacks_.onLoadDictionary) {
            dictionaryLoading_ = true;
            updateDictionaryStatus();
            callbacks_.onLoadDictionary();
        }
        return;
    case kRemove:
        if (auto sel = selectedPhrases(); !sel.empty() && callbacks_.onRemoveEntries) {
            callbacks_.onRemoveEntries(std::move(sel));
        }
        return;
    case kBlock:
    case kUnblock:
        if (auto sel = selectedPhrases(); !sel.empty() && callbacks_.onSetBlocked) {
            callbacks_.onSetBlocked(std::move(sel), id == kBlock);
        }
        return;
    case kPin:
    case kUnpin:
        if (auto sel = selectedPhrases(); !sel.empty() && callbacks_.onSetPinned) {
            callbacks_.onSetPinned(std::move(sel), id == kPin);
        }
        return;
    case kExport: {
        wchar_t path[MAX_PATH] = L"lankey-tu-dien.txt";
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd_;
        ofn.lpstrFilter = L"Văn bản (*.txt)\0*.txt\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = L"txt";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (GetSaveFileNameW(&ofn) && callbacks_.onExport) callbacks_.onExport(path);
        return;
    }
    case kEraseAll:
        if (callbacks_.onEraseAll) callbacks_.onEraseAll();
        return;
    case kShowPolicy:
        if (callbacks_.onShowDataPolicy) callbacks_.onShowDataPolicy();
        return;
    case kOpenFolder:
        if (callbacks_.onOpenDataFolder) callbacks_.onOpenDataFolder();
        return;
    case kRunAtStartup:
        runtime_.runAtStartup = isChecked(kRunAtStartup);
        if (callbacks_.onRunAtStartup) callbacks_.onRunAtStartup(runtime_.runAtStartup);
        return;
    default:
        break;
    }
    if (code == BN_CLICKED) applyFromControls();
}

} // namespace lankey::ui::win32
