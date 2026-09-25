#include "ui/win32/DataDialog.h"

#include "ui/win32/Theme.h"

namespace lankey::ui::win32 {

namespace {

constexpr wchar_t kClassName[] = L"LanKeyDataDialog";
constexpr int kIdOpenFolder = 1001;
constexpr int kIdErase = 1002;
constexpr int kIdClose = 1003;
constexpr int kWidth = 640; // 96-dpi pixels
constexpr int kHeight = 560;
constexpr int kMargin = 12;
constexpr int kButtonHeight = 30;

// The markdown reads well enough as plain text once the heading marks and table bars
// are softened; no renderer, nothing to get wrong.
std::wstring plainText(const std::wstring& markdown) {
    std::wstring out;
    out.reserve(markdown.size());
    std::size_t lineStart = 0;
    while (lineStart <= markdown.size()) {
        std::size_t lineEnd = markdown.find(L'\n', lineStart);
        if (lineEnd == std::wstring::npos) lineEnd = markdown.size();
        std::wstring line = markdown.substr(lineStart, lineEnd - lineStart);
        std::size_t hashes = 0;
        while (hashes < line.size() && line[hashes] == L'#')
            ++hashes;
        if (hashes > 0 && hashes < line.size() && line[hashes] == L' ') {
            line = line.substr(hashes + 1);
            if (hashes == 1) line = std::wstring(line.size(), L'=') + L"\r\n" + line;
            line += L"\r\n";
        } else if (line.rfind(L"|---", 0) == 0) {
            line.clear(); // table separator row
        } else if (!line.empty() && line.front() == L'|') {
            // Table row: cells separated by two spaces.
            std::wstring cells;
            std::size_t pos = 1;
            while (pos < line.size()) {
                std::size_t next = line.find(L'|', pos);
                if (next == std::wstring::npos) next = line.size();
                std::wstring cell = line.substr(pos, next - pos);
                while (!cell.empty() && cell.front() == L' ')
                    cell.erase(cell.begin());
                while (!cell.empty() && cell.back() == L' ')
                    cell.pop_back();
                if (!cell.empty()) {
                    if (!cells.empty()) cells += L"  -  ";
                    cells += cell;
                }
                pos = next + 1;
            }
            line = cells;
        }
        // Inline code and emphasis marks.
        std::wstring cleaned;
        for (const wchar_t c : line) {
            if (c == L'`' || c == L'*') continue;
            cleaned.push_back(c);
        }
        if (cleaned.rfind(L"- ", 0) == 0) cleaned = L"  • " + cleaned.substr(2);
        out += cleaned;
        out += L"\r\n";
        if (lineEnd == markdown.size()) break;
        lineStart = lineEnd + 1;
    }
    return out;
}

} // namespace

DataDialog::~DataDialog() {
    destroy();
}

void DataDialog::show(HINSTANCE instance, const std::wstring& text, Callbacks callbacks) {
    callbacks_ = std::move(callbacks);
    if (hwnd_ == nullptr) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = &DataDialog::wndProc;
        wc.hInstance = instance;
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&wc);

        const UINT dpi = GetDpiForSystem();
        const auto px = [dpi](int v) { return MulDiv(v, static_cast<int>(dpi), 96); };
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const int w = px(kWidth);
        const int h = px(kHeight);
        hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME, kClassName, L"LanKey — Dữ liệu của bạn",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
                                    WS_MINIMIZEBOX,
                                (work.left + work.right - w) / 2, (work.top + work.bottom - h) / 2,
                                w, h, nullptr, nullptr, instance, this);
        if (hwnd_ == nullptr) return;
        setWindowIcons(hwnd_, instance);
        font_ = createUiFont(dpi, 10, FW_NORMAL);
        text_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY |
                                    ES_AUTOVSCROLL,
                                0, 0, 0, 0, hwnd_, nullptr, instance, nullptr);
        openFolder_ = CreateWindowExW(
            0, L"BUTTON", L"Mở thư mục dữ liệu", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kIdOpenFolder), instance, nullptr);
        erase_ = CreateWindowExW(0, L"BUTTON", L"Xoá toàn bộ dữ liệu",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0,
                                 hwnd_, reinterpret_cast<HMENU>(kIdErase), instance, nullptr);
        close_ = CreateWindowExW(0, L"BUTTON", L"Đóng",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0,
                                 hwnd_, reinterpret_cast<HMENU>(kIdClose), instance, nullptr);
        for (const HWND child : {text_, openFolder_, erase_, close_}) {
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
        layout();
    }
    SetWindowTextW(text_, plainText(text).c_str());
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
}

void DataDialog::destroy() {
    if (hwnd_ != nullptr) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    if (font_ != nullptr) DeleteObject(font_);
    font_ = nullptr;
}

void DataDialog::layout() {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const UINT dpi = GetDpiForWindow(hwnd_);
    const auto px = [dpi](int v) { return MulDiv(v, static_cast<int>(dpi), 96); };
    const int margin = px(kMargin);
    const int buttonH = px(kButtonHeight);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    MoveWindow(text_, margin, margin, width - 2 * margin, height - 3 * margin - buttonH, TRUE);
    const int y = height - margin - buttonH;
    const int wide = px(170);
    const int narrow = px(90);
    MoveWindow(openFolder_, margin, y, wide, buttonH, TRUE);
    MoveWindow(erase_, margin * 2 + wide, y, wide, buttonH, TRUE);
    MoveWindow(close_, width - margin - narrow, y, narrow, buttonH, TRUE);
}

LRESULT CALLBACK DataDialog::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* self = static_cast<DataDialog*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    auto* self = reinterpret_cast<DataDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) return DefWindowProcW(hwnd, msg, wParam, lParam);
    return self->handle(msg, wParam, lParam);
}

LRESULT DataDialog::handle(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DPICHANGED: {
        const UINT dpi = HIWORD(wParam);
        if (font_ != nullptr) DeleteObject(font_);
        font_ = createUiFont(dpi, 10, FW_NORMAL);
        for (const HWND child : {text_, openFolder_, erase_, close_}) {
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
        const auto* r = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
        return 0;
    }
    case WM_SETCURSOR:
        if (const auto over = reinterpret_cast<HWND>(wParam);
            over == openFolder_ || over == erase_ || over == close_) {
            showHandCursor();
            return TRUE;
        }
        break;
    case WM_SIZE:
        if (text_ != nullptr) layout();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kIdOpenFolder:
            if (callbacks_.onOpenFolder) callbacks_.onOpenFolder();
            return 0;
        case kIdErase:
            if (callbacks_.onEraseAllData) callbacks_.onEraseAllData();
            return 0;
        case kIdClose:
            ShowWindow(hwnd_, SW_HIDE);
            return 0;
        default:
            break;
        }
        break;
    case WM_CLOSE:
        ShowWindow(hwnd_, SW_HIDE); // keep the window; it is cheap and reopens instantly
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

} // namespace lankey::ui::win32
