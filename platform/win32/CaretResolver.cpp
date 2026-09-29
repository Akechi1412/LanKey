#include "platform/win32/CaretResolver.h"

#include <objbase.h>
#include <oleacc.h>
#include <uiautomation.h>

namespace lankey::platform::win32 {

using core::model::ScreenRect;

namespace {

// UI Automation client for the calling (UI) thread. Created on first use; the thread must
// already be a COM apartment (App initialises COM on the UI thread).
class UiaCaret {
public:
    UiaCaret() {
        CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation,
                         reinterpret_cast<void**>(&automation_));
    }
    ~UiaCaret() {
        if (automation_ != nullptr) automation_->Release();
    }
    UiaCaret(const UiaCaret&) = delete;
    UiaCaret& operator=(const UiaCaret&) = delete;

    // The caret (or the end of the selection) of the focused text control, in screen px.
    // What the provider said about the caret, which is not the same question as where it
    // is. "Inactive" is the one that matters: the element owns a caret but the keyboard
    // is not in it - the user clicked away from the text box - and then there is no point
    // in a suggestion, wherever it would be drawn.
    enum class Caret { Unknown, Active, Inactive };

    [[nodiscard]] std::optional<ScreenRect> resolve(Caret& state, bool& fromRange) const {
        state = Caret::Unknown;
        fromRange = false;
        if (automation_ == nullptr) return std::nullopt;
        IUIAutomationElement* element = nullptr;
        if (FAILED(automation_->GetFocusedElement(&element)) || element == nullptr) {
            return std::nullopt;
        }
        std::optional<ScreenRect> rect = fromCaretRange(element, state);
        fromRange = rect.has_value();
        // The selection is NOT a fallback for an inactive caret. In a browser the document
        // keeps a selection whether or not an input has the focus, so falling through here
        // is what puts a suggestion popup on screen for text that is going nowhere.
        if (!rect && state != Caret::Inactive) rect = fromSelection(element);
        element->Release();
        return rect;
    }

private:
    // TextPattern2::GetCaretRange - the precise caret, supported by modern editors
    // (browsers, Office, WPF/WinUI, Notepad on Windows 11).
    [[nodiscard]] static std::optional<ScreenRect> fromCaretRange(IUIAutomationElement* element,
                                                                  Caret& state) {
        IUIAutomationTextPattern2* text = nullptr;
        if (FAILED(element->GetCurrentPatternAs(UIA_TextPattern2Id, IID_IUIAutomationTextPattern2,
                                                reinterpret_cast<void**>(&text))) ||
            text == nullptr) {
            return std::nullopt; // no opinion: older providers, panes, terminals
        }
        BOOL active = FALSE;
        IUIAutomationTextRange* range = nullptr;
        std::optional<ScreenRect> rect;
        if (SUCCEEDED(text->GetCaretRange(&active, &range)) && range != nullptr) {
            // isActive: "the caret is in the text element that has keyboard focus". The
            // provider is answering the question no other API here can.
            state = active ? Caret::Active : Caret::Inactive;
            if (active) rect = boundsOfCaretRange(range);
            range->Release();
        }
        text->Release();
        return rect;
    }

    // A caret is a degenerate (empty) range and many providers - Chromium/Electron among
    // them - return NO rectangles for it. Widen it step by step: the character before the
    // caret (its right edge is the caret), then the whole line.
    [[nodiscard]] static std::optional<ScreenRect>
    boundsOfCaretRange(IUIAutomationTextRange* range) {
        if (auto rect = boundsOf(range)) return rect;
        int moved = 0;
        if (SUCCEEDED(range->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Character,
                                                -1, &moved)) &&
            moved != 0) {
            if (auto rect = boundsOf(range)) {
                // Anchor at the right edge of the previous character.
                return ScreenRect{rect->x + rect->width, rect->y, 1, rect->height};
            }
        }
        if (SUCCEEDED(range->ExpandToEnclosingUnit(TextUnit_Line))) {
            if (auto rect = boundsOf(range)) {
                // Whole line: the caret is somewhere on it; use its right end.
                return ScreenRect{rect->x + rect->width, rect->y, 1, rect->height};
            }
        }
        return std::nullopt;
    }

    // TextPattern::GetSelection - older controls; the collapsed selection is the caret.
    [[nodiscard]] static std::optional<ScreenRect> fromSelection(IUIAutomationElement* element) {
        IUIAutomationTextPattern* text = nullptr;
        if (FAILED(element->GetCurrentPatternAs(UIA_TextPatternId, IID_IUIAutomationTextPattern,
                                                reinterpret_cast<void**>(&text))) ||
            text == nullptr) {
            return std::nullopt;
        }
        IUIAutomationTextRangeArray* ranges = nullptr;
        std::optional<ScreenRect> rect;
        if (SUCCEEDED(text->GetSelection(&ranges)) && ranges != nullptr) {
            int count = 0;
            ranges->get_Length(&count);
            if (count > 0) {
                IUIAutomationTextRange* range = nullptr;
                if (SUCCEEDED(ranges->GetElement(0, &range)) && range != nullptr) {
                    // Collapse to the end so a selection reports where typing continues.
                    range->MoveEndpointByRange(TextPatternRangeEndpoint_Start, range,
                                               TextPatternRangeEndpoint_End);
                    rect = boundsOfCaretRange(range);
                    range->Release();
                }
            }
            ranges->Release();
        }
        text->Release();
        return rect;
    }

