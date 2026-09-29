#include "ui/win32/SnippetPicker.h"

#include <algorithm>
#include <commctrl.h>

#include "ui/win32/Theme.h"

namespace lankey::ui::win32 {

namespace {

constexpr wchar_t kClassName[] = L"LanKeySnippetPicker";
constexpr int kIdSearch = 1;
constexpr int kIdList = 2;
constexpr UINT_PTR kSearchSubclass = 1;
// 96-dpi pixels. The window is as tall as its contents: field, up to kMaxRows rows, and
// the key hints. Anything taller would be empty card.
constexpr int kWidth = 560;
constexpr int kPad = 16; // text inset, left and right
constexpr int kFieldHeight = 46;
constexpr int kEditHeight = 24; // the text box itself, centred in the field area
constexpr int kRowHeight = 32;
constexpr int kMaxRows = 8;
constexpr int kEmptyHeight = 40; // the "nothing matches" strip
constexpr int kFooterHeight = 32;
constexpr int kAbbrColumn = 150;

std::wstring lowered(std::wstring s) {
    for (auto& c : s)
        c = static_cast<wchar_t>(towlower(c));
    return s;
}

} // namespace

SnippetPicker::~SnippetPicker() {
    destroy();
}

bool SnippetPicker::visible() const noexcept {
    return hwnd_ != nullptr && IsWindowVisible(hwnd_);
}

void SnippetPicker::show(HINSTANCE instance, std::vector<Item> items, Callbacks callbacks) {
    callbacks_ = std::move(callbacks);
    items_ = std::move(items);
    // Whoever was in front is where the text has to end up. Read before the window opens:
    // once it does, the foreground is us.
    const HWND previous = GetForegroundWindow();

    if (hwnd_ == nullptr) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = &SnippetPicker::wndProc;
        wc.hInstance = instance;
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&wc);

        const UINT dpi = GetDpiForSystem();
        const auto px = [dpi](int v) { return MulDiv(v, static_cast<int>(dpi), 96); };
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const int w = px(kWidth);
        // A third of the way down rather than centred: a palette that covers what you were
        // reading is a palette you close to read again. The height follows the contents,
        // so it is set by resizeToContent() below.
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kClassName, L"LanKey — Gõ tắt",
                                WS_POPUP, (work.left + work.right - w) / 2,
                                work.top + (work.bottom - work.top) / 4, w, px(kFieldHeight),
                                nullptr, nullptr, instance, this);
        if (hwnd_ == nullptr) return;
        font_ = createUiFont(dpi, 10, FW_NORMAL);
        bold_ = createUiFont(dpi, 10, FW_SEMIBOLD);
        large_ = createUiFont(dpi, 12, FW_NORMAL);
        background_ = CreateSolidBrush(kPopupPalette.background);
        applyRoundedCorners(hwnd_);
        setBorderColor(hwnd_, kPopupPalette.border);

        search_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0,
                                  0, hwnd_, reinterpret_cast<HMENU>(kIdSearch), instance, nullptr);
        list_ =
            CreateWindowExW(0, L"LISTBOX", L"",
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED |
                                LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
                            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kIdList), instance, nullptr);
        SendMessageW(search_, WM_SETFONT, reinterpret_cast<WPARAM>(large_), TRUE);
        // An empty dark box tells the user nothing about what to do with it.
        SendMessageW(search_, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(L"Tìm theo viết tắt hoặc nội dung…"));
        SendMessageW(list_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        SendMessageW(list_, LB_SETITEMHEIGHT, 0, MAKELPARAM(px(kRowHeight), 0));
        // The arrows and Enter belong to the list while the caret is in the search box:
        // one field, one list, no Tab between them.
        SetWindowSubclass(search_, &SnippetPicker::searchProc, kSearchSubclass,
                          reinterpret_cast<DWORD_PTR>(this));
    }
    target_ = previous;
    SetWindowTextW(search_, L"");
    refilter();
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    SetFocus(search_);
}

void SnippetPicker::hide() {
    if (hwnd_ != nullptr) ShowWindow(hwnd_, SW_HIDE);
}

void SnippetPicker::destroy() {
    if (search_ != nullptr)
        RemoveWindowSubclass(search_, &SnippetPicker::searchProc, kSearchSubclass);
    if (hwnd_ != nullptr) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    search_ = nullptr;
    list_ = nullptr;
    for (const HFONT f : {font_, bold_, large_}) {
        if (f != nullptr) DeleteObject(f);
    }
    font_ = bold_ = large_ = nullptr;
    if (background_ != nullptr) DeleteObject(background_);
    background_ = nullptr;
}

