#include "Clipboard.hpp"

#include <SDL3/SDL.h>

namespace editor {

std::optional<std::string> readClipboardText() {
  if (!SDL_HasClipboardText()) return std::nullopt;
  char* text = SDL_GetClipboardText();
  if (text == nullptr) return std::nullopt;
  std::string result(text);
  SDL_free(text);
  return result;
}

}  // namespace editor
