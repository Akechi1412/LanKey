#include "ui/win32/HotkeyField.h"

#include <commctrl.h>

namespace lankey::ui::win32 {

namespace hotkeyField {

namespace {

using core::model::Hotkey;
using core::model::Modifier;
using core::model::VirtualKey;

constexpr UINT_PTR kSubclassId = 1;

struct State {
    Hotkey value;
    Hotkey beforeFocus; // Esc restores it
    bool focused = false;
};

std::wstring label(const Hotkey& h) {
    if (!h.assigned()) return L"Chưa gán";
    return platform::win32::fromUtf8(core::model::formatHotkey(h));
}

void show(HWND field, const State& state) {
    const std::wstring text =
        state.focused && !state.value.assigned() ? L"Nhấn tổ hợp phím" : label(state.value);
    SetWindowTextW(field, text.c_str());
}

void notify(HWND field) {
    const HWND parent = GetParent(field);
    const int id = GetDlgCtrlID(field);
    SendMessageW(parent, WM_COMMAND, MAKEWPARAM(id, EN_CHANGE), reinterpret_cast<LPARAM>(field));
}

Modifier heldModifiers() {
    Modifier m = Modifier::None;
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) m = m | Modifier::Control;
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) m = m | Modifier::Shift;
    if ((GetKeyState(VK_MENU) & 0x8000) != 0) m = m | Modifier::Alt;
    if ((GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0) {
        m = m | Modifier::Win;
    }
    return m;
}

LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR /*id*/,
                      DWORD_PTR ref) {
    auto* state = reinterpret_cast<State*>(ref);
    switch (msg) {
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS; // Tab, Enter, Esc, arrows: all ours while focused
    case WM_CHAR:
    case WM_SYSCHAR:
        return 0; // never let the EDIT insert anything
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        const auto vk = static_cast<int>(wParam);
        switch (vk) {
        case VK_CONTROL:
        case VK_SHIFT:
        case VK_MENU:
        case VK_LWIN:
        case VK_RWIN:
            return 0; // a modifier alone: wait for the key
        case VK_TAB: {
            // Keep keyboard navigation working: move focus like a dialog would.
            const bool back = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const HWND next = GetNextDlgTabItem(GetParent(hwnd), hwnd, back ? TRUE : FALSE);
            if (next != nullptr) SetFocus(next);
            return 0;
        }
        case VK_ESCAPE:
            state->value = state->beforeFocus;
            show(hwnd, *state);
            notify(hwnd);
            return 0;
        case VK_BACK:
        case VK_DELETE:
            state->value = {};
            show(hwnd, *state);
            notify(hwnd);
            return 0;
        default:
            break;
        }
        const bool letterOrDigit = (vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9');
        const Modifier mods = heldModifiers();
        if (!letterOrDigit || mods == Modifier::None) {
            MessageBeep(MB_ICONWARNING); // unusable chord: needs a modifier and a letter/digit
            return 0;
        }
        state->value = Hotkey{mods, static_cast<VirtualKey>(vk)};
        show(hwnd, *state);
        notify(hwnd);
        return 0;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP:
        return 0;
    case WM_SETFOCUS:
        state->focused = true;
        state->beforeFocus = state->value;
        show(hwnd, *state);
        break;
    case WM_KILLFOCUS:
        state->focused = false;
        show(hwnd, *state);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, proc, kSubclassId);
        delete state;
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    default:
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

} // namespace

HWND create(HWND parent, int id, HINSTANCE instance) {
    const HWND field = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_CENTER | ES_READONLY | WS_TABSTOP, 0, 0, 10,
        10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    if (field == nullptr) return nullptr;
    auto* state = new State();
    SetWindowSubclass(field, proc, kSubclassId, reinterpret_cast<DWORD_PTR>(state));
    show(field, *state);
    return field;
}

void set(HWND field, const Hotkey& hotkey) {
    DWORD_PTR ref = 0;
    if (!GetWindowSubclass(field, proc, kSubclassId, &ref)) return;
    auto* state = reinterpret_cast<State*>(ref);
    state->value = hotkey;
    state->beforeFocus = hotkey;
    show(field, *state);
}

Hotkey get(HWND field) {
    DWORD_PTR ref = 0;
    if (!GetWindowSubclass(field, proc, kSubclassId, &ref)) return {};
    return reinterpret_cast<State*>(ref)->value;
}

} // namespace hotkeyField

} // namespace lankey::ui::win32