void SnippetPicker::resizeToContent() {
    if (hwnd_ == nullptr) return;
    const UINT dpi = GetDpiForWindow(hwnd_);
    const auto px = [dpi](int v) { return MulDiv(v, static_cast<int>(dpi), 96); };

    const int rows = (std::min)(static_cast<int>(shown_.size()), kMaxRows);
    const int listH = rows > 0 ? px(kRowHeight) * rows : px(kEmptyHeight);
    const int height = px(kFieldHeight) + 1 + listH + 1 + px(kFooterHeight);

    RECT r{};
    GetWindowRect(hwnd_, &r);
    // Grows downwards from where it already is, so the field does not move under the
    // user's eyes while they are typing in it.
    SetWindowPos(hwnd_, nullptr, r.left, r.top, r.right - r.left, height,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    const int width = r.right - r.left;
    // A single-line EDIT puts its text at the top of whatever box it is given, so the box
    // is only as tall as the text and is centred in the field area by hand.
    const int editH = px(kEditHeight);
    MoveWindow(search_, px(kPad), (px(kFieldHeight) - editH) / 2, width - 2 * px(kPad), editH,
               TRUE);
    // Full width, so the selected row reads as a band across the card rather than a
    // floating rectangle; the text inside it is what carries the padding.
    MoveWindow(list_, 0, px(kFieldHeight) + 1, width, rows > 0 ? listH : 0, TRUE);
    ShowWindow(list_, rows > 0 ? SW_SHOW : SW_HIDE);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

// The parts the controls do not cover: a hairline under the field, one above the hints,
// the hints themselves, and the "nothing matched" line when the list is empty.
void SnippetPicker::paintChrome(HDC dc) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const UINT dpi = GetDpiForWindow(hwnd_);
    const auto px = [dpi](int v) { return MulDiv(v, static_cast<int>(dpi), 96); };
    FillRect(dc, &client, background_);

    const HBRUSH line = CreateSolidBrush(kPopupPalette.selection);
    RECT under{0, px(kFieldHeight), client.right, px(kFieldHeight) + 1};
    FillRect(dc, &under, line);
    RECT above{0, client.bottom - px(kFooterHeight) - 1, client.right,
               client.bottom - px(kFooterHeight)};
    FillRect(dc, &above, line);
    DeleteObject(line);

    SetBkMode(dc, TRANSPARENT);
    const auto old = static_cast<HFONT>(SelectObject(dc, font_));
    SetTextColor(dc, kPopupPalette.textDim);
    RECT hints{px(kPad), client.bottom - px(kFooterHeight), client.right - px(kPad), client.bottom};
    DrawTextW(dc, L"↑↓ chọn     Enter chèn     Esc đóng", -1, &hints,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (shown_.empty()) {
        RECT empty{px(kPad), px(kFieldHeight) + 1, client.right - px(kPad),
                   px(kFieldHeight) + 1 + px(kEmptyHeight)};
        DrawTextW(dc, L"Không có đoạn gõ tắt nào khớp", -1, &empty,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(dc, old);
}

void SnippetPicker::refilter() {
    wchar_t buf[128] = {};
    GetWindowTextW(search_, buf, 127);
    const std::wstring filter = lowered(buf);

    shown_.clear();
    shown_.reserve(items_.size());
    for (std::size_t i = 0; i < items_.size(); ++i) {
        // Abbreviation or body: the user remembers one or the other, rarely both.
        if (filter.empty() || lowered(items_[i].abbr).find(filter) != std::wstring::npos ||
            lowered(items_[i].preview).find(filter) != std::wstring::npos) {
            shown_.push_back(static_cast<int>(i));
        }
    }
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    for (const int i : shown_) {
        SendMessageW(list_, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(items_[static_cast<std::size_t>(i)].abbr.c_str()));
    }
    if (!shown_.empty()) SendMessageW(list_, LB_SETCURSEL, 0, 0);
    resizeToContent();
}

void SnippetPicker::move(int delta) {
    const int count = static_cast<int>(shown_.size());
    if (count == 0) return;
    const auto current = static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0));
    const int next = current < 0 ? 0 : (current + delta % count + count) % count;
    SendMessageW(list_, LB_SETCURSEL, static_cast<WPARAM>(next), 0);
}

void SnippetPicker::chooseSelected() {
    const auto row = static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0));
    if (row < 0 || static_cast<std::size_t>(row) >= shown_.size()) return;
    const std::wstring abbr =
        items_[static_cast<std::size_t>(shown_[static_cast<std::size_t>(row)])]
            .abbr; // copied: the callback may reload the list
    // Hand the focus back BEFORE hiding, not after. SetForegroundWindow only obeys a
    // process that already owns the foreground, and hiding the picker gives that up - the
    // call then flashes a taskbar button instead of moving the focus, and the snippet is
    // sent to whatever Windows happened to promote.
    if (target_ != nullptr && IsWindow(target_)) SetForegroundWindow(target_);
    hide();
    if (callbacks_.onChoose) callbacks_.onChoose(abbr);
}