    [[nodiscard]] static std::optional<ScreenRect> boundsOf(IUIAutomationTextRange* range) {
        SAFEARRAY* array = nullptr;
        if (FAILED(range->GetBoundingRectangles(&array)) || array == nullptr) return std::nullopt;
        std::optional<ScreenRect> rect;
        double* values = nullptr;
        LONG lower = 0;
        LONG upper = -1;
        if (SUCCEEDED(SafeArrayGetLBound(array, 1, &lower)) &&
            SUCCEEDED(SafeArrayGetUBound(array, 1, &upper)) && upper - lower + 1 >= 4 &&
            SUCCEEDED(SafeArrayAccessData(array, reinterpret_cast<void**>(&values)))) {
            // Quadruples of (left, top, width, height); the last one is the caret's line.
            const LONG count = (upper - lower + 1) / 4;
            const double* r = values + (count - 1) * 4;
            rect = ScreenRect{static_cast<int>(r[0]), static_cast<int>(r[1]),
                              (std::max)(1, static_cast<int>(r[2])),
                              (std::max)(1, static_cast<int>(r[3]))};
            SafeArrayUnaccessData(array);
        }
        SafeArrayDestroy(array);
        // A zero-sized rectangle at the origin means "unknown".
        if (rect && rect->height <= 1 && rect->width <= 1 && rect->x == 0 && rect->y == 0) {
            return std::nullopt;
        }
        return rect;
    }

public:
    // Is the focused element something the user can type into? Measured 2026-09-29 across
    // Edge, VS Code, Notepad and Windows Terminal: a browser reports the focused <input>
    // as an Edit with IsReadOnly false, and once the user clicks away from it the focus
    // falls back to the page Document with IsReadOnly TRUE. That is the one thing on
    // screen that distinguishes "typing lands here" from "typing lands nowhere".
    [[nodiscard]] int textFocus(int& controlType) const { // 0 unknown, 1 editable, 2 read-only
        controlType = 0;
        if (automation_ == nullptr) return 0;
        IUIAutomationElement* element = nullptr;
        if (FAILED(automation_->GetFocusedElement(&element)) || element == nullptr) return 0;
        CONTROLTYPEID type = 0;
        if (SUCCEEDED(element->get_CurrentControlType(&type))) controlType = type;
        IUIAutomationValuePattern* value = nullptr;
        int result = 0;
        if (SUCCEEDED(element->GetCurrentPatternAs(UIA_ValuePatternId,
                                                   IID_IUIAutomationValuePattern,
                                                   reinterpret_cast<void**>(&value))) &&
            value != nullptr) {
            BOOL readOnly = FALSE;
            if (SUCCEEDED(value->get_CurrentIsReadOnly(&readOnly))) result = readOnly ? 2 : 1;
            value->Release();
        }
        element->Release();
        return result;
    }

private:
    IUIAutomation* automation_ = nullptr;
};

std::optional<ScreenRect> guiThreadCaret() {
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) return std::nullopt;
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    if (!GetGUIThreadInfo(GetWindowThreadProcessId(foreground, nullptr), &info))
        return std::nullopt;
    if (info.hwndCaret == nullptr || (info.rcCaret.bottom - info.rcCaret.top) <= 0) {
        return std::nullopt;
    }
    POINT tl{info.rcCaret.left, info.rcCaret.top};
    POINT br{info.rcCaret.right, info.rcCaret.bottom};
    ClientToScreen(info.hwndCaret, &tl);
    ClientToScreen(info.hwndCaret, &br);
    return ScreenRect{tl.x, tl.y, (std::max)(1L, br.x - tl.x), br.y - tl.y};
}

// Microsoft Active Accessibility, the pre-UIA interface: the system caret object of the
// focused window. Toolkits that never adopted UIA still report the caret here.
std::optional<ScreenRect> msaaCaret() {
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) return std::nullopt;
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    HWND target = foreground;
    if (GetGUIThreadInfo(GetWindowThreadProcessId(foreground, nullptr), &info) &&
        info.hwndFocus != nullptr) {
        target = info.hwndFocus;
    }
    IAccessible* caret = nullptr;
    if (FAILED(AccessibleObjectFromWindow(target, static_cast<DWORD>(OBJID_CARET), IID_IAccessible,
                                          reinterpret_cast<void**>(&caret))) ||
        caret == nullptr) {
        return std::nullopt;
    }
    VARIANT self;
    VariantInit(&self);
    self.vt = VT_I4;
    self.lVal = CHILDID_SELF;
    long left = 0;
    long top = 0;
    long width = 0;
    long height = 0;
    const HRESULT hr = caret->accLocation(&left, &top, &width, &height, self);
    caret->Release();
    if (FAILED(hr) || height <= 0) return std::nullopt;
    return ScreenRect{left, top, (std::max)(1L, width), height};
}

} // namespace

