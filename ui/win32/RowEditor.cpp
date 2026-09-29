#include "ui/win32/RowEditor.h"

#include <algorithm>
#include <string_view>

#include "ui/win32/Theme.h"

namespace lankey::ui::win32 {

namespace {

constexpr wchar_t kClassName[] = L"LanKeyRowEditor";
constexpr int kIdFirstField = 100;
constexpr int kIdOk = 1;
constexpr int kIdCancel = 2;
// 96-dpi pixels.
constexpr int kWidth = 480;
constexpr int kMargin = 16;
constexpr int kLabelHeight = 20;
constexpr int kLineHeight = 26;
constexpr int kMultilineHeight = 120;
constexpr int kCheckHeight = 24;
constexpr int kHintHeight = 18;
constexpr int kGap = 10;
constexpr int kButtonHeight = 30;
constexpr int kButtonWidth = 100;

constexpr COLORREF kText = RGB(0x1B, 0x1F, 0x26);
constexpr COLORREF kDim = RGB(0x6B, 0x73, 0x80);
constexpr COLORREF kBackground = RGB(0xFF, 0xFF, 0xFF);

// A multiline edit control speaks CRLF and nothing else; the snippet bodies, the JSON they
// are stored in and the sender all speak LF. Translate at this boundary so no \r ever gets
// into a body - it would be typed into the user's document as a stray character.
std::wstring toControl(std::wstring_view s) {
    std::wstring out;
    out.reserve(s.size());
    for (const wchar_t c : s) {
        if (c == L'\n') out.push_back(L'\r');
        out.push_back(c);
    }
    return out;
}

std::wstring fromControl(std::wstring_view s) {
    std::wstring out;
    out.reserve(s.size());
    for (const wchar_t c : s) {
        if (c != L'\r') out.push_back(c);
    }
    return out;
}

// How tall the hint has to be to show all of it at `width`. Measured rather than assumed:
// one of these is a list of placeholder names and it does not fit on a line.
int hintHeight(HFONT font, std::wstring_view text, int width, UINT dpi) {
    if (text.empty()) return 0;
    const HDC dc = GetDC(nullptr);
    const auto old = static_cast<HFONT>(SelectObject(dc, font));
    RECT r{0, 0, width, 0};
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &r,
              DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    return (std::max)(static_cast<int>(r.bottom), MulDiv(kHintHeight, static_cast<int>(dpi), 96));
}

int fieldHeight(RowEditor::Field::Kind kind) {
    switch (kind) {
    case RowEditor::Field::Kind::Multiline:
        return kMultilineHeight;
    case RowEditor::Field::Kind::Check:
        return kCheckHeight;
    case RowEditor::Field::Kind::Line:
        break;
    }
    return kLineHeight;
}

} // namespace

RowEditor::~RowEditor() {
    destroy();
}

int RowEditor::px(int v) const {
    return MulDiv(v, static_cast<int>(dpi_), 96);
}

void RowEditor::show(HINSTANCE instance, HWND owner, const std::wstring& title,
                     std::vector<Field> fields, OnApply onApply) {
    // Rebuilt every time: the two pages ask for different fields, so there is no window
    // worth keeping between uses.
    destroy();
    owner_ = owner;
    fields_ = std::move(fields);
    onApply_ = std::move(onApply);

    WNDCLASSW wc{};
    wc.lpfnWndProc = &RowEditor::wndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    dpi_ = owner != nullptr ? GetDpiForWindow(owner) : GetDpiForSystem();
    font_ = createUiFont(dpi_, 10, FW_NORMAL);
    semibold_ = createUiFont(dpi_, 10, FW_SEMIBOLD);
    background_ = CreateSolidBrush(kBackground);

    // Heights first, because the window has to be tall enough for all of them. In device
    // pixels throughout: the hints are measured, not scaled from a guess.
    const int fieldWidth = px(kWidth) - 2 * px(kMargin);
    std::vector<int> hints;
    hints.reserve(fields_.size());
    int content = 0;
    for (const Field& f : fields_) {
        if (f.kind != Field::Kind::Check) content += px(kLabelHeight);
        content += px(fieldHeight(f.kind)) + px(kGap);
        hints.push_back(hintHeight(font_, f.hint, fieldWidth, dpi_));
        content += hints.back();
    }
    const int height = 2 * px(kMargin) + content + px(kButtonHeight) + px(kGap);

    // That height is what the CONTENT needs. CreateWindow takes the whole window, title
    // bar and borders included, so ask Windows what those cost at this DPI - guessing sat
    // the buttons below the bottom edge.
    constexpr DWORD kStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT frame{0, 0, px(kWidth), height};
    AdjustWindowRectExForDpi(&frame, kStyle, FALSE, WS_EX_DLGMODALFRAME, dpi_);
    const int w = frame.right - frame.left;
    const int h = frame.bottom - frame.top;

    RECT ownerRect{};
    if (owner != nullptr) GetWindowRect(owner, &ownerRect);
    const int x = owner != nullptr ? (ownerRect.left + ownerRect.right - w) / 2 : CW_USEDEFAULT;
    const int y = owner != nullptr ? (ownerRect.top + ownerRect.bottom - h) / 2 : CW_USEDEFAULT;
    hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME, kClassName, title.c_str(), kStyle, x, y, w, h,
                            owner, nullptr, instance, this);
    if (hwnd_ == nullptr) return;
    setWindowIcons(hwnd_, instance);

