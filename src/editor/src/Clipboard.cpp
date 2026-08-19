#if defined(_WIN32)

#include "Clipboard.hpp"

#include <windows.h>

namespace editor {

std::optional<std::string> readClipboardText() {
  if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return std::nullopt;
  if (!OpenClipboard(nullptr)) return std::nullopt;

  std::optional<std::string> result;
  HANDLE data = GetClipboardData(CF_UNICODETEXT);
  if (data != nullptr) {
    const wchar_t* wide = static_cast<const wchar_t*>(GlobalLock(data));
    if (wide != nullptr) {
      const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
      if (utf8Length > 0) {
        std::string utf8(static_cast<std::size_t>(utf8Length) - 1, '\0');  // drop the trailing NUL WideCharToMultiByte counts
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8.data(), utf8Length, nullptr, nullptr);
        result = std::move(utf8);
      }
      GlobalUnlock(data);
    }
  }
  CloseClipboard();
  return result;
}

}  // namespace editor

#else
#error "The track editor clipboard integration is supported only on Windows."
#endif
