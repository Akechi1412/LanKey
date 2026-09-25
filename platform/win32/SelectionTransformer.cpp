#include "platform/win32/SelectionTransformer.h"

#include <algorithm>

namespace lankey::platform::win32 {

namespace {

constexpr wchar_t kExcludeFormat[] = L"ExcludeClipboardContentFromMonitorProcessing";
constexpr DWORD kCopyTimeoutMs = 300; // the application had this long to service Ctrl+C
constexpr DWORD kPasteSettleMs = 150; // ... and this long to read the clipboard on Ctrl+V
constexpr int kOpenClipboardRetries = 10;
constexpr DWORD kReleaseWaitMs = 800; // how long the user may keep holding the hotkey
constexpr DWORD kAfterReleaseMs = 30;

// OpenClipboard fails while another process holds it (briefly, after every copy).
struct ClipboardLock {
    bool ok = false;
    ClipboardLock() {
        for (int i = 0; i < kOpenClipboardRetries && !ok; ++i) {
            ok = OpenClipboard(nullptr) != 0;
            if (!ok) Sleep(5);
        }
    }
    ~ClipboardLock() {
        if (ok) CloseClipboard();
    }
    ClipboardLock(const ClipboardLock&) = delete;
    ClipboardLock& operator=(const ClipboardLock&) = delete;
};

INPUT taggedKey(WORD vk, bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    in.ki.dwExtraInfo = kLanKeyMagic;
    return in;
}

} // namespace

std::optional<std::wstring> SelectionTransformer::readClipboardText() {
    ClipboardLock lock;
    if (!lock.ok) return std::nullopt;
    const HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h == nullptr) return std::nullopt;
    const auto* p = static_cast<const wchar_t*>(GlobalLock(h));
    if (p == nullptr) return std::nullopt;
    std::wstring text(p);
    GlobalUnlock(h);
    return text;
}

bool SelectionTransformer::setClipboardText(std::wstring_view text, bool hideFromHistory) {
    ClipboardLock lock;
    if (!lock.ok) return false;
    EmptyClipboard();
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    const HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem == nullptr) return false;
    auto* dst = static_cast<wchar_t*>(GlobalLock(mem));
    if (dst == nullptr) {
        GlobalFree(mem);
        return false;
    }
    std::copy(text.begin(), text.end(), dst);
    dst[text.size()] = 0;
    GlobalUnlock(mem);
    if (SetClipboardData(CF_UNICODETEXT, mem) == nullptr) {
        GlobalFree(mem);
        return false;
    }
    if (hideFromHistory) {
        // Win+V (and LanKey's own history) skip entries carrying this format, whatever
        // its value.
        if (const UINT fmt = RegisterClipboardFormatW(kExcludeFormat); fmt != 0) {
            if (const HGLOBAL flag = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD)); flag != nullptr) {
                if (auto* v = static_cast<DWORD*>(GlobalLock(flag)); v != nullptr) {
                    *v = 1;
                    GlobalUnlock(flag);
                    if (SetClipboardData(fmt, flag) == nullptr) GlobalFree(flag);
                } else {
                    GlobalFree(flag);
                }
            }
        }
    }
    return true;
}

SelectionTransformer::Saved SelectionTransformer::saveClipboard() {
    Saved s;
    if (auto t = readClipboardText()) {
        s.hadText = true;
        s.text = std::move(*t);
    }
    return s;
}

void SelectionTransformer::restoreClipboard(const Saved& saved) {
    // Restored content is hidden from history too: it was already recorded when the user
    // copied it. A non-text clipboard (image, files) cannot be put back: it is emptied.
    if (saved.hadText) {
        setClipboardText(saved.text, /*hideFromHistory=*/true);
        return;
    }
    ClipboardLock lock;
    if (lock.ok) EmptyClipboard();
}

void SelectionTransformer::releaseHeldModifiers() {
    // The user is still holding the hotkey's modifiers when this runs (the hook posted the
    // action within a millisecond of the key-down). Sending Ctrl+C now would arrive as
    // Ctrl+Alt+C, and a key-up landing between our Ctrl-down and C-down would turn it into
    // a plain "c". So: wait for the physical release (a normal chord is let go within a
    // few hundred milliseconds), and only if the user keeps holding, release the keys
    // ourselves (tagged, so the hook ignores the key-ups).
    constexpr int kModifiers[] = {VK_MENU, VK_SHIFT, VK_LWIN, VK_RWIN, VK_CONTROL};
    const auto anyHeld = [&] {
        for (const int vk : kModifiers) {
            if ((GetAsyncKeyState(vk) & 0x8000) != 0) return true;
        }
        return false;
    };
    const ULONGLONG start = GetTickCount64();
    while (anyHeld() && GetTickCount64() - start < kReleaseWaitMs)
        Sleep(10);
    for (const int vk : kModifiers) {
        if ((GetAsyncKeyState(vk) & 0x8000) != 0) {
            INPUT up = taggedKey(static_cast<WORD>(vk), false);
            SendInput(1, &up, sizeof(INPUT));
        }
    }
    Sleep(kAfterReleaseMs); // let the last key-up reach the application before our chord
}

void SelectionTransformer::sendControlChord(WORD vk) {
    INPUT in[4] = {taggedKey(VK_CONTROL, true), taggedKey(vk, true), taggedKey(vk, false),
                   taggedKey(VK_CONTROL, false)};
    SendInput(4, in, sizeof(INPUT));
}

bool SelectionTransformer::waitForClipboardChange(DWORD before, DWORD timeoutMs) {
    const ULONGLONG start = GetTickCount64();
    while (GetTickCount64() - start < timeoutMs) {
        if (GetClipboardSequenceNumber() != before) return true;
        Sleep(10);
    }
    return false;
}

SelectionTransformer::Result SelectionTransformer::transform(const Transform& fn) {
    if (busy_.exchange(true)) return Result::Failed;
    struct Reset {
        std::atomic<bool>& flag;
        ~Reset() { flag.store(false); }
    } reset{busy_};

    releaseHeldModifiers();
    const Saved saved = saveClipboard();
    const DWORD before = GetClipboardSequenceNumber();
    sendControlChord('C');
    if (!waitForClipboardChange(before, kCopyTimeoutMs)) {
        return Result::NoSelection; // nothing copied: the clipboard was never touched
    }
    const auto copied = readClipboardText();
    if (!copied) {
        restoreClipboard(saved);
        return Result::Failed;
    }
    const std::u32string original = fromUtf16(*copied);
    const auto result = fn(original);
    if (!result || *result == original) {
        restoreClipboard(saved);
        return Result::Unchanged;
    }
    if (!setClipboardText(toUtf16(*result), /*hideFromHistory=*/true)) {
        restoreClipboard(saved);
        return Result::Failed;
    }
    sendControlChord('V');
    Sleep(kPasteSettleMs); // the application reads the clipboard when it handles the paste
    restoreClipboard(saved);
    return Result::Replaced;
}

SelectionTransformer::Result SelectionTransformer::paste(std::u32string_view text) {
    if (busy_.exchange(true)) return Result::Failed;
    struct Reset {
        std::atomic<bool>& flag;
        ~Reset() { flag.store(false); }
    } reset{busy_};

    releaseHeldModifiers();
    const Saved saved = saveClipboard();
    if (!setClipboardText(toUtf16(text), /*hideFromHistory=*/true)) return Result::Failed;
    sendControlChord('V');
    Sleep(kPasteSettleMs);
    restoreClipboard(saved);
    return Result::Replaced;
}

} // namespace lankey::platform::win32
