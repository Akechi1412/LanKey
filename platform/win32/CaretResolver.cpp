#include "platform/win32/CaretResolver.h"

#include <objbase.h>
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
    [[nodiscard]] std::optional<ScreenRect> resolve() const {
        if (automation_ == nullptr) return std::nullopt;
        IUIAutomationElement* element = nullptr;
        if (FAILED(automation_->GetFocusedElement(&element)) || element == nullptr) {
            return std::nullopt;
        }
        std::optional<ScreenRect> rect = fromCaretRange(element);
        if (!rect) rect = fromSelection(element);
        element->Release();
        return rect;
    }

private:
    // TextPattern2::GetCaretRange - the precise caret, supported by modern editors
    // (browsers, Office, WPF/WinUI, Notepad on Windows 11).
    [[nodiscard]] static std::optional<ScreenRect> fromCaretRange(IUIAutomationElement* element) {
        IUIAutomationTextPattern2* text = nullptr;
        if (FAILED(element->GetCurrentPatternAs(UIA_TextPattern2Id, IID_IUIAutomationTextPattern2,
                                                reinterpret_cast<void**>(&text))) ||
            text == nullptr) {
            return std::nullopt;
        }
        BOOL active = FALSE;
        IUIAutomationTextRange* range = nullptr;
        std::optional<ScreenRect> rect;
        if (SUCCEEDED(text->GetCaretRange(&active, &range)) && range != nullptr) {
            rect = boundsOfCaretRange(range);
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

} // namespace

std::optional<ScreenRect> CaretResolver::resolve() {
    // GetGUIThreadInfo first: when a classic control HAS a caret it is exact and free.
    if (auto rect = guiThreadCaret()) return rect;
    static thread_local UiaCaret uia;
    if (auto rect = uia.resolve()) return rect;
    // Unknown. Deliberately NOT the mouse: the popup must relate to where text goes, and
    // the mouse is usually somewhere else entirely. The UI falls back to a fixed corner.
    return std::nullopt;
}

} // namespace lankey::platform::win32
