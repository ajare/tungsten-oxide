// MaterialXmlImport.hpp — reads a Resources.xml-shaped file (the shape
// src/tungsten-monoxide/resources/Resources.xml uses) looking for <Resource type="Material">
// entries, resolving each one's dependent "Texture" resource down to a real image file path.
//
// A standalone reader built directly on Willpower.Common's XmlReader, mirroring
// src/editor/src/MaterialCatalog.cpp's approach (also documented there: this deliberately does
// NOT go through willpower.application::ResourceManager's own XML-driven resource loading --
// that layer has no single-file entry point, silently overwrites same-named resources instead of
// reporting collisions, and has no removal API; see docs/adr/0001-model-tool.md's Materials-XML-
// import decision). Unlike MaterialCatalog (which walks a TrackMaterial -> Material -> Image
// chain), this walks Material -> Image directly, one level shallower -- matching the shape a
// hand-authored Resources.xml already uses for its own Material declarations (model-tool itself no
// longer generates one; see TRACK_MODEL_LIST_PLAN.md's retirement of the old companion
// materials-XML export, formerly ModelResourceExport.hpp).
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace modeltool {

struct ImportedMaterialXmlEntry {
  std::string qualifiedName;               // "namespace/name", or just "name" if unnamespaced
  std::optional<std::string> texturePath;  // resolved (absolute) path; nullopt -> untextured
};

struct ImportedMaterialXmlFile {
  std::string sourcePath;
  std::vector<ImportedMaterialXmlEntry> materials;
};

// Returns nullopt and fills `outError` if the file can't be read/parsed, or declares no
// `type="Material"` resources at all.
std::optional<ImportedMaterialXmlFile> importMaterialXml(const std::string& utf8Path, std::string* outError);

}  // namespace modeltool
