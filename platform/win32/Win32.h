#pragma once

// The one place that includes <windows.h>. Everything under platform/win32 and ui/ goes
// through here so the lean-and-mean / NOMINMAX settings are consistent.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <cstdint>
#include <string>
#include <string_view>
#include <windows.h>

namespace lankey::platform::win32 {

// Stamped into dwExtraInfo of every INPUT we send. The keyboard hook sees it come back and
// marks the event injectedBySelf, which is what stops the hook from feeding our own output
// back into the engine (an infinite loop otherwise). Spelled "LKEY".
inline constexpr ULONG_PTR kLanKeyMagic = 0x4C4B4559;

// UTF-16 <-> UTF-32 for the platform boundary (core only speaks u32string).
[[nodiscard]] std::wstring toUtf16(std::u32string_view s);
[[nodiscard]] std::u32string fromUtf16(std::wstring_view s);
[[nodiscard]] std::string toUtf8(std::wstring_view s);
[[nodiscard]] std::wstring fromUtf8(std::string_view s);

// Last-error text for logs ("(0x00000005) Access is denied.").
[[nodiscard]] std::string lastErrorMessage(DWORD error);

} // namespace lankey::platform::win32
