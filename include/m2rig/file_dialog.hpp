#pragma once
// Native Win32 file dialogs (no extra dependencies). Implemented in
// src/app/file_dialog.cpp, compiled into the GUI target only.
#include <string>

namespace m2rig {

struct DialogResult {
    bool confirmed = false;  // false == user cancelled (not an error)
    std::string path;
};

// Parent may be null. Filter format: "SMD files (*.smd)|*.smd|All files (*.*)|*.*".
DialogResult openFileDialog(void* parentHwnd, const std::string& title, const std::string& filter,
                            const std::string& defaultExt = {});
DialogResult saveFileDialog(void* parentHwnd, const std::string& title, const std::string& filter,
                            const std::string& defaultExt = {},
                            const std::string& defaultName = {});
DialogResult selectDirectoryDialog(void* parentHwnd, const std::string& title,
                                   const std::string& defaultPath = {});

}  // namespace m2rig
