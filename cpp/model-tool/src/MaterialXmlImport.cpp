#include "MaterialXmlImport.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <utility>

#include "willpower/common/XmlReader.h"

namespace modeltool {
namespace {

// A resource as declared in the file, stripped down to just what this reader needs -- mirrors
// cpp/editor/src/MaterialCatalog.cpp's own RawResource/NamespaceMap/scanResourceElement/splitRef/
// findResource helpers verbatim (kept as a separate copy rather than shared: editor and
// model-tool are different executables with no common library between them for this).
struct RawResource {
  std::string type;
  std::string location;
  std::vector<std::pair<std::string, std::string>> dependents;  // id -> ref
};

using NamespaceMap = std::map<std::string, std::map<std::string, RawResource>>;

void scanResourceElement(wp::XmlNode* parent, const std::string& namesp, NamespaceMap& out) {
  auto resourceElem = parent->getOptionalChild("Resource");
  if (!resourceElem) return;

  do {
    std::string type, name, location;
    resourceElem->getOptionalAttribute("type", type);
    resourceElem->getOptionalAttribute("name", name);
    resourceElem->getOptionalAttribute("location", location);
    if (name.empty()) name = location;
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

// Willpower's qualified-name convention (Resource::splitName): "namespace/name", or just "name"
// to mean "same namespace as the resource doing the referencing".
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

std::optional<ImportedMaterialXmlFile> importMaterialXml(const std::string& utf8Path, std::string* outError) {
  try {
    std::unique_ptr<wp::XmlReader> reader(wp::XmlReader::fromFile(utf8Path));
    const NamespaceMap namespaces = scanResources(*reader);
    const std::filesystem::path baseDir = std::filesystem::path(utf8Path).parent_path();

    ImportedMaterialXmlFile file;
    file.sourcePath = utf8Path;

    for (const auto& [namesp, resources] : namespaces) {
      for (const auto& [name, res] : resources) {
        if (res.type != "Material") continue;

        ImportedMaterialXmlEntry entry;
        entry.qualifiedName = qualifiedName(namesp, name);

        const auto textureDep = std::find_if(res.dependents.begin(), res.dependents.end(),
                                              [](const auto& dep) { return dep.first == "Texture"; });
        // "__mpp_tex_none__" (ADR 0001 D7's sentinel, and what buildModelMaterialsXml itself
        // writes for an untextured material) has no Image resource declared for it anywhere --
        // that's expected, not a broken reference, so it's treated the same as "no Texture
        // dependent at all" rather than logged as missing.
        if (textureDep != res.dependents.end() && textureDep->second != "__mpp_tex_none__") {
          const auto [imageNamesp, imageName] = splitRef(textureDep->second, namesp);
          const RawResource* image = findResource(namespaces, imageNamesp, imageName);
          if (image != nullptr && image->type == "Image" && !image->location.empty()) {
            std::filesystem::path location(image->location);
            if (location.is_relative()) location = baseDir / location;
            entry.texturePath = location.lexically_normal().string();
          }
        }

        file.materials.push_back(std::move(entry));
      }
    }

    if (file.materials.empty()) {
      if (outError) *outError = "No Material resources found in '" + utf8Path + "'.";
      return std::nullopt;
    }

    return file;
  } catch (const std::exception& error) {
    if (outError) *outError = error.what();
    return std::nullopt;
  }
}

}  // namespace modeltool
