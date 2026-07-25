// Clipboard.hpp — reads CF_UNICODETEXT off the Windows clipboard (EDITOR_NATIVE_FILE_IO_PLAN.md
// M9), the native analogue of navigator.clipboard.readText() used by js/editor.js's paste-mesh
// button. Windows/MSVC-only, matching the rest of cpp/editor.
#pragma once

#include <optional>
#include <string>

namespace editor {

// Returns the clipboard's text as UTF-8, or nullopt if the clipboard couldn't be opened or holds
// no text.
std::optional<std::string> readClipboardText();

}  // namespace editor
