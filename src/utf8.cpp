// UTF-8 <-> wchar_t conversion. Contract in m2rig/utf8.hpp.
// Windows path keeps the exact semantics of the dialog helpers that
// shipped since Wave 2 (CP_UTF8 both directions); the portable fallback
// is a small explicit codec with U+FFFD replacement for invalid input.
#include "m2rig/utf8.hpp"

#include <cstddef>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace m2rig {

#ifdef _WIN32

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n > 0 ? n : 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

std::string wideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n > 0 ? n : 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, out.data(), n, nullptr, nullptr);
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

#else

namespace {

constexpr char32_t kReplacement = 0xFFFDu;

void pushWide(std::wstring& out, char32_t cp) {
    if (sizeof(wchar_t) == 2 && cp > 0xFFFFu) {
        const char32_t v = cp - 0x10000u;
        out.push_back(static_cast<wchar_t>(0xD800u + (v >> 10)));
        out.push_back(static_cast<wchar_t>(0xDC00u + (v & 0x3FFu)));
    } else {
        out.push_back(static_cast<wchar_t>(cp));
    }
}

void emitUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80u) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800u) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp < 0x10000u) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

}  // namespace

std::wstring utf8ToWide(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    // Minimum code point value per sequence length (rejects overlongs).
    static constexpr char32_t kMinForLen[5] = {0u, 0u, 0x80u, 0x800u, 0x10000u};
    std::size_t i = 0;
    const std::size_t n = s.size();
    while (i < n) {
        const char32_t c = static_cast<unsigned char>(s[i]);
        char32_t cp = kReplacement;
        std::size_t len = 0;
        if (c < 0x80u) {
            cp = c;
            len = 1;
        } else if ((c >> 5) == 0x6u) {
            cp = c & 0x1Fu;
            len = 2;
        } else if ((c >> 4) == 0xEu) {
            cp = c & 0x0Fu;
            len = 3;
        } else if ((c >> 3) == 0x1Eu) {
            cp = c & 0x07u;
            len = 4;
        }
        if (len == 0 || i + len > n) {
            pushWide(out, kReplacement);  // lead byte without followers, or truncation
            ++i;
            continue;
        }
        bool ok = true;
        for (std::size_t k = 1; k < len; ++k) {
            const char32_t cc = static_cast<unsigned char>(s[i + k]);
            if ((cc >> 6) != 0x2u) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if (!ok) {
            pushWide(out, kReplacement);
            ++i;
            continue;
        }
        if (cp < kMinForLen[len] || cp > 0x10FFFFu ||
            (cp >= 0xD800u && cp <= 0xDFFFu)) {
            pushWide(out, kReplacement);  // overlong, out of range, or surrogate
            i += len;
            continue;
        }
        pushWide(out, cp);
        i += len;
    }
    return out;
}

std::string wideToUtf8(const std::wstring& s) {
    std::string out;
    out.reserve(s.size() * 2u);
    for (std::size_t i = 0; i < s.size(); ++i) {
        char32_t cp = static_cast<char32_t>(s[i]);
        if (sizeof(wchar_t) == 2) {
            if (cp >= 0xD800u && cp <= 0xDBFFu && i + 1 < s.size()) {
                const char32_t lo = static_cast<char32_t>(s[i + 1]);
                if (lo >= 0xDC00u && lo <= 0xDFFFu) {
                    cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                    ++i;
                } else {
                    cp = kReplacement;  // lone high surrogate
                }
            } else if (cp >= 0xD800u && cp <= 0xDFFFu) {
                cp = kReplacement;  // lone surrogate
            }
        } else if (s[i] < 0) {
            cp = kReplacement;  // cannot occur in well-formed UTF-32 input
        }
        emitUtf8(out, cp);
    }
    return out;
}

#endif

}  // namespace m2rig
