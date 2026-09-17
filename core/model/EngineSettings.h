#pragma once

namespace lankey::core::model {

enum class InputMethod {
    Telex,
    Vni,
    SimpleTelex,
};

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

    friend constexpr bool operator==(const EngineSettings&, const EngineSettings&) = default;
};

} // namespace lankey::core::model
