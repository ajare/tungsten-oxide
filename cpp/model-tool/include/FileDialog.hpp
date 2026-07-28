// FileDialog.hpp — native Open/Save dialogs, backing model-tool's Import/Save menu items. Copied
// (and re-namespaced) from cpp/editor/include/FileDialog.hpp verbatim: a self-contained wrapper
// around the modern COM IFileOpenDialog/IFileSaveDialog (Vista+), with no dependency on anything
// editor-specific. Windows/MSVC-only, matching the rest of cpp/model-tool.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace modeltool {

// One filter entry, e.g. {L"OBJ (*.obj)", L"*.obj"}.
struct FileDialogFilter {
  std::wstring name;
  std::wstring pattern;
};

struct FileDialogResult {
  bool ok{false};
  std::filesystem::path path;
};

// `defaultFileName` seeds the Save dialog's filename field (ignored by Open); `defaultExtension`
// is appended by the shell when the user doesn't type one (no leading dot, e.g. L"mppmodel").
FileDialogResult showOpenFileDialog(const std::wstring& title, const std::vector<FileDialogFilter>& filters);
FileDialogResult showSaveFileDialog(const std::wstring& title, const std::vector<FileDialogFilter>& filters,
                                     const std::wstring& defaultFileName, const std::wstring& defaultExtension);

// UTF-8 <-> native-wide conversions for the Win32 text boundary (dialog default filenames, status
// text built from a returned path, stb_image). std::wstring(narrow.begin(), narrow.end()) widens
// BYTES, not code points, and std::filesystem::path::string() narrows through the system ANSI
// codepage -- both silently mangle non-ASCII text. `pathToUtf8` exists because a
// std::filesystem::path already holds native (wide, on Windows) text, so converting it via this
// path avoids that ACP round trip entirely rather than doing wide->ACP->UTF-8.
std::wstring utf8ToWide(const std::string& utf8);
std::string wideToUtf8(const std::wstring& wide);
std::string pathToUtf8(const std::filesystem::path& path);

}  // namespace modeltool
