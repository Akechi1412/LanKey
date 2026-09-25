#include <gtest/gtest.h>

#include "core/pipeline/HotkeyDetector.h"

namespace lankey::core::pipeline {
namespace {

using model::HotkeyAction;
using model::KeyEvent;
using model::Modifier;
using model::VirtualKey;

KeyEvent down(VirtualKey k, Modifier m) {
    KeyEvent e;
    e.key = k;
    e.modifiers = m;
    e.isDown = true;
    return e;
}

TEST(HotkeyDetector, FiresOnExactChordDown) {
    HotkeyDetector d;
    d.configure({});
    EXPECT_EQ(d.onKey(down(VirtualKey::F, Modifier::Control | Modifier::Alt)),
              HotkeyAction::ConvertWidth);
    EXPECT_EQ(d.onKey(down(VirtualKey::L, Modifier::Control | Modifier::Alt)),
              HotkeyAction::ConvertLanguage);
    EXPECT_EQ(d.onKey(down(VirtualKey::V, Modifier::Control | Modifier::Alt)),
              HotkeyAction::ClipboardHistory);
    EXPECT_EQ(d.onKey(down(VirtualKey::S, Modifier::Control | Modifier::Alt)),
              HotkeyAction::SnippetPicker);
}

TEST(HotkeyDetector, ExtraOrMissingModifierDoesNotFire) {
    HotkeyDetector d;
    d.configure({});
    EXPECT_FALSE(d.onKey(down(VirtualKey::F, Modifier::Control)).has_value());
    EXPECT_FALSE(d.onKey(down(VirtualKey::F, Modifier::Control | Modifier::Alt | Modifier::Shift))
                     .has_value());
    EXPECT_FALSE(d.onKey(down(VirtualKey::F, Modifier::None)).has_value());
    EXPECT_FALSE(d.onKey(down(VirtualKey::G, Modifier::Control | Modifier::Alt)).has_value());
}

TEST(HotkeyDetector, CapsLockIgnoredKeyUpAndInjectedNot) {
    HotkeyDetector d;
    d.configure({});
    EXPECT_TRUE(d.onKey(down(VirtualKey::F, Modifier::Control | Modifier::Alt | Modifier::CapsLock))
                    .has_value());
    KeyEvent up = down(VirtualKey::F, Modifier::Control | Modifier::Alt);
    up.isDown = false;
    EXPECT_FALSE(d.onKey(up).has_value());
    KeyEvent injected = down(VirtualKey::F, Modifier::Control | Modifier::Alt);
    injected.injectedBySelf = true;
    EXPECT_FALSE(d.onKey(injected).has_value());
}

TEST(HotkeyDetector, UnassignedNeverFiresAndReconfigureTakesEffect) {
    model::HotkeySettings s;
    s.convertWidth = {};
    HotkeyDetector d;
    d.configure(s);
    EXPECT_FALSE(d.onKey(down(VirtualKey::F, Modifier::Control | Modifier::Alt)).has_value());
    s.convertWidth = *model::parseHotkey("Alt+9");
    d.configure(s);
    EXPECT_EQ(d.onKey(down(VirtualKey::Digit9, Modifier::Alt)), HotkeyAction::ConvertWidth);
}

} // namespace
} // namespace lankey::core::pipeline
