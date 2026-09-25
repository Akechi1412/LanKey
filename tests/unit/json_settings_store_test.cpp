#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "core/storage/JsonSettingsStore.h"

namespace lankey::core::storage {
namespace {

using model::AutoCorrectLevel;
using model::InputMethod;
using model::Settings;

TEST(JsonSettingsStore, RoundTripsEveryField) {
    Settings s;
    s.vietnameseEnabled = false;
    s.engine.inputMethod = InputMethod::Vni;
    s.engine.modernToneMark = false;
    s.engine.spellCheck = false;
    s.engine.quickTelex = true;
    s.suggestions.enabled = false;
    s.suggestions.minPrefixLength = 3;
    s.suggestions.weightRecency = 0.25;
    s.suggestions.selectWithDigits = true;
    s.suggestions.selectWithEnter = true;
    s.autoCorrect.level = AutoCorrectLevel::Aggressive;
    s.engine.inputMethod = model::InputMethod::Custom;
    s.engine.customKeys = "1,2,3,4,5,6,6,7,8[,9,0,,";
    s.engine.codeTable = model::CodeTable::Tcvn3;
    s.autoCorrect.excludedApps = {"x.exe"};
    s.advanced.sendKeys = model::SendKeysMode::KeyByKey;
    s.languageMemory.enabled = true;
    s.languageMemory.perApp = {{"code.exe", false}, {"notepad.exe", true}};
    s.privacy.excludedApps = {"a.exe", "b.exe"};
    s.privacy.suggestionsDisabledApps = {"c.exe"};
    s.hotkeys.convertWidth = *model::parseHotkey("Ctrl+Shift+Alt+W");
    s.hotkeys.snippetPicker = {}; // unassigned survives the round trip

    const auto back = JsonSettingsStore::parse(JsonSettingsStore::serialize(s));
    ASSERT_TRUE(back.has_value()) << back.error().message;
    EXPECT_EQ(*back, s);
}

TEST(JsonSettingsStore, MissingAndUnknownKeysKeepDefaults) {
    const auto s = JsonSettingsStore::parse(R"({"engine":{"inputMethod":"vni"},"future":42})");
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->engine.inputMethod, InputMethod::Vni);
    EXPECT_TRUE(s->engine.modernToneMark); // default kept
    EXPECT_EQ(s->suggestions.minPrefixLength, Settings{}.suggestions.minPrefixLength);
}

TEST(JsonSettingsStore, WrongTypesAndBadEnumsFallBackToDefaults) {
    const auto s = JsonSettingsStore::parse(
        R"({"vietnameseEnabled":"yes","engine":{"inputMethod":"morse"},"suggestions":{"minPrefixLength":99}})");
    ASSERT_TRUE(s.has_value());
    EXPECT_TRUE(s->vietnameseEnabled);
    EXPECT_EQ(s->engine.inputMethod, InputMethod::Telex);
    EXPECT_EQ(s->suggestions.minPrefixLength, 4); // clamped
}

TEST(JsonSettingsStore, HotkeysFallBackAndDuplicatesAreResolved) {
    const auto back = JsonSettingsStore::parse(
        R"({"hotkeys":{"convertWidth":"Ctrl+Alt","convertLanguage":"Ctrl+Alt+V"}})");
    ASSERT_TRUE(back.has_value()) << back.error().message;
    EXPECT_EQ(model::formatHotkey(back->hotkeys.convertWidth), "Ctrl+Alt+F"); // unparsable
    EXPECT_EQ(model::formatHotkey(back->hotkeys.convertLanguage), "Ctrl+Alt+V");
    EXPECT_FALSE(back->hotkeys.clipboardHistory.assigned()); // default clashed with the above
    EXPECT_EQ(model::formatHotkey(back->hotkeys.snippetPicker), "Ctrl+Alt+S");
}

TEST(JsonSettingsStore, CorruptFileIsAnError) {
    EXPECT_FALSE(JsonSettingsStore::parse("{not json").has_value());
    EXPECT_FALSE(JsonSettingsStore::parse("[1,2]").has_value());
}

TEST(JsonSettingsStore, LoadMissingFileGivesDefaultsAndSaveCreatesIt) {
    const auto dir = std::filesystem::temp_directory_path() / "lankey-settings-test";
    std::filesystem::remove_all(dir);
    const JsonSettingsStore store((dir / "settings.json").string());

    const auto fresh = store.load();
    ASSERT_TRUE(fresh.has_value());
    EXPECT_EQ(*fresh, Settings{});

    Settings s;
    s.engine.quickTelex = true;
    ASSERT_TRUE(store.save(s).has_value());
    const auto loaded = store.load();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->engine.quickTelex);
    EXPECT_FALSE(std::filesystem::exists(dir / "settings.json.tmp"));
    std::filesystem::remove_all(dir);
}

} // namespace
} // namespace lankey::core::storage
