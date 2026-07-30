// Clipboard.hpp — reads CF_UNICODETEXT off the Windows clipboard, backing the editor's paste-mesh
// button. Windows/MSVC-only, matching the rest of cpp/editor.
#pragma once

#include <optional>
#include <string>

namespace editor {

// Returns the clipboard's text as UTF-8, or nullopt if the clipboard couldn't be opened or holds
// no text.
std::optional<std::string> readClipboardText();

}  // namespace editor
