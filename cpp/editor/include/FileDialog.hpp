// FileDialog.hpp — native Open/Save dialogs (EDITOR_NATIVE_FILE_IO_PLAN.md M8), backing the
// editor's Save/Export/Import buttons. editor.html leans on browser primitives (<input type=file>,
// Blob/URL.createObjectURL) that have no native equivalent; this wraps the modern COM
// IFileOpenDialog/IFileSaveDialog (Vista+) picked over the legacy GetOpenFileNameW/
// GetSaveFileNameW pair for nicer dialog chrome and Explorer integration (see the plan's "Open
// decisions" section). Windows/MSVC-only, matching the rest of cpp/editor.
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
                                     const std::wstring& defaultFileName, const std::wstring& defaultExtension);

}  // namespace editor
