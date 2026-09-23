#pragma once
// UTF-8 <-> wchar_t conversion shared by the Win32 dialog layer and any
// future bridge/process code that crosses the Unicode boundary.
// Public contract (platform-independent):
//  - Valid UTF-8 always round-trips exactly (utf8 -> wide -> utf8 identity).
//  - Empty maps to empty in both directions.
//  - Invalid-input mapping is PLATFORM-DEFINED, never depend on it:
//    Windows converts via CP_UTF8 without MB_ERR_INVALID_CHARS (system
//    default-char substitution); the portable fallback substitutes U+FFFD.
//    Validate first when invalid input must be rejected.
// Windows note: wchar_t is UTF-16 (surrogate pairs for astral planes).
// Portable note: wchar_t width follows the platform (UTF-32 where 4 B).
#include <string>

namespace m2rig {

std::wstring utf8ToWide(const std::string& s);
std::string wideToUtf8(const std::wstring& s);

}  // namespace m2rig
