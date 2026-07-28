// MaterialCatalog.hpp -- reads a Willpower Resources.xml (see
// cpp/tungsten-monoxide/resources/Resources.xml) looking for <Resource type="TrackMaterial">
// entries, resolves each one's dependent Material -> Image chain down to real texture file
// paths, and eager-loads those through the editor's existing TextureCache.
//
// This is a standalone, editor-owned reader built on Willpower::Common's XmlReader -- it does
// NOT use willpower.application::ResourceManager, which requires a full mpp::RenderSystem (a
// separate GL/window-owning render pipeline the editor doesn't run; the editor has its own
// SDL2+gl3w context). This only needs TrackMaterial names, their underlying Material's own
// qualified name, and texture paths for a picker UI, not GPU-loaded materials. The underlying
// Material's qualified name matters at export time too: MppModelExport.cpp writes a mesh's
// material reference (and its Resources.xml <DependentResource>) as the actual Material a
// TrackMaterial points at, not the TrackMaterial's own name -- a TrackMaterial isn't itself a
// renderable resource (see applib::TrackMaterial::getMaterial()), and other consumers of an
// exported .mppmodel shouldn't need TrackMaterial-aware resolution just to render it.
//
// Structural problems (the file can't be found/parsed, or doesn't look like a Resources.xml, or
// there are no usable TrackMaterial entries at all, or the fixed "Tracks/DefaultRailMaterial"/
// "Tracks/DefaultMeshMaterial"/"Tracks/DefaultShellMaterial"/"Tracks/DefaultZoneMaterial"/
// "Tracks/DefaultTriggerMaterial" Materials rails/mesh regions/shells/zones/triggers always
// export with are missing) throw
// (wp::XmlException or std::runtime_error), meant to be treated as a fatal startup error by the
// caller. A single TrackMaterial with a broken dependency chain (missing ref, unreadable texture)
// is instead skipped with a warning on stderr; the rest of the catalog still loads.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "TextureCache.hpp"

namespace editor {

struct MaterialEntry {
  std::string namesp;
  std::string name;
  std::string qualifiedName;         // namesp + "/" + name (or just name if namesp is empty)
  std::string materialQualifiedName;      // this TrackMaterial's own "Material" dependent's qualified name
  std::vector<std::string> texturePaths;  // resolved paths, already loaded into the TextureCache
};

class MaterialCatalog {
 public:
  static MaterialCatalog load(const std::filesystem::path& resourcesXmlPath, TextureCache& textureCache);

  const std::vector<MaterialEntry>& materials() const { return entries_; }

 private:
  std::vector<MaterialEntry> entries_;
};

}  // namespace editor
