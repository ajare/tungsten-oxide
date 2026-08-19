// TextureCache.hpp — lazy-loaded OpenGL textures for texture-asset thumbnails/tile pickers
// (EDITOR_CPP_PORT_PLAN.md M7b), backed by stb_image (vendored single-header decoder, see
// include/stb/stb_image.h) since the editor process has no other image codec.
//
// Loads PNG bytes straight off disk with stbi_load and uploads one GL_TEXTURE_2D per unique path,
// cached for the process lifetime -- textures are display-only decoration here (the tile grid
// picker), never touched by physics/baking, so re-decoding per frame would be pure waste.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

namespace editor {

struct LoadedTexture {
  std::uint32_t glId{0};
  int width{0}, height{0};
  bool ok() const { return glId != 0; }
};

// Extracts a display name from a path: strips any ?query/#fragment, then takes the last path
// segment (either slash style).
std::string textureNameFromPath(const std::string& path);

// Reads just the pixel dimensions of an image file without decoding it, used when registering a
// new texture asset. Returns false if the file can't be read/decoded.
bool readImageSize(const std::filesystem::path& file, int& outWidth, int& outHeight);

// Locates the repo's checked-in assets/ directory by walking up from the current working
// directory looking for assets/track/manifest.json -- the editor's build output directory is
// nested several levels under the repo root (build/editor/Release/...). Returns an empty path
// if not found within a bounded number of parent hops.
std::filesystem::path findAssetsDir();

class TextureCache {
 public:
  TextureCache() = default;
  ~TextureCache();
  TextureCache(const TextureCache&) = delete;
  TextureCache& operator=(const TextureCache&) = delete;

  // Loads (or returns the cached) GL texture for a file path. A failed load is cached too (as a
  // zero-valued LoadedTexture) so a missing/corrupt file isn't retried every frame.
  const LoadedTexture& get(const std::string& path);

  void clear();

 private:
  std::map<std::string, LoadedTexture> cache_;
};

}  // namespace editor
