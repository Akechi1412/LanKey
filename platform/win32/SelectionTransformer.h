#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "platform/win32/Win32.h"

namespace lankey::platform::win32 {

// Applies a text function to whatever is selected in the foreground application, the way
// every "convert the selection" tool does it: Ctrl+C, read the clipboard, Ctrl+V the
// result, then put the user's clipboard back. Works in any application with copy/paste;
// UI Automation would be cleaner but only a minority of applications expose their text.
//
// UI thread only. The keys it sends carry kLanKeyMagic so the keyboard hook ignores them,
// and the temporary clipboard entries carry the "exclude from history" format so neither
// Win+V nor LanKey's own history records them.
class SelectionTransformer {
public:
    using Transform = std::function<std::optional<std::u32string>(std::u32string_view)>;

    enum class Result {
        Replaced,    // the selection was replaced with fn's result
        NoSelection, // Ctrl+C changed nothing: nothing was selected (clipboard untouched)
        Unchanged,   // fn returned nullopt or the same text (clipboard restored)
        Failed,      // clipboard could not be opened/read, or a transform is already running
    };

    Result transform(const Transform& fn);
    // Pastes `text` at the caret; the user's clipboard is restored afterwards.
    Result paste(std::u32string_view text);

    [[nodiscard]] bool busy() const noexcept { return busy_.load(); }

private:
    struct Saved {
        bool hadText = false;
        std::wstring text;
    };
    static Saved saveClipboard();
    static void restoreClipboard(const Saved& saved);
    static bool setClipboardText(std::wstring_view text, bool hideFromHistory);
    static std::optional<std::wstring> readClipboardText();
    static void releaseHeldModifiers();
    static void sendControlChord(WORD vk);
    static bool waitForClipboardChange(DWORD before, DWORD timeoutMs);

    std::atomic<bool> busy_{false};
};

} // namespace lankey::platform::win32
