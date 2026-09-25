#pragma once

#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/model/KeyEvent.h"

namespace lankey::core::model {

// A global chord: one letter or digit plus at least one of Ctrl/Alt/Shift/Win. Serialised
// as "Ctrl+Alt+F" (modifiers in that fixed order). An unassigned hotkey has key == Unknown
// and formats as "".
struct Hotkey {
    Modifier mods = Modifier::None;
    VirtualKey key = VirtualKey::Unknown;

    [[nodiscard]] constexpr bool assigned() const noexcept { return key != VirtualKey::Unknown; }
    friend constexpr bool operator==(const Hotkey&, const Hotkey&) = default;
};

inline std::string formatHotkey(const Hotkey& h) {
    if (!h.assigned()) return "";
    std::string out;
    if (has(h.mods, Modifier::Control)) out += "Ctrl+";
    if (has(h.mods, Modifier::Shift)) out += "Shift+";
    if (has(h.mods, Modifier::Alt)) out += "Alt+";
    if (has(h.mods, Modifier::Win)) out += "Win+";
    out += static_cast<char>(static_cast<int>(h.key)); // VK_A..Z and VK_0..9 are ASCII
    return out;
}

// "ctrl + alt + f" -> Ctrl+Alt+F. nullopt when there is no key, no modifier, two keys, or
// an unknown word; only letters and digits are accepted as the key.
inline std::optional<Hotkey> parseHotkey(std::string_view text) {
    Hotkey h;
    bool sawKey = false;
    std::size_t start = 0;
    while (true) {
        std::size_t end = text.find('+', start);
        if (end == std::string_view::npos) end = text.size();
        std::string part;
        for (const char c : text.substr(start, end - start)) {
            if (c != ' ' && c != '\t') {
                part += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
        }
        if (part == "ctrl" || part == "control") {
            h.mods = h.mods | Modifier::Control;
        } else if (part == "shift") {
            h.mods = h.mods | Modifier::Shift;
        } else if (part == "alt") {
            h.mods = h.mods | Modifier::Alt;
        } else if (part == "win") {
            h.mods = h.mods | Modifier::Win;
        } else if (part.size() == 1 &&
                   ((part[0] >= 'a' && part[0] <= 'z') || (part[0] >= '0' && part[0] <= '9'))) {
            if (sawKey) return std::nullopt;
            sawKey = true;
            h.key = static_cast<VirtualKey>(std::toupper(static_cast<unsigned char>(part[0])));
        } else {
            return std::nullopt;
        }
        if (end == text.size()) break;
        start = end + 1;
    }
    if (!sawKey || h.mods == Modifier::None) return std::nullopt;
    return h;
}

enum class HotkeyAction { ConvertLanguage, ConvertWidth, ClipboardHistory, SnippetPicker };
inline constexpr int kHotkeyActionCount = 4;
inline constexpr std::array<HotkeyAction, kHotkeyActionCount> kAllHotkeyActions = {
    HotkeyAction::ConvertLanguage, HotkeyAction::ConvertWidth, HotkeyAction::ClipboardHistory,
    HotkeyAction::SnippetPicker};

// JSON key of each action.
inline constexpr const char* hotkeyActionName(HotkeyAction a) noexcept {
    switch (a) {
    case HotkeyAction::ConvertLanguage:
        return "convertLanguage";
    case HotkeyAction::ConvertWidth:
        return "convertWidth";
    case HotkeyAction::ClipboardHistory:
        return "clipboardHistory";
    case HotkeyAction::SnippetPicker:
        return "snippetPicker";
    }
    return "";
}

struct HotkeySettings {
    Hotkey convertLanguage{Modifier::Control | Modifier::Alt, VirtualKey::L};
    Hotkey convertWidth{Modifier::Control | Modifier::Alt, VirtualKey::F};
    Hotkey clipboardHistory{Modifier::Control | Modifier::Alt, VirtualKey::V};
    Hotkey snippetPicker{Modifier::Control | Modifier::Alt, VirtualKey::S};

    Hotkey& operator[](HotkeyAction a) noexcept {
        switch (a) {
        case HotkeyAction::ConvertLanguage:
            return convertLanguage;
        case HotkeyAction::ConvertWidth:
            return convertWidth;
        case HotkeyAction::ClipboardHistory:
            return clipboardHistory;
        case HotkeyAction::SnippetPicker:
            return snippetPicker;
        }
        return convertLanguage;
    }
    const Hotkey& operator[](HotkeyAction a) const noexcept {
        return const_cast<HotkeySettings&>(*this)[a];
    }

    // Two actions on one chord: the later action (enum order) becomes unassigned.
    void resolveDuplicates() noexcept {
        for (std::size_t i = 1; i < kAllHotkeyActions.size(); ++i) {
            Hotkey& later = (*this)[kAllHotkeyActions[i]];
            for (std::size_t j = 0; j < i && later.assigned(); ++j) {
                if (later == (*this)[kAllHotkeyActions[j]]) later = {};
            }
        }
    }

    friend constexpr bool operator==(const HotkeySettings&, const HotkeySettings&) = default;
};

} // namespace lankey::core::model
