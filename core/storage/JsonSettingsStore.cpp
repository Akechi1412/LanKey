#include "core/storage/JsonSettingsStore.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

namespace lankey::core::storage {

namespace {
constexpr int kSaveRenameAttempts = 5;
constexpr int kSaveRenameRetryMs = 40;
} // namespace

using model::AutoCorrectLevel;
using model::Error;
using model::InputMethod;
using model::SendKeysMode;
using model::Settings;
using nlohmann::json;

namespace {

const char* inputMethodName(InputMethod m) {
    switch (m) {
    case InputMethod::Telex:
        return "telex";
    case InputMethod::Vni:
        return "vni";
    case InputMethod::SimpleTelex:
        return "simple-telex";
    case InputMethod::Custom:
        return "custom";
    }
    return "telex";
}

InputMethod inputMethodFrom(const std::string& s, InputMethod fallback) {
    if (s == "telex") return InputMethod::Telex;
    if (s == "vni") return InputMethod::Vni;
    if (s == "simple-telex") return InputMethod::SimpleTelex;
    if (s == "custom") return InputMethod::Custom;
    return fallback;
}

const char* codeTableName(model::CodeTable c) {
    switch (c) {
    case model::CodeTable::Unicode:
        return "unicode";
    case model::CodeTable::UnicodeCompound:
        return "unicode-compound";
    case model::CodeTable::Tcvn3:
        return "tcvn3";
    case model::CodeTable::VniWindows:
        return "vni-windows";
    }
    return "unicode";
}

model::CodeTable codeTableFrom(const std::string& s, model::CodeTable fallback) {
    if (s == "unicode") return model::CodeTable::Unicode;
    if (s == "unicode-compound") return model::CodeTable::UnicodeCompound;
    if (s == "tcvn3") return model::CodeTable::Tcvn3;
    if (s == "vni-windows") return model::CodeTable::VniWindows;
    return fallback;
}

// Canonical form of a usable table; anything malformed -> the default (Telex).
std::string sanitizedCustomKeys(const std::string& keys) {
    if (const auto table = model::parseCustomKeys(keys)) return model::joinCustomKeys(*table);
    return model::kDefaultCustomKeys;
}

const char* levelName(AutoCorrectLevel l) {
    switch (l) {
    case AutoCorrectLevel::Off:
        return "off";
    case AutoCorrectLevel::Cautious:
        return "cautious";
    case AutoCorrectLevel::Balanced:
        return "balanced";
    case AutoCorrectLevel::Aggressive:
        return "aggressive";
    }
    return "cautious";
}

AutoCorrectLevel levelFrom(const std::string& s, AutoCorrectLevel fallback) {
    if (s == "off") return AutoCorrectLevel::Off;
    if (s == "cautious") return AutoCorrectLevel::Cautious;
    if (s == "balanced") return AutoCorrectLevel::Balanced;
    if (s == "aggressive") return AutoCorrectLevel::Aggressive;
    return fallback;
}

// Read `key` from `j` into `out` if present and of the right type; otherwise keep `out`.
template <class T>
void get(const json& j, const char* key, T& out) {
    if (const auto it = j.find(key); it != j.end()) {
        try {
            out = it->get<T>();
        } catch (const json::exception&) {
            // Wrong type: keep the default rather than failing the whole file.
        }
    }
}

} // namespace

JsonSettingsStore::JsonSettingsStore(std::string path) : path_(std::move(path)) {}

std::string JsonSettingsStore::serialize(const Settings& s) {
    json j;
    j["schemaVersion"] = Settings::kSchemaVersion;
    j["vietnameseEnabled"] = s.vietnameseEnabled;
    j["engine"] = {
        {"inputMethod", inputMethodName(s.engine.inputMethod)},
        {"modernToneMark", s.engine.modernToneMark},
        {"spellCheck", s.engine.spellCheck},
        {"quickTelex", s.engine.quickTelex},
        {"codeTable", codeTableName(s.engine.codeTable)},
        {"customKeys", s.engine.customKeys},
    };
    j["suggestions"] = {
        {"enabled", s.suggestions.enabled},
        {"minPrefixLength", s.suggestions.minPrefixLength},
        {"idleDelayMs", s.suggestions.idleDelayMs},
        {"selectWithDigits", s.suggestions.selectWithDigits},
        {"selectWithEnter", s.suggestions.selectWithEnter},
        {"weightFrequency", s.suggestions.weightFrequency},
        {"weightRecency", s.suggestions.weightRecency},
        {"weightAppContext", s.suggestions.weightAppContext},
        {"weightSavedKeystrokes", s.suggestions.weightSavedKeystrokes},
        {"weightPhraseLength", s.suggestions.weightPhraseLength},
        {"recencyLambda", s.suggestions.recencyLambda},
    };
    j["autoCorrect"] = {
        {"level", levelName(s.autoCorrect.level)},
        {"excludedApps", s.autoCorrect.excludedApps},
    };
    j["advanced"] = {
        {"sendKeys", s.advanced.sendKeys == SendKeysMode::KeyByKey ? "keyByKey" : "batch"},
    };
    {
        nlohmann::json perApp = nlohmann::json::object();
        for (const auto& [app, vietnamese] : s.languageMemory.perApp)
            perApp[app] = vietnamese;
        j["languageMemory"] = {{"enabled", s.languageMemory.enabled}, {"perApp", perApp}};
    }
    j["privacy"] = {
        {"excludedApps", s.privacy.excludedApps},
        {"suggestionsDisabledApps", s.privacy.suggestionsDisabledApps},
    };
    {
        nlohmann::json hotkeys = nlohmann::json::object();
        for (const auto a : model::kAllHotkeyActions)
            hotkeys[model::hotkeyActionName(a)] = model::formatHotkey(s.hotkeys[a]);
        j["hotkeys"] = hotkeys;
    }
    return j.dump(2) + "\n";
}

lk::expected<Settings> JsonSettingsStore::parse(const std::string& text) {
    json j;
    try {
        j = json::parse(text);
    } catch (const json::exception& e) {
        return lk::unexpected(Error::make(Error::Code::Corrupted, e.what()));
    }
    if (!j.is_object()) {
        return lk::unexpected(
            Error::make(Error::Code::Corrupted, "settings root is not an object"));
    }
    Settings s;
    get(j, "schemaVersion", s.schemaVersion);
    get(j, "vietnameseEnabled", s.vietnameseEnabled);
    if (const auto e = j.find("engine"); e != j.end() && e->is_object()) {
        std::string method;
        get(*e, "inputMethod", method);
        s.engine.inputMethod = inputMethodFrom(method, s.engine.inputMethod);
        {
            std::string codeTable;
            get(*e, "codeTable", codeTable);
            s.engine.codeTable = codeTableFrom(codeTable, s.engine.codeTable);
            std::string customKeys = s.engine.customKeys;
            get(*e, "customKeys", customKeys);
            s.engine.customKeys = sanitizedCustomKeys(customKeys);
        }
        get(*e, "modernToneMark", s.engine.modernToneMark);
        get(*e, "spellCheck", s.engine.spellCheck);
        get(*e, "quickTelex", s.engine.quickTelex);
    }
    if (const auto g = j.find("suggestions"); g != j.end() && g->is_object()) {
        auto& o = s.suggestions;
        get(*g, "enabled", o.enabled);
        get(*g, "minPrefixLength", o.minPrefixLength);
        get(*g, "idleDelayMs", o.idleDelayMs);
        get(*g, "selectWithDigits", o.selectWithDigits);
        get(*g, "selectWithEnter", o.selectWithEnter);
        get(*g, "weightFrequency", o.weightFrequency);
        get(*g, "weightRecency", o.weightRecency);
        get(*g, "weightAppContext", o.weightAppContext);
        get(*g, "weightSavedKeystrokes", o.weightSavedKeystrokes);
        get(*g, "weightPhraseLength", o.weightPhraseLength);
        get(*g, "recencyLambda", o.recencyLambda);
        if (o.minPrefixLength < 1) o.minPrefixLength = 1;
        if (o.minPrefixLength > 4) o.minPrefixLength = 4;
        if (o.idleDelayMs < 0) o.idleDelayMs = 0;
        if (o.idleDelayMs > 5000) o.idleDelayMs = 5000;
    }
    if (const auto a = j.find("autoCorrect"); a != j.end() && a->is_object()) {
        std::string level;
        get(*a, "level", level);
        s.autoCorrect.level = levelFrom(level, s.autoCorrect.level);
        get(*a, "excludedApps", s.autoCorrect.excludedApps);
    }
    if (const auto a = j.find("advanced"); a != j.end() && a->is_object()) {
        std::string mode;
        get(*a, "sendKeys", mode);
        s.advanced.sendKeys = mode == "keyByKey" ? SendKeysMode::KeyByKey : SendKeysMode::Batch;
    }
    if (const auto m = j.find("languageMemory"); m != j.end() && m->is_object()) {
        get(*m, "enabled", s.languageMemory.enabled);
        if (const auto p = m->find("perApp"); p != m->end() && p->is_object()) {
            s.languageMemory.perApp.clear();
            for (const auto& [app, value] : p->items()) {
                if (value.is_boolean())
                    s.languageMemory.perApp.emplace_back(app, value.get<bool>());
            }
        }
    }
    if (const auto p = j.find("privacy"); p != j.end() && p->is_object()) {
        get(*p, "excludedApps", s.privacy.excludedApps);
        get(*p, "suggestionsDisabledApps", s.privacy.suggestionsDisabledApps);
    }
    if (const auto h = j.find("hotkeys"); h != j.end() && h->is_object()) {
        for (const auto a : model::kAllHotkeyActions) {
            const auto it = h->find(model::hotkeyActionName(a));
            if (it == h->end() || !it->is_string()) continue; // absent: keep the default
            const std::string chord = it->get<std::string>();
            if (chord.empty()) {
                s.hotkeys[a] = {}; // deliberately unassigned
            } else if (const auto parsed = model::parseHotkey(chord)) {
                s.hotkeys[a] = *parsed;
            } // unparsable: keep the default
        }
        s.hotkeys.resolveDuplicates();
    }
    return s;
}

lk::expected<Settings> JsonSettingsStore::load() const {
    std::ifstream in(std::filesystem::path(path_), std::ios::binary);
    if (!in) return Settings{}; // first run
    std::stringstream buf;
    buf << in.rdbuf();
    return parse(buf.str());
}

lk::expected<void> JsonSettingsStore::save(const Settings& settings) const {
    const std::filesystem::path target(path_);
    const std::filesystem::path temp = target.string() + ".tmp";
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return lk::unexpected(Error::make(Error::Code::Io, "cannot write " + temp.string()));
        }
        out << serialize(settings);
        if (!out) {
            return lk::unexpected(Error::make(Error::Code::Io, "write failed " + temp.string()));
        }
    }
    // The file is also the user's editing surface: an editor (or a backup tool) can hold
    // it open for a moment exactly when we save, and the rename then fails. Losing a
    // setting because of a 20 ms overlap is not acceptable, so retry briefly.
    for (int attempt = 0; attempt < kSaveRenameAttempts; ++attempt) {
        ec.clear();
        std::filesystem::rename(temp, target, ec);
        if (!ec) return {};
        std::this_thread::sleep_for(std::chrono::milliseconds(kSaveRenameRetryMs));
    }
    return lk::unexpected(Error::make(Error::Code::Io, "rename failed: " + ec.message()));
}

} // namespace lankey::core::storage
