#include "platform/win32/Win32.h"

#include "core/text/Utf.h"

namespace lankey::platform::win32 {

std::wstring toUtf16(std::u32string_view s) {
    const std::u16string u = core::text::toUtf16(s);
    return std::wstring(u.begin(), u.end());
}

std::u32string fromUtf16(std::wstring_view s) {
    return core::text::fromUtf16(std::u16string(s.begin(), s.end()));
}

std::string toUtf8(std::wstring_view s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0,
                                      nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr,
                        nullptr);
    return out;
}

std::wstring fromUtf8(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string lastErrorMessage(DWORD error) {
    wchar_t* buffer = nullptr;
    const DWORD n = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::string text = n != 0 ? toUtf8(std::wstring_view(buffer, n)) : "unknown error";
    if (buffer != nullptr) LocalFree(buffer);
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n'))
        text.pop_back();
    char code[16];
    std::snprintf(code, sizeof(code), "(0x%08lX) ", static_cast<unsigned long>(error));
    return code + text;
}

} // namespace lankey::platform::win32
