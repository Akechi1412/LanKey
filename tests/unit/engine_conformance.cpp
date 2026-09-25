// Conformance test suite for IVietnameseEngine.
//
// Every adapter (OpenKey today, any future one) runs through this same table. It is the objective
// basis for choosing the engine in Phase 0, and the safety net for future upstream updates.
//
// Adding an edge case = adding one row to kCases. Each case types a key sequence on a fake
// "screen" (tests/support/Typist.h) and compares the final result - independent of each
// engine's own backspace/retype strategy.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "tests/support/DynamicTests.h"
#include "tests/support/Typist.h"
#include "tests/unit/engine_registry.h"

namespace lankey::tests {
namespace {

using core::model::EngineSettings;
using core::model::InputMethod;

struct EngineCase {
    std::string name;
    EngineSettings settings;
    std::string keys;        // see special-character conventions in Typist.h
    std::u32string expected; // screen contents after typing finishes
    // If set: check ComposedText::vietnameseTransformApplied of the last key.
    std::optional<bool> expectTransform;
};

EngineSettings telex() {
    return {};
}
EngineSettings telexClassic() {
    EngineSettings s;
    s.modernToneMark = false;
    return s;
}
EngineSettings vni() {
    EngineSettings s;
    s.inputMethod = InputMethod::Vni;
    return s;
}
// User-defined method: VNI digits for the tones, Telex letters for the modifiers, '9' for đ.
EngineSettings custom() {
    EngineSettings s;
    s.inputMethod = InputMethod::Custom;
    s.customKeys = "1,2,3,4,5,a,o,e,w,9,z,,";
    return s;
}
EngineSettings customVniLike() {
    EngineSettings s;
    s.inputMethod = InputMethod::Custom;
    s.customKeys = "1,2,3,4,5,6,6,7,8,9,0,,";
    return s;
}
// Several keys per function: Telex plus the VNI digits, '[' for the horn as well.
EngineSettings customMulti() {
    EngineSettings s;
    s.inputMethod = InputMethod::Custom;
    s.customKeys = "s1,f2,r3,x4,j5,a,o,e,w[,d9,z0,,";
    return s;
}
EngineSettings customPunct() {
    EngineSettings s;
    s.inputMethod = InputMethod::Custom;
    s.customKeys = ";,',/,\\,.,a,o,e,],d,-,,";
    return s;
}
EngineSettings customStandalone() {
    EngineSettings s;
    s.inputMethod = InputMethod::Custom;
    s.customKeys = "s,f,r,x,j,a,o,e,w,d,z,[,]";
    return s;
}
EngineSettings customStandaloneQ() { // any key will do, not only the brackets
    EngineSettings s;
    s.inputMethod = InputMethod::Custom;
    s.customKeys = "s,f,r,x,j,a,o,e,w,d,z,q,";
    return s;
}
EngineSettings withTable(core::model::CodeTable table) {
    EngineSettings s;
    s.codeTable = table;
    return s;
}

// clang-format off
const std::vector<EngineCase> kCases = {
    // -- Basic Telex -----------------------------------------------------------
    {"telex_tieng",                telex(), "tieengs",  U"tiếng", true},
    {"telex_viet",                 telex(), "vieejt",   U"việt",  true},
    {"telex_quet",                 telex(), "quets",    U"quét",  true},   // old OpenKey bug
    {"telex_dong",                 telex(), "ddoongf",  U"đồng",  true},
    {"telex_dien",                 telex(), "dieejn",   U"diện",  true},
    {"telex_acute_then_continue",  telex(), "asn",      U"án",    true},   // "as" -> "á", keep typing without breaking

    // -- Modern / classic tone-mark placement ----------------------------------
    {"telex_hoa_modern",           telex(),        "hoaf", U"hoà", true},
    {"telex_hoa_classic",          telexClassic(), "hoaf", U"hòa", true},

    // -- Backspace restores diacritics -----------------------------------------
    {"telex_bs_keeps_tone",        telex(),        "tieengs\b", U"tiến", std::nullopt},
    {"telex_bs_tuy_modern",        telex(),        "tuyfa\b",   U"tuỳ",  std::nullopt},
    {"telex_bs_tuy_classic",       telexClassic(), "tuyfa\b",   U"tùy",  std::nullopt},

    // -- Uppercase -------------------------------------------------------------
    {"telex_capitalized",          telex(), "Vieejt", U"Việt", true},
    {"telex_all_caps",             telex(), "VIEEJT", U"VIỆT", true},

    // -- Interleaved English ---------------------------------------------------
    // An engine cannot know "text" is English while it is being typed: 'x' is a tone
    // key, so the composition legitimately reads "tẽt". Telling English from Vietnamese
    // mid-syllable is the Smart Layer's job (see README). What the engine must do is
    // restore the raw keys at the word break when the syllable is not valid Vietnamese.
    {"telex_english_the",          telex(), "the",    U"the",    false},
    {"telex_x_is_tone_key",        telex(), "text",   U"tẽt",    true},
    {"telex_restore_text",         telex(), "text ",  U"text ",  std::nullopt},
    {"telex_restore_user",         telex(), "user ",  U"user ",  std::nullopt},
    {"telex_restore_hello",        telex(), "hello ", U"hello ", std::nullopt},
    {"telex_valid_syllable_kept",  telex(), "tex ",   U"tẽ ",    std::nullopt},

    // -- Syllable boundary -----------------------------------------------------
    {"telex_two_syllables",        telex(), "chaof banj", U"chào bạn", true},

    // -- Classic tone placement on "uy" / re-placement after Backspace ----------
    {"telex_tuy_classic",          telexClassic(), "tuyf",     U"tùy",  true},
    {"telex_thuy_classic",         telexClassic(), "thuys",    U"thúy", true},
    {"telex_hoan_classic",         telexClassic(), "hoafn",    U"hoàn", true},
    // After Backspace the tone must move back to where the (now shorter) syllable
    // wants it, respecting the classic/modern setting. (VKey 8bb2bd0, evaluated as an
    // alternative engine, re-placed it in modern style regardless of the setting.)
    {"telex_bs_replaces_tone_classic", telexClassic(), "hoafn\b", U"hòa", std::nullopt},
    {"telex_bs_replaces_tone_modern",  telex(),        "hoafn\b", U"hoà", std::nullopt},

    // -- VNI -------------------------------------------------------------------
    {"vni_tieng",                  vni(), "tie6ng1", U"tiếng", true},
    {"vni_viet",                   vni(), "vie6t5",  U"việt",  true},
    {"vni_dieu",                   vni(), "dieu96",  U"điêu",  true},     // upstream issue #72

    // -- User-defined method (engine patch 002) --------------------------------
    {"custom_tieng",               custom(), "tieeng1",  U"tiếng", true},
    {"custom_dong",                custom(), "d9oong2",  U"đồng",  true}, // đ = d + đ-key, as in VNI
    {"custom_hoi",                 custom(), "hoi3",     U"hỏi",   true},
    {"custom_horn",                custom(), "tuwowng4", U"tưỡng", true},
    {"custom_s_is_plain",          custom(), "las ",     U"las ",  std::nullopt}, // 's' no longer a tone key
    {"custom_bs_keeps_tone",       custom(), "tieeng1\b", U"tiến", std::nullopt},
    // Modifier keys that are not the Telex letters: VNI-style, 6 shared by â and ô.
    {"custom_vni_like_tieng",      customVniLike(), "tie7ng1", U"tiếng", true},
    {"custom_vni_like_dong",       customVniLike(), "d9o6ng2", U"đồng",  true},
    {"custom_vni_like_can",        customVniLike(), "ca6n1",   U"cấn",   true},
    {"custom_vni_like_duong",      customVniLike(), "d9uo8ng2", U"đường", true},
    // Several keys for one function (Unikey-style): either spelling works.
    {"custom_multi_telex_keys",    customMulti(), "tieengs",   U"tiếng", true},
    {"custom_multi_vni_keys",      customMulti(), "tieeng1",   U"tiếng", true},
    {"custom_multi_bracket_horn",  customMulti(), "tu[o[ng",   U"tương", true},
    {"custom_multi_mixed",         customMulti(), "d9uwowng2", U"đường", true},
    // Standalone letters: the Telex [ ] keys, Shift for capitals, w alone for ư.
    {"custom_standalone_o",        customStandalone(), "[",    U"ơ",   true},
    {"custom_standalone_u",        customStandalone(), "]",    U"ư",   true},
    {"custom_standalone_cap",      customStandalone(), "{}",   U"ƠƯ",  true},
    {"custom_standalone_word",     customStandalone(), "m[i",  U"mơi", true},
    {"custom_standalone_w_alone",  customStandalone(), "w",    U"ư",   true},
    {"custom_standalone_q_key",    customStandaloneQ(), "q",   U"ơ",   true},
    {"custom_no_standalone_bracket", custom(), "[",  U"[",  std::nullopt}, // not assigned: plain key
    // Punctuation the engine normally treats as a word break can be a function key.
    {"custom_punct_tone",          customPunct(), "tieeng;",   U"tiếng", true},
    {"custom_punct_horn_bs",       customPunct(), "tu]o]ng\b", U"tươn",  std::nullopt},
    {"custom_punct_unassigned_breaks", customPunct(), "tieeng, ", U"tiêng, ", std::nullopt},

    // -- Legacy code tables: font code points on screen, two units for a marked letter --
    {"compound_tieng",             withTable(core::model::CodeTable::UnicodeCompound),
                                   "tieengs",   U"tiếng",  std::nullopt},
    {"compound_bs_removes_both",   withTable(core::model::CodeTable::UnicodeCompound),
                                   "tieengs\b\b\b", U"ti", std::nullopt},
    {"compound_duong",             withTable(core::model::CodeTable::UnicodeCompound),
                                   "dduwowngf", U"đường", std::nullopt},
    {"vni_windows_tieng",          withTable(core::model::CodeTable::VniWindows),
                                   "tieengs",   U"tieáng",  std::nullopt}, // 'e' + VNI ế glyph 0xE1
    {"tcvn3_tieng",                withTable(core::model::CodeTable::Tcvn3),
                                   "tieengs",   U"tiÕng",   std::nullopt}, // ABC: ế at 0xD5
};
// clang-format on

class ConformanceTest : public testing::Test {
public:
    ConformanceTest(EngineCandidate candidate, EngineCase testCase)
        : candidate_(std::move(candidate)), case_(std::move(testCase)) {}

