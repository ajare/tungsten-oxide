#include "TextureLoad.hpp"

#include <cstring>

#include <GL/glew.h>

// This is the one translation unit that owns stb_image's implementation (mirrors
// cpp/editor/src/TextureCache.cpp's own comment on the same convention).
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO_WARNING
// Without this, stbi_load opens files via ANSI fopen, so a UTF-8 path outside the system codepage
// silently fails to open -- model-tool's paths (from FileDialog's pathToUtf8, and AssImpImport's
// resolved texture paths) are UTF-8 throughout. STBI_WINDOWS_UTF8 routes stb_image's fopen calls
// through _wfopen instead (same fix cpp/editor/src/TextureCache.cpp already applies).
#define STBI_WINDOWS_UTF8
#include "stb/stb_image.h"

namespace modeltool {

mpp::TextureData loadImage(std::string const& filename) {
  int width = 0, height = 0, channels = 0;
  unsigned char* pixels = stbi_load(filename.c_str(), &width, &height, &channels, 4);
  if (pixels == nullptr) {
    // 1x1 opaque white, not a null/empty buffer -- a bad texture reference degrades to a blank
    // material rather than crashing whatever uploads this TextureData.
    auto* fallback = new std::uint8_t[4]{255, 255, 255, 255};
    return mpp::TextureData(fallback, 1, 1, 32, GL_RGBA, GL_UNSIGNED_BYTE);
  }

  const std::size_t byteCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
  auto* copy = new std::uint8_t[byteCount];
  std::memcpy(copy, pixels, byteCount);
  stbi_image_free(pixels);

  return mpp::TextureData(copy, static_cast<std::size_t>(width), static_cast<std::size_t>(height), 32, GL_RGBA, GL_UNSIGNED_BYTE);
}

}  // namespace modeltool