const char* CaretResolver::sourceName(Source s) noexcept {
    switch (s) {
    case Source::GuiThread:
        return "gui";
    case Source::UiaCaretRange:
        return "uia-caret";
    case Source::UiaSelection:
        return "uia-selection";
    case Source::Msaa:
        return "msaa";
    case Source::NoTextFocus:
        return "no-text-focus";
    case Source::None:
        break;
    }
    return "none";
}

const char* CaretResolver::focusName(TextFocus f) noexcept {
    switch (f) {
    case TextFocus::Editable:
        return "editable";
    case TextFocus::NotText:
        return "not-text";
    case TextFocus::Unknown:
        break;
    }
    return "unknown";
}

// Measured 2026-09-29, not guessed: each of these was the focused element at a moment when
// keystrokes demonstrably went nowhere - VS Code's file tree (TreeItem) and status bar
// (StatusBar), a WinForms push button (Button), the group a web view falls back to.
//
// Deliberately absent, because typing DOES land there: Pane (Windows Terminal, Slack),
// Window, Document (Notepad, browsers), Edit (every text box), Custom and DataItem (older
// or unusual toolkits that say nothing useful). Anything not listed stays "unknown", and
// unknown never suppresses anything.
bool CaretResolver::isNonTextControl(int controlType) noexcept {
    switch (controlType) {
    case UIA_ButtonControlTypeId:
    case UIA_CheckBoxControlTypeId:
    case UIA_RadioButtonControlTypeId:
    case UIA_HyperlinkControlTypeId:
    case UIA_ImageControlTypeId:
    case UIA_ListItemControlTypeId:
    case UIA_ListControlTypeId:
    case UIA_MenuControlTypeId:
    case UIA_MenuBarControlTypeId:
    case UIA_MenuItemControlTypeId:
    case UIA_ProgressBarControlTypeId:
    case UIA_ScrollBarControlTypeId:
    case UIA_SliderControlTypeId:
    case UIA_StatusBarControlTypeId:
    case UIA_TabControlTypeId:
    case UIA_TabItemControlTypeId:
    case UIA_ToolBarControlTypeId:
    case UIA_ToolTipControlTypeId:
    case UIA_TreeControlTypeId:
    case UIA_TreeItemControlTypeId:
    case UIA_GroupControlTypeId:
    case UIA_ThumbControlTypeId:
    case UIA_TitleBarControlTypeId:
    case UIA_SeparatorControlTypeId:
    case UIA_SplitButtonControlTypeId:
    case UIA_HeaderControlTypeId:
    case UIA_HeaderItemControlTypeId:
        return true;
    default:
        return false;
    }
}

CaretResolver::TextFocus CaretResolver::textFocus() {
    static thread_local UiaCaret uia;
    const int verdict = uia.textFocus(lastControlType_);
    // A control that never takes text settles it whatever the patterns say.
    if (isNonTextControl(lastControlType_)) return TextFocus::NotText;
    switch (verdict) {
    case 1:
        return TextFocus::Editable;
    case 2:
        return TextFocus::NotText;
    default:
        return TextFocus::Unknown;
    }
}

std::optional<ScreenRect> CaretResolver::resolve() {
    // GetGUIThreadInfo first: when a classic control HAS a caret it is exact and free.
    if (auto rect = guiThreadCaret()) {
        lastSource_ = Source::GuiThread;
        return rect;
    }
    static thread_local UiaCaret uia;
    bool fromRange = false;
    UiaCaret::Caret state = UiaCaret::Caret::Unknown;
    if (auto rect = uia.resolve(state, fromRange)) {
        lastSource_ = fromRange ? Source::UiaCaretRange : Source::UiaSelection;
        return rect;
    }
    if (state == UiaCaret::Caret::Inactive) {
        // Told, not guessed: the keyboard is not in a text element. MSAA would only find
        // the same stale caret, so stop here and let the caller act on the difference
        // between "somewhere unknown" and "nowhere".
        lastSource_ = Source::NoTextFocus;
        return std::nullopt;
    }
    if (auto rect = msaaCaret()) {
        lastSource_ = Source::Msaa;
        return rect;
    }
    // Unknown. Deliberately NOT the mouse: the popup must relate to where text goes, and
    // the mouse is usually somewhere else entirely. The UI falls back to a fixed corner.
    lastSource_ = Source::None;
    return std::nullopt;
}

} // namespace lankey::platform::win32
