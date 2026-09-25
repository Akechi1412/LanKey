#pragma once

#include <string>
#include <string_view>

namespace lankey::core::text {

// Japanese 全角/半角 conversion: ASCII U+0021..U+007E <-> U+FF01..U+FF5E, space <-> U+3000,
// half-width katakana U+FF61..U+FF9F <-> full-width katakana (voiced marks combined on the
// way to full width, split on the way back). Everything else passes through unchanged.
std::u32string toFullWidth(std::u32string_view s);
std::u32string toHalfWidth(std::u32string_view s);

// Converts the whole string to whichever class is in the minority, so pressing the hotkey
// twice restores the text. A string with nothing convertible is returned as is.
std::u32string toggleWidth(std::u32string_view s);

} // namespace lankey::core::text
