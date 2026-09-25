#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lankey::core::model {

enum class InputMethod {
    Telex,
    Vni,
    SimpleTelex,
    Custom, // the 11 keys in EngineSettings::customKeys
};

// Output encoding. Anything but Unicode targets legacy fonts: the engine then emits font
// code points, not Vietnamese letters, so learning, suggestions and AutoCorrect are off.
enum class CodeTable {
    Unicode,
    UnicodeCompound, // base letter + combining mark (U+0300...)
    Tcvn3,           // ABC: one byte per letter, glyphs at ANSI positions
    VniWindows,      // base letter + VNI-font mark character
};

// User-defined input method: thirteen functions, in this order:
//   sắc huyền hỏi ngã nặng â ô ê ư/ơ/ă đ xoá-dấu chữ-ơ chữ-ư
// each with up to kCustomKeysPerFunction keys; the first eleven need at least one, the two
// standalone letters (Telex [ ], Shift for Ơ Ư) may have none. Serialised as comma-separated
// groups of lowercase key characters, e.g. "s,f,r,x,j,a,o,e,w,d,z,[,]" (Telex); eleven
// groups (or the old eleven-character string) are accepted with the letters left empty.
// Keys are the unshifted characters the engine can see: a-z, 0-9 and [];'./\-=`.
inline constexpr int kCustomKeyCount = 13;
inline constexpr int kCustomRequiredCount = 11;
inline constexpr int kCustomKeysPerFunction = 4;
inline constexpr const char* kDefaultCustomKeys = "s,f,r,x,j,a,o,e,w,d,z,[,]";
inline constexpr std::string_view kCustomKeyChars =
    "abcdefghijklmnopqrstuvwxyz0123456789[];'./\\-=`";

using CustomKeyTable = std::array<std::string, kCustomKeyCount>;

// Parses the serialised form (an old eleven-character string counts as one key per
// function). nullopt when malformed: wrong group count, empty or over-long group, a
// character the engine cannot map, or one key in two functions other than â/ô/ê (which
// may share a key, VNI-style).
inline std::optional<CustomKeyTable> parseCustomKeys(std::string_view text) {
    CustomKeyTable table;
    std::vector<std::string> groups;
    if (text.find(',') == std::string_view::npos) {
        if (text.size() != static_cast<std::size_t>(kCustomRequiredCount)) return std::nullopt;
        for (const char c : text)
            groups.emplace_back(1, c);
    } else {
        std::size_t start = 0;
        while (true) {
            const std::size_t comma = text.find(',', start);
            groups.emplace_back(text.substr(start, comma - start));
            if (comma == std::string_view::npos) break;
            start = comma + 1;
        }
    }
    while (groups.size() == static_cast<std::size_t>(kCustomRequiredCount) ||
           (groups.size() > static_cast<std::size_t>(kCustomRequiredCount) &&
            groups.size() < static_cast<std::size_t>(kCustomKeyCount))) {
        groups.emplace_back(); // standalone letters unset
    }
    if (groups.size() != static_cast<std::size_t>(kCustomKeyCount)) return std::nullopt;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        std::string& g = groups[i];
        const bool required = i < static_cast<std::size_t>(kCustomRequiredCount);
        if ((required && g.empty()) ||
            g.size() > static_cast<std::size_t>(kCustomKeysPerFunction)) {
            return std::nullopt;
        }
        for (auto& c : g) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (kCustomKeyChars.find(c) == std::string_view::npos) return std::nullopt;
        }
        table[i] = g;
    }
    for (int i = 0; i < kCustomKeyCount; ++i) {
        for (int j = i + 1; j < kCustomKeyCount; ++j) {
            const bool circumflex = i >= 5 && i <= 7 && j >= 5 && j <= 7;
            if (circumflex) continue;
            for (const char c : table[static_cast<std::size_t>(i)]) {
                if (table[static_cast<std::size_t>(j)].find(c) != std::string::npos) {
                    return std::nullopt;
                }
            }
        }
    }
    return table;
}

inline std::string joinCustomKeys(const CustomKeyTable& table) {
    std::string out;
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (i != 0) out += ',';
        out += table[i];
    }
    return out;
}

// Configuration for the Telex/VNI engine. Contains only what affects diacritic placement;
// Smart Layer configuration lives in a separate struct.
struct EngineSettings {
    InputMethod inputMethod = InputMethod::Telex;
    // true: "hoà", "thuý" (modern style). false: "hòa", "thúy" (classic style).
    bool modernToneMark = true;
    // Reject combinations that are not valid Vietnamese syllables (e.g. do not let
    // "tiếngs" receive a second tone mark).
    bool spellCheck = true;
    // cc -> ch, gg -> gi, kk -> kh, nn -> ng, qq -> qu, pp -> ph, tt -> th, uu -> ươ.
    bool quickTelex = false;
    CodeTable codeTable = CodeTable::Unicode;
    // InputMethod::Custom: see parseCustomKeys() above.
    std::string customKeys = kDefaultCustomKeys;

    friend bool operator==(const EngineSettings&, const EngineSettings&) = default;
};

} // namespace lankey::core::model
