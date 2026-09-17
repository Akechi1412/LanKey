#pragma once

#include "core/model/KeyEvent.h"

namespace lankey::core::pipeline {

// Detects the classic Vietnamese IME toggle: press Ctrl and Shift together, release, with
// no other key in between. Firing on RELEASE is what lets Ctrl+Shift+S etc. keep working:
// any other key while the chord is held disarms it.
//
// Pure state machine over KeyEvents (needs key-ups, which the hook delivers and the
// pipeline ignores). Hook thread only.
class ToggleHotkey {
public:
    // Returns true when the chord completed and the language should toggle.
    [[nodiscard]] bool onKey(const model::KeyEvent& key) noexcept {
        using model::Modifier;
        using model::VirtualKey;
        const bool isChordKey = key.key == VirtualKey::Shift || key.key == VirtualKey::Control;
        if (key.isDown) {
            if (isChordKey) {
                const bool otherHeld = key.key == VirtualKey::Shift
                                           ? has(key.modifiers, Modifier::Control)
                                           : has(key.modifiers, Modifier::Shift);
                if (otherHeld && !armed_) {
                    armed_ = true;
                    spoiled_ = false;
                }
            } else if (armed_) {
                spoiled_ = true;
            }
            return false;
        }
        if (!isChordKey || !armed_) return false;
        armed_ = false;
        return !spoiled_;
    }

private:
    bool armed_ = false;   // both chord keys have been down together
    bool spoiled_ = false; // another key was pressed meanwhile
};

} // namespace lankey::core::pipeline
