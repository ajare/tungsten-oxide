// MaterialCatalog.hpp -- reads editor authoring metadata from PbrMaterialBinding resources in
// src/tungsten-monoxide/resources/Resources.yaml. Bindings that represent path-material choices
// carry editorTrackMaterial, editorMaterialKey, and editorTexture properties. The catalog exposes
// those choices to the editor and eager-loads their preview textures through TextureCache.
//
// This is a standalone, editor-owned YAML reader -- it does
// NOT use willpower.application::ResourceManager, which requires a full mpp::RenderSystem. The
// editor-facing material name remains what TrackDefinition stores (for example
// "Tracks/AsphaltTrack"), while materialQualifiedName is the stable key baked into the exported
// .mppmodel and used as the generated Track resource's DependentResource id.
//
// Structural problems (the file cannot be read, contains no usable editor material choices, or
// omits a fixed PBR binding needed by generated rail/mesh/shell/zone/trigger geometry) throw and
// are treated as fatal startup errors. An individual choice with incomplete metadata or an
// unreadable preview texture is skipped with a warning.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "TextureCache.hpp"

namespace editor {

struct MaterialEntry {
  std::string namesp;
  std::string name;
  std::string qualifiedName;              // editor path-material name, qualified with namesp
  std::string materialQualifiedName;      // stable PBR material key baked into exported meshes
  std::vector<std::string> texturePaths;  // resolved paths, already loaded into TextureCache
};

class MaterialCatalog {
public:
  static MaterialCatalog load(const std::filesystem::path& resourcesYamlPath, TextureCache& textureCache);

  const std::vector<MaterialEntry>& materials() const { return entries_; }

private:
  std::vector<MaterialEntry> entries_;
};

}  // namespace editor