    // Children are placed in client coordinates, which is why the client rect is measured
    // rather than the window size reused.
    RECT client{};
    GetClientRect(hwnd_, &client);
    int top = px(kMargin);
    const int left = px(kMargin);
    const int width = client.right - 2 * px(kMargin);
    for (std::size_t i = 0; i < fields_.size(); ++i) {
        const Field& f = fields_[i];
        if (f.kind != Field::Kind::Check) {
            const HWND label = CreateWindowExW(
                0, L"STATIC", f.label.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_CENTERIMAGE, left, top, width,
                px(kLabelHeight), hwnd_, nullptr, instance, nullptr);
            SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(semibold_), TRUE);
            top += px(kLabelHeight);
        }
        const auto id = reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kIdFirstField + i));
        HWND control = nullptr;
        if (f.kind == Field::Kind::Check) {
            control = CreateWindowExW(0, L"BUTTON", f.label.c_str(),
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, left,
                                      top, width, px(kCheckHeight), hwnd_, id, instance, nullptr);
            SendMessageW(control, BM_SETCHECK, f.value.empty() ? BST_UNCHECKED : BST_CHECKED, 0);
        } else {
            const DWORD style = f.kind == Field::Kind::Multiline
                                    ? (ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL)
                                    : ES_AUTOHSCROLL;
            const std::wstring shown =
                f.kind == Field::Kind::Multiline ? toControl(f.value) : f.value;
            control = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", shown.c_str(),
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, left, top, width,
                                      px(fieldHeight(f.kind)), hwnd_, id, instance, nullptr);
        }
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        controls_.push_back(control);
        top += px(fieldHeight(f.kind));
        if (!f.hint.empty()) {
            const HWND hint = CreateWindowExW(
                0, L"STATIC", f.hint.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, left,
                top, width, hints[i], hwnd_, nullptr, instance, nullptr);
            SendMessageW(hint, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
            dim_.push_back(hint);
        }
        top += hints[i] + px(kGap);
    }

    const int buttonY = client.bottom - px(kMargin) - px(kButtonHeight);
    const int buttonW = px(kButtonWidth);
    cancel_ = CreateWindowExW(0, L"BUTTON", L"Huỷ", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                              left + width - buttonW, buttonY, buttonW, px(kButtonHeight), hwnd_,
                              reinterpret_cast<HMENU>(kIdCancel), instance, nullptr);
    ok_ =
        CreateWindowExW(0, L"BUTTON", L"Lưu", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                        left + width - 2 * buttonW - px(kGap), buttonY, buttonW, px(kButtonHeight),
                        hwnd_, reinterpret_cast<HMENU>(kIdOk), instance, nullptr);
    for (const HWND b : {ok_, cancel_})
        SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);

    enableDialogNavigation(hwnd_);
    EnableWindow(owner_, FALSE);
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    if (!controls_.empty()) SetFocus(controls_.front());
}

void RowEditor::destroy() {
    if (hwnd_ != nullptr) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    ok_ = cancel_ = nullptr;
    controls_.clear();
    dim_.clear();
    for (const HFONT f : {font_, semibold_}) {
        if (f != nullptr) DeleteObject(f);
    }
    font_ = semibold_ = nullptr;
    if (background_ != nullptr) DeleteObject(background_);
    background_ = nullptr;
}

void RowEditor::close(bool apply) {
    std::vector<std::wstring> values;
    if (apply) {
        values.reserve(controls_.size());
        for (std::size_t i = 0; i < controls_.size(); ++i) {
            if (fields_[i].kind == Field::Kind::Check) {
                values.push_back(
                    SendMessageW(controls_[i], BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"");
                continue;
            }
            const int length = GetWindowTextLengthW(controls_[i]);
            std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
            GetWindowTextW(controls_[i], text.data(), length + 1);
            text.resize(static_cast<std::size_t>(length));
            values.push_back(fromControl(text));
        }
    }
    EnableWindow(owner_, TRUE);
    if (owner_ != nullptr) SetForegroundWindow(owner_);
    ShowWindow(hwnd_, SW_HIDE);
    // The callback may open this editor again (it will not, today) so the values are read
    // and the window is out of the way before it runs.
    if (apply && onApply_) onApply_(values);
}

LRESULT CALLBACK RowEditor::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        static_cast<RowEditor*>(cs->lpCreateParams)->hwnd_ = hwnd;
    }
    auto* self = reinterpret_cast<RowEditor*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) return DefWindowProcW(hwnd, msg, wParam, lParam);
    return self->handle(msg, wParam, lParam);
}

LRESULT RowEditor::handle(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CTLCOLORSTATIC: {
        const auto dc = reinterpret_cast<HDC>(wParam);
        const auto control = reinterpret_cast<HWND>(lParam);
        const bool hint = std::find(dim_.begin(), dim_.end(), control) != dim_.end();
        SetTextColor(dc, hint ? kDim : kText);
        SetBkColor(dc, kBackground);
        return reinterpret_cast<LRESULT>(background_);
    }
    case WM_CTLCOLORBTN: {
        SetBkColor(reinterpret_cast<HDC>(wParam), kBackground);
        return reinterpret_cast<LRESULT>(background_);
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kIdOk:
            close(true);
            return 0;
        case kIdCancel:
            close(false);
            return 0;
        default:
            break;
        }
        break;
    case WM_CLOSE:
        close(false);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

} // namespace lankey::ui::win32
