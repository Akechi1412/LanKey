#include "platform/win32/DpapiProtector.h"

#include "platform/win32/Win32.h"

// clang-format off
#include <wincrypt.h> // needs windows.h (via Win32.h) first
#include <dpapi.h>
// clang-format on

namespace lankey::platform::win32 {

using core::model::Error;

namespace {

// Extra entropy mixed into the key: another program running as the same user would
// also have to know this string to open the image. Not a secret (open source), but it
// keeps generic "decrypt every DPAPI blob in %APPDATA%" tooling from working unmodified.
constexpr char kEntropy[] = "LanKey.lexicon.v1";

DATA_BLOB blobOf(std::span<const std::uint8_t> bytes) noexcept {
    DATA_BLOB b;
    b.pbData = const_cast<BYTE*>(bytes.data());
    b.cbData = static_cast<DWORD>(bytes.size());
    return b;
}

DATA_BLOB entropyBlob() noexcept {
    DATA_BLOB b;
    b.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy));
    b.cbData = static_cast<DWORD>(sizeof(kEntropy) - 1);
    return b;
}

// Copies DPAPI output into a vector, wipes and frees the DPAPI buffer.
std::vector<std::uint8_t> takeAndFree(DATA_BLOB& out) {
    std::vector<std::uint8_t> bytes(out.pbData, out.pbData + out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return bytes;
}

} // namespace

lk::expected<std::vector<std::uint8_t>>
DpapiProtector::protect(std::span<const std::uint8_t> plain) {
    DATA_BLOB in = blobOf(plain);
    DATA_BLOB entropy = entropyBlob();
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"LanKey lexicon", &entropy, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return lk::unexpected(
            Error::make(Error::Code::Io, "DPAPI protect: " + lastErrorMessage(GetLastError())));
    }
    return takeAndFree(out);
}

lk::expected<std::vector<std::uint8_t>>
DpapiProtector::unprotect(std::span<const std::uint8_t> sealed) {
    DATA_BLOB in = blobOf(sealed);
    DATA_BLOB entropy = entropyBlob();
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                            &out)) {
        return lk::unexpected(
            Error::make(Error::Code::Io, "DPAPI unprotect: " + lastErrorMessage(GetLastError())));
    }
    return takeAndFree(out);
}

} // namespace lankey::platform::win32
