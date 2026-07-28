#include "MaterialCatalog.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>

#include "willpower/common/XmlReader.h"

#include "FileDialog.hpp"

namespace editor {

namespace {

// A resource as declared in Resources.xml, stripped down to just what this reader needs:
// enough to walk TrackMaterial -> Material -> Image dependency chains. Mirrors (a small
// subset of) willpower.application::ResourceLocation::scanResourceElement's schema.
struct RawResource {
  std::string type;
  std::string location;
  std::vector<std::pair<std::string, std::string>> dependents;  // id -> ref
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

    auto depsElem = resourceElem->getOptionalChild("DependentResources");
    if (depsElem) {
      auto depElem = depsElem->getOptionalChild("DependentResource");
      if (depElem) {
        do {
          std::string id, ref;
          depElem->getOptionalAttribute("id", id);
          // Inline (non-"ref") dependent resources aren't needed for the TrackMaterial ->
          // Material -> Image chain this reader cares about, so they're skipped.
          if (depElem->getOptionalAttribute("ref", ref)) r.dependents.emplace_back(id, ref);
        } while (depElem->next());
      }
    }

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

// Willpower's own qualified-name convention (Resource::splitName): "namespace/name", or just
// "name" to mean "same namespace as the resource doing the referencing".
std::pair<std::string, std::string> splitRef(const std::string& ref, const std::string& currentNamesp) {
  const auto pos = ref.find('/');
  if (pos == std::string::npos) return {currentNamesp, ref};
  return {ref.substr(0, pos), ref.substr(pos + 1)};
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
      if (res.type != "TrackMaterial") continue;

      const std::string qname = qualifiedName(namesp, name);

      const auto materialDep = std::find_if(res.dependents.begin(), res.dependents.end(),
                                             [](const auto& dep) { return dep.first == "Material"; });
      if (materialDep == res.dependents.end()) {
        std::fprintf(stderr, "MaterialCatalog: TrackMaterial '%s' has no 'Material' dependent resource; skipping.\n",
                     qname.c_str());
        continue;
      }

      const auto [materialNamesp, materialName] = splitRef(materialDep->second, namesp);
      const RawResource* material = findResource(namespaces, materialNamesp, materialName);
      if (material == nullptr) {
        std::fprintf(stderr, "MaterialCatalog: TrackMaterial '%s' references missing Material '%s'; skipping.\n",
                     qname.c_str(), materialDep->second.c_str());
        continue;
      }

      MaterialEntry entry;
      entry.namesp = namesp;
      entry.name = name;
      entry.qualifiedName = qname;

      for (const auto& [depId, depRef] : material->dependents) {
        const auto [imageNamesp, imageName] = splitRef(depRef, materialNamesp);
        const RawResource* image = findResource(namespaces, imageNamesp, imageName);
        if (image == nullptr || image->type != "Image" || image->location.empty()) continue;

        const std::filesystem::path resolved = (baseDir / image->location).lexically_normal();
        const std::string resolvedStr = pathToUtf8(resolved);

        const LoadedTexture& tex = textureCache.get(resolvedStr);
        if (!tex.ok()) {
          std::fprintf(stderr, "MaterialCatalog: TrackMaterial '%s' texture '%s' failed to load; skipping it.\n",
                       qname.c_str(), resolvedStr.c_str());
          continue;
        }

        entry.texturePaths.push_back(resolvedStr);
      }

      if (entry.texturePaths.empty()) {
        std::fprintf(stderr, "MaterialCatalog: TrackMaterial '%s' has no resolvable texture; skipping.\n", qname.c_str());
        continue;
      }

      catalog.entries_.push_back(std::move(entry));
    }
  }

  if (catalog.entries_.empty()) {
    throw std::runtime_error("No TrackMaterial resources found in '" + pathToUtf8(resourcesXmlPath) + "'.");
  }

  // Rails and mesh regions always export with these two fixed materials (see TrackBake.cpp/
  // TrackMesh.cpp's hardcoded "Tracks/DefaultRailMaterial"/"Tracks/DefaultMeshMaterial"
  // materialKey values) -- fail hard here rather than let a .mppmodel silently reference a
  // material Resources.xml doesn't actually define.
  for (const char* requiredMaterial : {"DefaultRailMaterial", "DefaultMeshMaterial"}) {
    const RawResource* material = findResource(namespaces, "Tracks", requiredMaterial);
    if (material == nullptr || material->type != "Material") {
      throw std::runtime_error(std::string("Required Material 'Tracks/") + requiredMaterial + "' not found in '" +
                                pathToUtf8(resourcesXmlPath) + "'.");
    }
  }

  return catalog;
}

}  // namespace editor