    void TestBody() override {
        auto engine = candidate_.make();
        ASSERT_NE(engine, nullptr);
        engine->configure(case_.settings);

        Typist typist(*engine);
        typist.type(case_.keys);

        EXPECT_EQ(typist.screen(), case_.expected)
            << "keys=\"" << case_.keys << "\" expected=\"" << toUtf8(case_.expected)
            << "\" actual=\"" << toUtf8(typist.screen()) << "\"";

        if (case_.expectTransform.has_value()) {
            EXPECT_EQ(typist.lastComposed().vietnameseTransformApplied, *case_.expectTransform)
                << "wrong vietnameseTransformApplied for keys=\"" << case_.keys << "\"";
        }
    }

private:
    EngineCandidate candidate_;
    EngineCase case_;
};

} // namespace

void registerConformanceTests() {
    const auto engines = registeredEngines();

    if (engines.empty()) {
        testing::RegisterTest(
            "EngineConformance", "NoAdapterRegistered", nullptr, nullptr, __FILE__, __LINE__, [] {
                struct Skip : testing::Test {
                    void TestBody() override {
                        GTEST_SKIP() << "No adapter registered in tests/unit/engine_registry.cpp. "
                                        "Enable LANKEY_ENGINE_OPENKEY to run "
                                     << kCases.size() << " conformance cases.";
                    }
                };
                return new Skip();
            });
        return;
    }

    for (const auto& engine : engines) {
        const std::string suite = "EngineConformance_" + engine.name;
        for (const auto& c : kCases) {
            testing::RegisterTest(suite.c_str(), c.name.c_str(), nullptr, nullptr, __FILE__,
                                  __LINE__, [engine, c] { return new ConformanceTest(engine, c); });
        }
    }
}

} // namespace lankey::tests
