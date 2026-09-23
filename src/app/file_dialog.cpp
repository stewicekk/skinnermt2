// Win32 common file dialogs (Unicode throughout; public contract stays
// UTF-8 std::string in/out — see m2rig/file_dialog.hpp).
#include "m2rig/file_dialog.hpp"

#include "m2rig/utf8.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <commdlg.h>
#include <vector>
#include <shlobj.h>

namespace m2rig {

namespace {

std::wstring toFilter(const std::string& filter) {
    // "A (*.a)|*.a|B (*.*)|*.*" -> L"A (*.a)\0*.a\0B (*.*)\0*.*\0\0"
    std::wstring out;
    std::string cur;
    bool isPattern = false;
    auto flush = [&] {
        out += utf8ToWide(cur);
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
    const std::wstring wTitle = utf8ToWide(title);
    const std::wstring wExt = utf8ToWide(defaultExt);
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
    return {true, wideToUtf8(file.data())};
}

DialogResult saveFileDialog(void* parentHwnd, const std::string& title, const std::string& filter,
                            const std::string& defaultExt, const std::string& defaultName) {
    std::vector<wchar_t> file(MAX_PATH * 4, L'\0');
    const std::wstring wInit = utf8ToWide(defaultName);
    if (!wInit.empty())
        wcsncpy_s(file.data(), file.size(), wInit.c_str(), _TRUNCATE);
    const std::wstring wFilter = toFilter(filter);
    const std::wstring wTitle = utf8ToWide(title);
    const std::wstring wExt = utf8ToWide(defaultExt);
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
    return {true, wideToUtf8(file.data())};
}

DialogResult selectDirectoryDialog(void* parentHwnd, const std::string& title,
                                   const std::string& defaultPath) {
    DialogResult result;
    // Unicode end to end: the old ANSI version lossy-converted through the
    // system codepage, so directories with diacritics (e.g. D:\modely\brnění)
    // came back mojibake. Title + initial selection + result all travel as
    // wide strings now; the public contract stays UTF-8 std::string.
    const std::wstring wTitle = utf8ToWide(title);
    const std::wstring wDefault = utf8ToWide(defaultPath);
    BROWSEINFOW bi{};
    bi.hwndOwner = static_cast<HWND>(parentHwnd);
    bi.lpszTitle = wTitle.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
    if (!wDefault.empty()) {
        bi.lParam = reinterpret_cast<LPARAM>(wDefault.c_str());
        bi.lpfn = [](HWND hwnd, UINT msg, LPARAM, LPARAM lpData) -> INT {
            if (msg == BFFM_INITIALIZED) {
                SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, lpData);
            }
            return 0;
        };
    }
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        wchar_t wpath[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, wpath)) {
            result.confirmed = true;
            result.path = wideToUtf8(wpath);
        }
        CoTaskMemFree(pidl);
    }
    return result;
}

}  // namespace m2rig
