#include "core/storage/JsonSettingsStore.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

namespace lankey::core::storage {

using model::AutoCorrectLevel;
using model::Error;
using model::InputMethod;
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
    }
    return "telex";
}

InputMethod inputMethodFrom(const std::string& s, InputMethod fallback) {
    if (s == "telex") return InputMethod::Telex;
    if (s == "vni") return InputMethod::Vni;
    if (s == "simple-telex") return InputMethod::SimpleTelex;
    return fallback;
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
    };
    j["suggestions"] = {
        {"enabled", s.suggestions.enabled},
        {"minPrefixLength", s.suggestions.minPrefixLength},
        {"idleDelayMs", s.suggestions.idleDelayMs},
        {"selectWithDigits", s.suggestions.selectWithDigits},
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
    j["privacy"] = {
        {"excludedApps", s.privacy.excludedApps},
        {"suggestionsDisabledApps", s.privacy.suggestionsDisabledApps},
    };
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
    if (const auto p = j.find("privacy"); p != j.end() && p->is_object()) {
        get(*p, "excludedApps", s.privacy.excludedApps);
        get(*p, "suggestionsDisabledApps", s.privacy.suggestionsDisabledApps);
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
    std::filesystem::rename(temp, target, ec);
    if (ec) {
        return lk::unexpected(Error::make(Error::Code::Io, "rename failed: " + ec.message()));
    }
    return {};
}

} // namespace lankey::core::storage
