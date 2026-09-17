#include "core/text/Utf.h"

namespace lankey::core::text {

std::string toUtf8(std::u32string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char32_t cp : s) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

std::u32string fromUtf8(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const auto b0 = static_cast<unsigned char>(s[i]);
        char32_t cp = 0;
        std::size_t len = 1;
        if (b0 < 0x80) {
            cp = b0;
        } else if ((b0 & 0xE0) == 0xC0) {
            cp = b0 & 0x1F;
            len = 2;
        } else if ((b0 & 0xF0) == 0xE0) {
            cp = b0 & 0x0F;
            len = 3;
        } else {
            cp = b0 & 0x07;
            len = 4;
        }
        for (std::size_t k = 1; k < len && i + k < s.size(); ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        }
        out.push_back(cp);
        i += len;
    }
    return out;
}

std::u16string toUtf16(std::u32string_view s) {
    std::u16string out;
    out.reserve(s.size());
    for (const char32_t cp : s) {
        if (cp < 0x10000) {
            out.push_back(static_cast<char16_t>(cp));
        } else {
            const char32_t v = cp - 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (v >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (v & 0x3FF)));
        }
    }
    return out;
}

std::u32string fromUtf16(std::u16string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto w = static_cast<char32_t>(s[i]);
        if (w >= 0xD800 && w <= 0xDBFF && i + 1 < s.size()) {
            const auto lo = static_cast<char32_t>(s[i + 1]);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                out.push_back(0x10000 + ((w - 0xD800) << 10) + (lo - 0xDC00));
                ++i;
                continue;
            }
        }
        out.push_back(w);
    }
    return out;
}

} // namespace lankey::core::text
