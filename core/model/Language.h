#pragma once

#include <cstdint>

namespace lankey::core::model {

// Which column of the user's VI-EN-JA glossary a piece of text belongs to. Lives in the
// model rather than in core/convert so a Suggestion can name a language without the model
// depending on the glossary.
enum class Language : std::uint8_t { Vietnamese, English, Japanese };

} // namespace lankey::core::model
