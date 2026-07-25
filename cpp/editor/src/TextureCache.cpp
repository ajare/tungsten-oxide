#include "TextureCache.hpp"

#include <GL/gl3w.h>

// This is the one translation unit that owns stb_image's implementation.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO_WARNING
// Without this, stbi_load/stbi_info open files via ANSI fopen, so a `char const*` path outside the
// system codepage silently fails to open. TextureAsset::path and the paths this module receives
// are UTF-8 throughout (see FileDialog.hpp's pathToUtf8) -- STBI_WINDOWS_UTF8 makes stb_image
// interpret its `char const*` filename arguments as UTF-8 and route through _wfopen internally
// (EDITOR_PARITY_FIXES.md finding 7).
#define STBI_WINDOWS_UTF8
#include "stb/stb_image.h"

#include "FileDialog.hpp"

namespace editor {

std::string textureNameFromPath(const std::string& path) {
  std::string clean = path;
  const auto queryPos = clean.find_first_of("?#");
  if (queryPos != std::string::npos) clean = clean.substr(0, queryPos);
  const auto lastSep = clean.find_last_of("/\\");
  const std::string name = lastSep == std::string::npos ? clean : clean.substr(lastSep + 1);
  return name.empty() ? "texture" : name;
}

bool readImageSize(const std::filesystem::path& file, int& outWidth, int& outHeight) {
  int comp = 0;
  return stbi_info(pathToUtf8(file).c_str(), &outWidth, &outHeight, &comp) != 0;
}

std::filesystem::path findAssetsDir() {
  std::filesystem::path dir = std::filesystem::current_path();
  for (int depth = 0; depth < 8; ++depth) {
    std::error_code ec;
    if (std::filesystem::exists(dir / "assets" / "track" / "manifest.json", ec)) return dir / "assets";
    if (!dir.has_parent_path() || dir.parent_path() == dir) break;
    dir = dir.parent_path();
  }
  return {};
}

TextureCache::~TextureCache() { clear(); }

void TextureCache::clear() {
  for (auto& [path, tex] : cache_) {
    if (tex.glId != 0) {
      const GLuint id = tex.glId;
      glDeleteTextures(1, &id);
    }
  }
  cache_.clear();
}

const LoadedTexture& TextureCache::get(const std::string& path) {
  const auto existing = cache_.find(path);
  if (existing != cache_.end()) return existing->second;

  LoadedTexture tex;
  int width = 0, height = 0, comp = 0;
  unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &comp, 4);
  if (pixels != nullptr) {
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(pixels);
    tex.glId = id;
    tex.width = width;
    tex.height = height;
  }
  return cache_.emplace(path, tex).first->second;
}

}  // namespace editor
