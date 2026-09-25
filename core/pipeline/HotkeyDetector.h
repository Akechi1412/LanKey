#pragma once

#include <cstdint>
#include <optional>

#include "core/model/Hotkey.h"
#include "core/model/KeyEvent.h"

namespace lankey::core::pipeline {

// Matches the configured chords on key-down: the key must be the chord's key and the set
// of held Ctrl/Shift/Alt/Win must equal the chord's exactly (CapsLock is ignored). Keys
// LanKey injected itself never match. Hook thread only; configure() runs there too (App
// posts settings changes through KeyboardHook::post), so no locking is needed.
class HotkeyDetector {
public:
    void configure(const model::HotkeySettings& settings) noexcept { settings_ = settings; }

    [[nodiscard]] std::optional<model::HotkeyAction>
    onKey(const model::KeyEvent& key) const noexcept {
        if (!key.isDown || key.injectedBySelf) return std::nullopt;
        constexpr auto kChordMask = model::Modifier::Control | model::Modifier::Shift |
                                    model::Modifier::Alt | model::Modifier::Win;
        const auto held = static_cast<model::Modifier>(static_cast<std::uint8_t>(key.modifiers) &
                                                       static_cast<std::uint8_t>(kChordMask));
        if (held == model::Modifier::None) return std::nullopt;
        for (const auto action : model::kAllHotkeyActions) {
            const model::Hotkey& h = settings_[action];
            if (h.assigned() && h.key == key.key && h.mods == held) return action;
        }
        return std::nullopt;
    }

private:
    model::HotkeySettings settings_;
};

} // namespace lankey::core::pipeline
