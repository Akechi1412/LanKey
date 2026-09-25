#include "ui/win32/TrayIcon.h"

#include <shellapi.h>
#include <utility>

// clang-format off
#include <objidl.h> // gdiplus.h needs PROPID and IStream declared first
#include <gdiplus.h>
// clang-format on

namespace lankey::ui::win32 {

using core::model::InputMethod;

namespace {

constexpr UINT kTrayMessage = WM_APP + 10;
constexpr UINT kTrayId = 1;
constexpr wchar_t kClassName[] = L"LanKeyTrayWindow";

enum MenuId : UINT {
    kMenuToggle = 100,
    kMenuTelex,
    kMenuVni,
    kMenuSimpleTelex,
    kMenuSuggestions,
    kMenuAutoCorrect,
    kMenuEraseData,
    kMenuShowData,
    kMenuSettings,
    kMenuQuit,
};

} // namespace

TrayIcon::TrayIcon() = default;

TrayIcon::~TrayIcon() {
    destroy();
}

bool TrayIcon::create(HINSTANCE instance, Callbacks callbacks) {
    instance_ = instance;
    callbacks_ = std::move(callbacks);

    WNDCLASSW wc{};
    wc.lpfnWndProc = &TrayIcon::wndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    RegisterClassW(&wc); // failing because it exists already is fine

    hwnd_ = CreateWindowExW(0, kClassName, L"LanKey", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                            instance, this);
    if (hwnd_ == nullptr) return false;

    // Explorer restarts recreate the taskbar; the icon must be re-added then.
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");

    iconVietnamese_ = makeLetterIcon(L'V', kBrandVietnamese);
    iconEnglish_ = makeLetterIcon(L'E', kBrandEnglish);

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = kTrayId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = kTrayMessage;
    nid.hIcon = iconVietnamese_;
    wcscpy_s(nid.szTip, L"LanKey");
    if (!Shell_NotifyIconW(NIM_ADD, &nid)) return false;
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
    updateIcon();
    return true;
}

void TrayIcon::destroy() {
    if (hwnd_ != nullptr) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd_;
        nid.uID = kTrayId;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (iconVietnamese_ != nullptr) DestroyIcon(iconVietnamese_);
    if (iconEnglish_ != nullptr) DestroyIcon(iconEnglish_);
    iconVietnamese_ = nullptr;
    iconEnglish_ = nullptr;
}

void TrayIcon::setState(bool vietnamese, InputMethod method, bool suggestions, bool autoCorrect) {
    vietnamese_ = vietnamese;
    method_ = method;
    suggestions_ = suggestions;
    autoCorrect_ = autoCorrect;
    updateIcon();
}

void TrayIcon::updateIcon() {
    if (hwnd_ == nullptr) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = kTrayId;
    nid.uFlags = NIF_ICON | NIF_TIP;
    nid.hIcon = vietnamese_ ? iconVietnamese_ : iconEnglish_;
    const wchar_t* method = method_ == InputMethod::Vni           ? L"VNI"
                            : method_ == InputMethod::SimpleTelex ? L"Simple Telex"
                            : method_ == InputMethod::Custom      ? L"Tự định nghĩa"
                                                                  : L"Telex";
    swprintf_s(nid.szTip, L"LanKey - %s (%s)", vietnamese_ ? L"Tiếng Việt" : L"English", method);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayIcon::showBalloon(const std::wstring& title, const std::wstring& text) {
    if (hwnd_ == nullptr) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = kTrayId;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

HICON TrayIcon::makeLetterIcon(wchar_t letter, COLORREF background) {
    // GDI+ for anti-aliased corners and glyph: at 16 px a GDI-drawn square with a jagged
    // letter is the one thing that makes a tray icon look home-made.
    static ULONG_PTR gdiplusToken = [] {
        Gdiplus::GdiplusStartupInput input;
        ULONG_PTR token = 0;
        Gdiplus::GdiplusStartup(&token, &input, nullptr);
        return token;
    }();
    (void)gdiplusToken;

    // Render at 4x and downsample: GDI+ anti-aliasing at 16 px is soft and its text
    // hinting drifts the glyph off centre; a bicubic downscale of a large, exactly centred
    // rendering is crisp at every tray DPI.
    const int size = GetSystemMetrics(SM_CXSMICON);
    constexpr int kOversample = 4;
    const int big = size * kOversample;
    Gdiplus::Bitmap large(big, big, PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics g(&large);
        g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));

        // Rounded square, radius ~26% like Windows 11 app tiles.
        const auto s = static_cast<Gdiplus::REAL>(big);
        const Gdiplus::REAL r = s * 0.26f;
        Gdiplus::GraphicsPath tile;
        tile.AddArc(0.0f, 0.0f, 2 * r, 2 * r, 180.0f, 90.0f);
        tile.AddArc(s - 2 * r, 0.0f, 2 * r, 2 * r, 270.0f, 90.0f);
        tile.AddArc(s - 2 * r, s - 2 * r, 2 * r, 2 * r, 0.0f, 90.0f);
        tile.AddArc(0.0f, s - 2 * r, 2 * r, 2 * r, 90.0f, 90.0f);
        tile.CloseFigure();
        Gdiplus::SolidBrush fill(Gdiplus::Color(255, GetRValue(background), GetGValue(background),
                                                GetBValue(background)));
        g.FillPath(&fill, &tile);

        // The letter as a path: its real ink bounds are known, so it is centred exactly
        // instead of by the em box, which sits capitals visibly low and to the left.
        Gdiplus::FontFamily family(L"Segoe UI");
        Gdiplus::GraphicsPath glyph;
        const wchar_t text[2] = {letter, 0};
        glyph.AddString(text, 1, &family, Gdiplus::FontStyleBold, s * 0.80f,
                        Gdiplus::PointF(0.0f, 0.0f), Gdiplus::StringFormat::GenericTypographic());
        Gdiplus::RectF bounds;
        glyph.GetBounds(&bounds);
        Gdiplus::Matrix centre;
        centre.Translate((s - bounds.Width) / 2 - bounds.X, (s - bounds.Height) / 2 - bounds.Y);
        glyph.Transform(&centre);
        Gdiplus::SolidBrush ink(Gdiplus::Color(255, 255, 255, 255));
        g.FillPath(&ink, &glyph);
    }

    Gdiplus::Bitmap bitmap(size, size, PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics g(&bitmap);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        g.DrawImage(&large, Gdiplus::Rect(0, 0, size, size), 0, 0, big, big, Gdiplus::UnitPixel);
    }

    HICON icon = nullptr;
    bitmap.GetHICON(&icon);
    return icon;
}

LRESULT CALLBACK TrayIcon::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        // The member is not set yet while CreateWindowEx is still running, but the handler
        // already needs a valid HWND for DefWindowProc (returning 0 here aborts creation).
        static_cast<TrayIcon*>(cs->lpCreateParams)->hwnd_ = hwnd;
    }
    auto* self = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) return self->handle(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT TrayIcon::handle(UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == kTrayMessage) {
        switch (LOWORD(lParam)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
            // NOTIFYICON_VERSION_4 delivers NIN_SELECT for a click in addition to the
            // WM_LBUTTONUP it also sends; handling both toggled twice (V stayed V).
            //
            // The language switches on this click, not after the double-click interval:
            // waiting made the icon feel broken next to Ctrl+Shift, which is instant.
            lastToggleTick_ = GetTickCount64();
            if (callbacks_.onToggleVietnamese) callbacks_.onToggleVietnamese();
            return 0;
        case WM_LBUTTONDBLCLK: {
            // The first of the two clicks already toggled the language. Put it back -
            // quietly, so the user does not see two toasts - and open the panel.
            const bool justToggled = GetTickCount64() - lastToggleTick_ <= GetDoubleClickTime();
            if (justToggled && callbacks_.onUndoToggle) callbacks_.onUndoToggle();
            if (callbacks_.onSettings) callbacks_.onSettings();
            return 0;
        }
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            showMenu();
            return 0;
        default:
            return 0;
        }
    }
    if (msg == WM_COMMAND) {
        switch (LOWORD(wParam)) {
        case kMenuToggle:
            if (callbacks_.onToggleVietnamese) callbacks_.onToggleVietnamese();
            break;
        case kMenuTelex:
            if (callbacks_.onInputMethod) callbacks_.onInputMethod(InputMethod::Telex);
            break;
        case kMenuVni:
            if (callbacks_.onInputMethod) callbacks_.onInputMethod(InputMethod::Vni);
            break;
        case kMenuSimpleTelex:
            if (callbacks_.onInputMethod) callbacks_.onInputMethod(InputMethod::SimpleTelex);
            break;
        case kMenuSuggestions:
            if (callbacks_.onSuggestionsEnabled) callbacks_.onSuggestionsEnabled(!suggestions_);
            break;
        case kMenuAutoCorrect:
            if (callbacks_.onAutoCorrectEnabled) callbacks_.onAutoCorrectEnabled(!autoCorrect_);
            break;
        case kMenuShowData:
            if (callbacks_.onShowData) callbacks_.onShowData();
            break;
        case kMenuSettings:
            if (callbacks_.onSettings) callbacks_.onSettings();
            break;
        case kMenuEraseData:
            if (callbacks_.onEraseAllData) callbacks_.onEraseAllData();
            break;
        case kMenuQuit:
            if (callbacks_.onQuit) callbacks_.onQuit();
            break;
        default:
            break;
        }
        return 0;
    }
    if (taskbarCreatedMessage_ != 0 && msg == taskbarCreatedMessage_) {
        // Explorer came back: re-add the icon.
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd_;
        nid.uID = kTrayId;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = kTrayMessage;
        nid.hIcon = vietnamese_ ? iconVietnamese_ : iconEnglish_;
        wcscpy_s(nid.szTip, L"LanKey");
        Shell_NotifyIconW(NIM_ADD, &nid);
        updateIcon();
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

void TrayIcon::showMenu() {
    const HMENU menu = CreatePopupMenu();
    const auto check = [](bool on) -> UINT {
        return static_cast<UINT>(MF_STRING | (on ? MF_CHECKED : MF_UNCHECKED));
    };
    AppendMenuW(menu, check(vietnamese_), kMenuToggle, L"Tiếng Việt\tCtrl+Shift");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, check(method_ == InputMethod::Telex), kMenuTelex, L"Telex");
    AppendMenuW(menu, check(method_ == InputMethod::Vni), kMenuVni, L"VNI");
    AppendMenuW(menu, check(method_ == InputMethod::SimpleTelex), kMenuSimpleTelex,
                L"Telex đơn giản");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, check(suggestions_), kMenuSuggestions, L"Gợi ý cụm từ");
    AppendMenuW(menu, check(autoCorrect_), kMenuAutoCorrect, L"Tự sửa lỗi chính tả");
    AppendMenuW(menu, MF_STRING, kMenuSettings, L"Bảng điều khiển");
    AppendMenuW(menu, MF_STRING, kMenuShowData, L"Dữ liệu của bạn");
    AppendMenuW(menu, MF_STRING, kMenuEraseData, L"Xoá toàn bộ dữ liệu đã học");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuQuit, L"Thoát");

    POINT pt{};
    GetCursorPos(&pt);
    // Required so the menu closes when the user clicks elsewhere (MSDN KB135788).
    SetForegroundWindow(hwnd_);
    TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

} // namespace lankey::ui::win32
