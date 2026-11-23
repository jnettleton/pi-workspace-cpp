#pragma once

#include <string>
#include <string_view>

namespace util {

/// Decodes UTF-8 into code points. Each byte of an invalid or truncated
/// sequence becomes U+FFFD, so text always decodes to something drawable.
inline std::u32string decode_utf8(std::string_view text) {
    std::u32string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const auto b0 = static_cast<unsigned char>(text[i]);
        std::size_t len = 0;
        char32_t cp = 0;
        char32_t min = 0;
        if (b0 < 0x80) {
            out.push_back(b0);
            ++i;
            continue;
        } else if ((b0 & 0xE0) == 0xC0) {
            len = 2, cp = b0 & 0x1F, min = 0x80;
        } else if ((b0 & 0xF0) == 0xE0) {
            len = 3, cp = b0 & 0x0F, min = 0x800;
        } else if ((b0 & 0xF8) == 0xF0) {
            len = 4, cp = b0 & 0x07, min = 0x10000;
        }

        bool ok = len != 0 && i + len <= text.size();
        for (std::size_t k = 1; ok && k < len; ++k) {
            const auto b = static_cast<unsigned char>(text[i + k]);
            if ((b & 0xC0) != 0x80) {
                ok = false;
            } else {
                cp = (cp << 6) | (b & 0x3F);
            }
        }
        if (ok && cp >= min && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF)) {
            out.push_back(cp);
            i += len;
        } else {
            out.push_back(U'�');
            ++i;
        }
    }
    return out;
}

}  // namespace util
