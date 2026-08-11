#include "MaterialCatalog.hpp"

#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>

#include "willpower/common/XmlReader.h"

#include "FileDialog.hpp"

namespace editor {

namespace {

// A resource as declared in Resources.xml, stripped down to just what this reader needs.
// PbrMaterialBinding resources may carry editor-only authoring metadata as attributes; the
// runtime factory ignores those attributes and reads only the logical package Binding.
struct RawResource {
  std::string type;
  std::string location;
  std::string editorTrackMaterial;
  std::string editorMaterialKey;
  std::string editorTexture;
};

// namespace name -> resource name -> RawResource. The root (non-namespaced) resources live
// under the "" key, matching Resource::splitName's convention in willpower.application.
using NamespaceMap = std::map<std::string, std::map<std::string, RawResource>>;

void scanResourceElement(wp::XmlNode* parent, const std::string& namesp, NamespaceMap& out) {
  auto resourceElem = parent->getOptionalChild("Resource");
  if (!resourceElem) return;

  do {
    std::string type, name, location;
    resourceElem->getOptionalAttribute("type", type);
    resourceElem->getOptionalAttribute("name", name);
    resourceElem->getOptionalAttribute("location", location);
    if (name.empty()) name = location;  // matches ResourceLocation::validateResourceRecordBaseData
    if (name.empty()) continue;

    RawResource r;
    r.type = type;
    r.location = location;
    resourceElem->getOptionalAttribute("editorTrackMaterial", r.editorTrackMaterial);
    resourceElem->getOptionalAttribute("editorMaterialKey", r.editorMaterialKey);
    resourceElem->getOptionalAttribute("editorTexture", r.editorTexture);

    out[namesp][name] = std::move(r);
  } while (resourceElem->next());
}

NamespaceMap scanResources(wp::XmlReader& reader) {
  NamespaceMap namespaces;

  auto root = reader.getNode("Resources");

  auto namespaceElem = root->getOptionalChild("Namespace");
  if (namespaceElem) {
    do {
      const std::string namesp = namespaceElem->getAttribute("name");
      scanResourceElement(namespaceElem, namesp, namespaces);
    } while (namespaceElem->next());
  }

  scanResourceElement(root, "", namespaces);

  return namespaces;
}

const RawResource* findResource(const NamespaceMap& namespaces, const std::string& namesp, const std::string& name) {
  const auto namespIt = namespaces.find(namesp);
  if (namespIt == namespaces.end()) return nullptr;

  const auto resIt = namespIt->second.find(name);
  if (resIt == namespIt->second.end()) return nullptr;

  return &resIt->second;
}

std::string qualifiedName(const std::string& namesp, const std::string& name) {
  return namesp.empty() ? name : namesp + "/" + name;
}

}  // namespace

MaterialCatalog MaterialCatalog::load(const std::filesystem::path& resourcesXmlPath, TextureCache& textureCache) {
  // XmlReader::fromFile throws wp::XmlException if the file can't be found/opened; getNode()
  // throws wp::XmlPathException if the root element isn't "Resources". Both are structural
  // failures this reader deliberately does NOT catch -- they're meant to propagate to the
  // caller as a fatal startup error.
  std::unique_ptr<wp::XmlReader> reader(wp::XmlReader::fromFile(pathToUtf8(resourcesXmlPath)));
  const NamespaceMap namespaces = scanResources(*reader);

  const std::filesystem::path baseDir = resourcesXmlPath.parent_path();

  MaterialCatalog catalog;

  for (const auto& [namesp, resources] : namespaces) {
    for (const auto& [name, res] : resources) {
      if (res.type != "PbrMaterialBinding" || res.editorTrackMaterial.empty()) continue;

      const std::string qname = qualifiedName(namesp, res.editorTrackMaterial);
      if (res.editorMaterialKey.empty() || res.editorTexture.empty()) {
        std::fprintf(stderr, "MaterialCatalog: PBR track material '%s' has incomplete editor metadata; skipping.\n",
                     qname.c_str());
        continue;
      }

      const std::filesystem::path resolved = (baseDir / res.editorTexture).lexically_normal();
      const std::string resolvedStr = pathToUtf8(resolved);
      if (!textureCache.get(resolvedStr).ok()) {
        std::fprintf(stderr, "MaterialCatalog: PBR track material '%s' texture '%s' failed to load; skipping.\n",
                     qname.c_str(), resolvedStr.c_str());
        continue;
      }

      MaterialEntry entry;
      entry.namesp = namesp;
      entry.name = res.editorTrackMaterial;
      entry.qualifiedName = qname;
      entry.materialQualifiedName = res.editorMaterialKey;
      entry.texturePaths.push_back(resolvedStr);
      catalog.entries_.push_back(std::move(entry));
    }
  }

  if (catalog.entries_.empty()) {
    throw std::runtime_error("No editor-authored PBR track materials found in '" + pathToUtf8(resourcesXmlPath) + "'.");
  }

  // Rails, meshes, shells, zones, and triggers always export with these fixed material keys.
  // Their resources must now be package bindings rather than legacy render Materials.
  for (const char* requiredMaterial :
       {"DefaultRailMaterial", "DefaultMeshMaterial", "DefaultShellMaterial", "DefaultZoneMaterial", "DefaultTriggerMaterial"}) {
    const RawResource* material = findResource(namespaces, "Tracks", requiredMaterial);
    if (material == nullptr || material->type != "PbrMaterialBinding" ||
        material->editorMaterialKey != std::string("Tracks/") + requiredMaterial) {
      throw std::runtime_error(std::string("Required PBR material binding 'Tracks/") + requiredMaterial + "' not found in '" +
                               pathToUtf8(resourcesXmlPath) + "'.");
    }
  }

  return catalog;
}

}  // namespace editor
