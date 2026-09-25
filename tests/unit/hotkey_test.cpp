#include <gtest/gtest.h>

#include "core/model/Hotkey.h"

namespace lankey::core::model {
namespace {

TEST(Hotkey, FormatsModifiersInFixedOrder) {
    EXPECT_EQ(formatHotkey(Hotkey{Modifier::Alt | Modifier::Control, VirtualKey::F}), "Ctrl+Alt+F");
    EXPECT_EQ(formatHotkey(
                  Hotkey{Modifier::Control | Modifier::Shift | Modifier::Win, VirtualKey::Digit1}),
              "Ctrl+Shift+Win+1");
    EXPECT_EQ(formatHotkey(Hotkey{}), "");
}

TEST(Hotkey, ParsesCaseInsensitivelyAndTrimsSpaces) {
    const auto h = parseHotkey(" ctrl + ALT + f ");
    ASSERT_TRUE(h.has_value());
    EXPECT_EQ(h->mods, Modifier::Control | Modifier::Alt);
    EXPECT_EQ(h->key, VirtualKey::F);
}

TEST(Hotkey, RejectsMissingKeyOrModifierAndNonAlnumKeys) {
    EXPECT_FALSE(parseHotkey("Ctrl+Alt").has_value());
    EXPECT_FALSE(parseHotkey("F").has_value());
    EXPECT_FALSE(parseHotkey("Ctrl+Alt+Enter").has_value());
    EXPECT_FALSE(parseHotkey("Ctrl+A+B").has_value());
    EXPECT_FALSE(parseHotkey("").has_value());
}

TEST(Hotkey, RoundTrips) {
    for (const char* s : {"Ctrl+Alt+L", "Ctrl+Alt+F", "Ctrl+Shift+Alt+V", "Alt+9"}) {
        const auto h = parseHotkey(s);
        ASSERT_TRUE(h.has_value()) << s;
        EXPECT_EQ(formatHotkey(*h), s);
    }
}

TEST(HotkeySettings, DefaultsAndIndexing) {
    HotkeySettings s;
    EXPECT_EQ(formatHotkey(s.convertLanguage), "Ctrl+Alt+L");
    EXPECT_EQ(formatHotkey(s.convertWidth), "Ctrl+Alt+F");
    EXPECT_EQ(formatHotkey(s.clipboardHistory), "Ctrl+Alt+V");
    EXPECT_EQ(formatHotkey(s.snippetPicker), "Ctrl+Alt+S");
    EXPECT_EQ(&s[HotkeyAction::ConvertWidth], &s.convertWidth);
}

TEST(HotkeySettings, DuplicatesResolveInActionOrder) {
    HotkeySettings s;
    s.clipboardHistory = *parseHotkey("Ctrl+Alt+F"); // same as convertWidth (earlier)
    s.resolveDuplicates();
    EXPECT_EQ(formatHotkey(s.convertWidth), "Ctrl+Alt+F");
    EXPECT_FALSE(s.clipboardHistory.assigned());
    EXPECT_EQ(formatHotkey(s.snippetPicker), "Ctrl+Alt+S");
}

} // namespace
} // namespace lankey::core::model
