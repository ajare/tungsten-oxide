// FileDialog.hpp — native Open/Save dialogs backing the editor's Save/Export/Import buttons. Wraps
// the modern COM IFileOpenDialog/IFileSaveDialog (Vista+), picked over the legacy
// GetOpenFileNameW/GetSaveFileNameW pair for nicer dialog chrome and Explorer integration.
// Windows/MSVC-only, matching the rest of cpp/editor.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace editor {

// One filter entry, e.g. {L"Track JSON (*.json)", L"*.json"}.
struct FileDialogFilter {
  std::wstring name;
  std::wstring pattern;
};

struct FileDialogResult {
  bool ok{false};
  std::filesystem::path path;
};

// `defaultFileName` seeds the Save dialog's filename field (ignored by Open); `defaultExtension`
// is appended by the shell when the user doesn't type one (no leading dot, e.g. L"json").
FileDialogResult showOpenFileDialog(const std::wstring& title, const std::vector<FileDialogFilter>& filters);
FileDialogResult showSaveFileDialog(const std::wstring& title, const std::vector<FileDialogFilter>& filters,
                                    const std::wstring& defaultFileName, const std::wstring& defaultExtension,
                                    bool confirmOverwrite = true);

// UTF-8 <-> native-wide conversions for the Win32 text boundary (dialog default filenames, status
// text built from a returned path, stb_image). track::Track/editor records hold UTF-8 in memory
// throughout (TrackDefinition::name, TextureAsset::path, ...); convert only at this boundary.
// std::wstring(narrow.begin(), narrow.end()) widens BYTES, not code points, and
// std::filesystem::path::string() narrows through the system ANSI codepage -- both silently mangle
// non-ASCII text. `pathToUtf8` exists because a
// std::filesystem::path already holds native (wide, on Windows) text, so converting it via this
// path avoids that ACP round trip entirely rather than doing wide->ACP->UTF-8.
std::wstring utf8ToWide(const std::string& utf8);
std::string wideToUtf8(const std::wstring& wide);
std::string pathToUtf8(const std::filesystem::path& path);

}  // namespace editor
