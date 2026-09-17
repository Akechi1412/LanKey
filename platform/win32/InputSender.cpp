#include "platform/win32/InputSender.h"

#include <vector>

namespace lankey::platform::win32 {

namespace {

INPUT keyEvent(WORD vk, bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    in.ki.dwExtraInfo = kLanKeyMagic;
    return in;
}

INPUT unicodeEvent(wchar_t unit, bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = unit;
    in.ki.dwFlags = KEYEVENTF_UNICODE | (down ? 0 : KEYEVENTF_KEYUP);
    in.ki.dwExtraInfo = kLanKeyMagic;
    return in;
}

void appendPress(std::vector<INPUT>& out, WORD vk) {
    out.push_back(keyEvent(vk, true));
    out.push_back(keyEvent(vk, false));
}

void appendChar(std::vector<INPUT>& out, char32_t cp) {
    // Control characters come from Undo restoring a terminator; send them as real keys so
    // the application reacts as it would to the user pressing them.
    switch (cp) {
    case U'\n':
        appendPress(out, VK_RETURN);
        return;
    case U'\t':
        appendPress(out, VK_TAB);
        return;
    default:
        break;
    }
    const std::wstring units = toUtf16(std::u32string_view(&cp, 1));
    for (const wchar_t u : units) {
        out.push_back(unicodeEvent(u, true));
        out.push_back(unicodeEvent(u, false));
    }
}

} // namespace

void InputSender::apply(const core::model::TextReplacement& replacement) {
    std::vector<INPUT> inputs;
    inputs.reserve(static_cast<std::size_t>(replacement.deleteCount) * 2 +
                   replacement.insert.size() * 2);
    for (int i = 0; i < replacement.deleteCount; ++i)
        appendPress(inputs, VK_BACK);
    for (const char32_t cp : replacement.insert)
        appendChar(inputs, cp);
    if (inputs.empty()) return;

    if (strategy_.load() == Strategy::Batch) {
        SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    } else {
        // Down+up pairs one at a time. The pause is what keeps slow consumers in order.
        for (std::size_t i = 0; i + 1 < inputs.size(); i += 2) {
            SendInput(2, &inputs[i], sizeof(INPUT));
            Sleep(1);
        }
    }
    eventsSent_.fetch_add(inputs.size(), std::memory_order_relaxed);
}

} // namespace lankey::platform::win32