void SnippetPicker::drawRow(const DRAWITEMSTRUCT& item) {
    if (item.itemID == static_cast<UINT>(-1)) return;
    const auto row = static_cast<std::size_t>(item.itemID);
    if (row >= shown_.size()) return;
    const Item& e = items_[static_cast<std::size_t>(shown_[row])];
    const bool selected = (item.itemState & ODS_SELECTED) != 0;

    RECT r = item.rcItem;
    const HBRUSH fill =
        CreateSolidBrush(selected ? kPopupPalette.selection : kPopupPalette.background);
    FillRect(item.hDC, &r, fill);
    DeleteObject(fill);
    if (selected) {
        // The same accent bar the suggestion popup uses for the row Tab would take.
        const UINT dpi = GetDpiForWindow(hwnd_);
        const RECT bar{r.left, r.top, r.left + MulDiv(3, static_cast<int>(dpi), 96), r.bottom};
        const HBRUSH accent = CreateSolidBrush(kPopupPalette.accent);
        FillRect(item.hDC, &bar, accent);
        DeleteObject(accent);
    }

    const UINT dpi = GetDpiForWindow(hwnd_);
    const auto px = [dpi](int v) { return MulDiv(v, static_cast<int>(dpi), 96); };
    SetBkMode(item.hDC, TRANSPARENT);
    RECT abbr{r.left + px(kPad), r.top, r.left + px(kAbbrColumn), r.bottom};
    const auto old = static_cast<HFONT>(SelectObject(item.hDC, bold_));
    SetTextColor(item.hDC, kPopupPalette.text);
    DrawTextW(item.hDC, e.abbr.c_str(), -1, &abbr,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(item.hDC, old);

    RECT preview{r.left + px(kAbbrColumn), r.top, r.right - px(kPad), r.bottom};
    SetTextColor(item.hDC, kPopupPalette.textDim);
    DrawTextW(item.hDC, e.preview.c_str(), -1, &preview,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

LRESULT CALLBACK SnippetPicker::searchProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                           UINT_PTR id, DWORD_PTR ref) {
    auto* self = reinterpret_cast<SnippetPicker*>(ref);
    if (msg == WM_KEYDOWN && self != nullptr) {
        switch (wParam) {
        case VK_DOWN:
            self->move(1);
            return 0;
        case VK_UP:
            self->move(-1);
            return 0;
        case VK_RETURN:
            self->chooseSelected();
            return 0;
        case VK_ESCAPE:
            self->hide();
            return 0;
        default:
            break;
        }
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, &SnippetPicker::searchProc, id);
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK SnippetPicker::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* self = static_cast<SnippetPicker*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    auto* self = reinterpret_cast<SnippetPicker*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) return DefWindowProcW(hwnd, msg, wParam, lParam);
    return self->handle(msg, wParam, lParam);
}

LRESULT SnippetPicker::handle(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1; // WM_PAINT fills it; erasing first only flickers
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        const HDC dc = BeginPaint(hwnd_, &ps);
        paintChrome(dc);
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        // The field sits on the card itself; the hairline under it is what makes it a
        // field. A filled slab at this size read as a title bar, not as somewhere to type.
        const auto dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, kPopupPalette.text);
        SetBkColor(dc, kPopupPalette.background);
        return reinterpret_cast<LRESULT>(background_);
    }
    case WM_CTLCOLORLISTBOX: {
        // Owner-drawn rows only cover the rows. Everything below the last one is painted
        // with the class brush, which is white - a bright slab under a dark list.
        const auto dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, kPopupPalette.background);
        return reinterpret_cast<LRESULT>(background_);
    }
    case WM_DRAWITEM:
        drawRow(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == kIdSearch && HIWORD(wParam) == EN_CHANGE) {
            refilter();
            InvalidateRect(list_, nullptr, TRUE);
            return 0;
        }
        if (LOWORD(wParam) == kIdList && HIWORD(wParam) == LBN_DBLCLK) {
            chooseSelected();
            return 0;
        }
        break;
    case WM_ACTIVATE:
        // Clicking away is the same as pressing Esc: a palette does not linger.
        if (LOWORD(wParam) == WA_INACTIVE) hide();
        return 0;
    case WM_CLOSE:
        hide();
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

} // namespace lankey::ui::win32
