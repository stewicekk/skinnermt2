// Win32 common file dialogs.
#include "m2rig/file_dialog.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <commdlg.h>
#include <vector>

namespace m2rig {

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n > 0 ? n : 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

std::string narrow(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n > 0 ? n : 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, out.data(), n, nullptr, nullptr);
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

std::wstring toFilter(const std::string& filter) {
    // "A (*.a)|*.a|B (*.*)|*.*" -> L"A (*.a)\0*.a\0B (*.*)\0*.*\0\0"
    std::wstring out;
    std::string cur;
    bool isPattern = false;
    auto flush = [&] {
        out += widen(cur);
        out.push_back(L'\0');
        cur.clear();
    };
    for (char c : filter) {
        if (c == '|') {
            flush();
            isPattern = !isPattern;
        } else {
            cur.push_back(c);
        }
    }
    flush();
    out.push_back(L'\0');
    (void)isPattern;
    return out;
}

}  // namespace

DialogResult openFileDialog(void* parentHwnd, const std::string& title, const std::string& filter,
                            const std::string& defaultExt) {
    std::vector<wchar_t> file(MAX_PATH * 4, L'\0');
    const std::wstring wFilter = toFilter(filter);
    const std::wstring wTitle = widen(title);
    const std::wstring wExt = widen(defaultExt);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(parentHwnd);
    ofn.lpstrFile = file.data();
    ofn.nMaxFile = static_cast<DWORD>(file.size());
    ofn.lpstrFilter = wFilter.c_str();
    ofn.lpstrTitle = wTitle.c_str();
    ofn.lpstrDefExt = wExt.empty() ? nullptr : wExt.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return {};
    return {true, narrow(file.data())};
}

DialogResult saveFileDialog(void* parentHwnd, const std::string& title, const std::string& filter,
                            const std::string& defaultExt, const std::string& defaultName) {
    std::vector<wchar_t> file(MAX_PATH * 4, L'\0');
    const std::wstring wInit = widen(defaultName);
    if (!wInit.empty())
        wcsncpy_s(file.data(), file.size(), wInit.c_str(), _TRUNCATE);
    const std::wstring wFilter = toFilter(filter);
    const std::wstring wTitle = widen(title);
    const std::wstring wExt = widen(defaultExt);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(parentHwnd);
    ofn.lpstrFile = file.data();
    ofn.nMaxFile = static_cast<DWORD>(file.size());
    ofn.lpstrFilter = wFilter.c_str();
    ofn.lpstrTitle = wTitle.c_str();
    ofn.lpstrDefExt = wExt.empty() ? nullptr : wExt.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return {};
    return {true, narrow(file.data())};
}

}  // namespace m2rig
