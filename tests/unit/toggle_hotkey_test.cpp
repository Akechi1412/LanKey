#include <gtest/gtest.h>

#include "core/pipeline/ToggleHotkey.h"

namespace lankey::core::pipeline {
namespace {

using model::KeyEvent;
using model::Modifier;
using model::VirtualKey;

KeyEvent key(VirtualKey k, bool down, Modifier mods = Modifier::None) {
    KeyEvent e;
    e.key = k;
    e.isDown = down;
    e.modifiers = mods;
    return e;
}

TEST(ToggleHotkey, CtrlThenShiftReleasedFires) {
    ToggleHotkey h;
    EXPECT_FALSE(h.onKey(key(VirtualKey::Control, true, Modifier::Control)));
    EXPECT_FALSE(h.onKey(key(VirtualKey::Shift, true, Modifier::Control | Modifier::Shift)));
    EXPECT_TRUE(h.onKey(key(VirtualKey::Shift, false, Modifier::Control)));
    // Releasing the second key must not fire again.
    EXPECT_FALSE(h.onKey(key(VirtualKey::Control, false)));
}

TEST(ToggleHotkey, ShiftThenCtrlAlsoFires) {
    ToggleHotkey h;
    (void)h.onKey(key(VirtualKey::Shift, true, Modifier::Shift));
    (void)h.onKey(key(VirtualKey::Control, true, Modifier::Control | Modifier::Shift));
    EXPECT_TRUE(h.onKey(key(VirtualKey::Control, false, Modifier::Shift)));
}

TEST(ToggleHotkey, AnotherKeyInBetweenDisarms) {
    ToggleHotkey h;
    (void)h.onKey(key(VirtualKey::Control, true, Modifier::Control));
    (void)h.onKey(key(VirtualKey::Shift, true, Modifier::Control | Modifier::Shift));
    (void)h.onKey(key(VirtualKey::S, true, Modifier::Control | Modifier::Shift)); // Ctrl+Shift+S
    (void)h.onKey(key(VirtualKey::S, false, Modifier::Control | Modifier::Shift));
    EXPECT_FALSE(h.onKey(key(VirtualKey::Shift, false, Modifier::Control)));
    EXPECT_FALSE(h.onKey(key(VirtualKey::Control, false)));
}

TEST(ToggleHotkey, SingleModifierNeverFires) {
    ToggleHotkey h;
    (void)h.onKey(key(VirtualKey::Shift, true, Modifier::Shift));
    EXPECT_FALSE(h.onKey(key(VirtualKey::Shift, false)));
    (void)h.onKey(key(VirtualKey::Control, true, Modifier::Control));
    EXPECT_FALSE(h.onKey(key(VirtualKey::Control, false)));
}

TEST(ToggleHotkey, KeyRepeatWhileHeldDoesNotSpoil) {
    ToggleHotkey h;
    (void)h.onKey(key(VirtualKey::Control, true, Modifier::Control));
    (void)h.onKey(key(VirtualKey::Shift, true, Modifier::Control | Modifier::Shift));
    (void)h.onKey(key(VirtualKey::Shift, true, Modifier::Control | Modifier::Shift)); // auto-repeat
    EXPECT_TRUE(h.onKey(key(VirtualKey::Shift, false, Modifier::Control)));
}

} // namespace
} // namespace lankey::core::pipeline
