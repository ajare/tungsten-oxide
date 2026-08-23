#include "MaterialCatalog.hpp"

#include <cstdio>
#include <map>
#include <stdexcept>

#include <yaml-cpp/yaml.h>

#include "FileDialog.hpp"

namespace editor {
namespace {

struct RawResource {
  std::string type;
  std::string location;
  std::string editorTrackMaterial;
  std::string editorMaterialKey;
  std::string editorTexture;
};

using NamespaceMap = std::map<std::string, std::map<std::string, RawResource>>;

std::string scalar(const YAML::Node& node, const char* key) {
  const YAML::Node value = node[key];
  return value && value.IsScalar() ? value.as<std::string>() : std::string{};
}

void scanResourceElement(const YAML::Node& parent, const std::string& namesp, NamespaceMap& out) {
  YAML::Node resources = parent["Resource"];
  if (!resources) return;
  if (!resources.IsSequence()) {
    YAML::Node sequence(YAML::NodeType::Sequence);
    sequence.push_back(resources);
    resources = sequence;
  }

  for (const YAML::Node& resource : resources) {
    std::string name = scalar(resource, "name");
    const std::string location = scalar(resource, "location");
    if (name.empty()) name = location;
    if (name.empty()) continue;

    RawResource raw;
    raw.type = scalar(resource, "type");
    raw.location = location;
    raw.editorTrackMaterial = scalar(resource, "editorTrackMaterial");
    raw.editorMaterialKey = scalar(resource, "editorMaterialKey");
    raw.editorTexture = scalar(resource, "editorTexture");
    out[namesp][name] = std::move(raw);
  }
}

NamespaceMap scanResources(const YAML::Node& root) {
  NamespaceMap namespaces;
  YAML::Node namespaceNodes = root["Namespace"];
  if (namespaceNodes) {
    if (!namespaceNodes.IsSequence()) {
      YAML::Node sequence(YAML::NodeType::Sequence);
      sequence.push_back(namespaceNodes);
      namespaceNodes = sequence;
    }
    for (const YAML::Node& namesp : namespaceNodes) scanResourceElement(namesp, scalar(namesp, "name"), namespaces);
  }
  scanResourceElement(root, "", namespaces);
  return namespaces;
}

const RawResource* findResource(const NamespaceMap& namespaces, const std::string& namesp, const std::string& name) {
  const auto namespIt = namespaces.find(namesp);
  if (namespIt == namespaces.end()) return nullptr;
  const auto resIt = namespIt->second.find(name);
  return resIt == namespIt->second.end() ? nullptr : &resIt->second;
}

std::string qualifiedName(const std::string& namesp, const std::string& name) {
  return namesp.empty() ? name : namesp + "/" + name;
}

}  // namespace

MaterialCatalog MaterialCatalog::load(const std::filesystem::path& resourcesYamlPath, TextureCache& textureCache) {
  const YAML::Node document = YAML::LoadFile(pathToUtf8(resourcesYamlPath));
  const YAML::Node root = document["Resources"];
  if (!root || !root.IsMap()) throw std::runtime_error("Resource YAML has no Resources mapping.");
  const NamespaceMap namespaces = scanResources(root);
  const std::filesystem::path baseDir = resourcesYamlPath.parent_path();
  MaterialCatalog catalog;

  for (const auto& [namesp, resources] : namespaces) {
    for (const auto& [name, res] : resources) {
      if (res.type != "PbrMaterialBinding" || res.editorTrackMaterial.empty()) continue;
      const std::string qname = qualifiedName(namesp, res.editorTrackMaterial);
      if (res.editorMaterialKey.empty() || res.editorTexture.empty()) {
        std::fprintf(stderr, "MaterialCatalog: PBR track material '%s' has incomplete editor metadata; skipping.\n", qname.c_str());
        continue;
      }
      const std::filesystem::path resolved = (baseDir / res.editorTexture).lexically_normal();
      const std::string resolvedStr = pathToUtf8(resolved);
      if (!textureCache.get(resolvedStr).ok()) {
        std::fprintf(stderr, "MaterialCatalog: PBR track material '%s' texture '%s' failed to load; skipping.\n", qname.c_str(),
                     resolvedStr.c_str());
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

  if (catalog.entries_.empty())
    throw std::runtime_error("No editor-authored PBR track materials found in '" + pathToUtf8(resourcesYamlPath) + "'.");

  for (const char* requiredMaterial :
       {"DefaultRailMaterial", "DefaultMeshMaterial", "DefaultShellMaterial", "DefaultZoneMaterial", "DefaultTriggerMaterial"}) {
    const RawResource* material = findResource(namespaces, "Tracks", requiredMaterial);
    if (material == nullptr || material->type != "PbrMaterialBinding" ||
        material->editorMaterialKey != std::string("Tracks/") + requiredMaterial) {
      throw std::runtime_error(std::string("Required PBR material binding 'Tracks/") + requiredMaterial + "' not found in '" +
                               pathToUtf8(resourcesYamlPath) + "'.");
    }
  }
  return catalog;
}

}  // namespace editor
