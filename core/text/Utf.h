#pragma once

#include <string>
#include <string_view>

// UTF conversions at the boundaries of core: UTF-8 for storage/logs, UTF-16 for Win32.
// Inside core everything is std::u32string. Invalid input is decoded leniently (no
// exceptions): the hook thread must never throw and a broken byte in a log line is not
// worth failing over.
namespace lankey::core::text {

[[nodiscard]] std::string toUtf8(std::u32string_view s);
[[nodiscard]] std::u32string fromUtf8(std::string_view s);

[[nodiscard]] std::u16string toUtf16(std::u32string_view s);
[[nodiscard]] std::u32string fromUtf16(std::u16string_view s);

} // namespace lankey::core::text
