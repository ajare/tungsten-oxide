#include "ModelResourceExport.hpp"

#include <map>
#include <set>
#include <utility>

namespace modeltool {
namespace {

std::string xmlEscape(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (char c : value) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c; break;
    }
  }
  return out;
}

// Willpower's own qualified-name convention (Resource::splitName), mirrored by
// MaterialXmlImport.cpp's splitRef(): "namespace/leaf", or just "leaf" for an unnamespaced
// resource -- split on the FIRST '/' so a leaf name containing '/' isn't itself possible (matches
// the read side exactly).
std::pair<std::string, std::string> splitQualifiedName(const std::string& name) {
  const auto pos = name.find('/');
  if (pos == std::string::npos) return {"", name};
  return {name.substr(0, pos), name.substr(pos + 1)};
}

void writeMaterialResource(std::string& xml, const std::string& indent, const std::string& leafName,
                            const std::optional<std::string>& texturePath) {
  std::string textureRef = "__mpp_tex_none__";
  if (texturePath.has_value()) {
    textureRef = leafName + "Texture";
    xml += indent + "<Resource type=\"Image\" name=\"" + xmlEscape(textureRef) + "\" location=\"" + xmlEscape(*texturePath) + "\" />\n";
  }

  xml += indent + "<Resource type=\"Material\" name=\"" + xmlEscape(leafName) + "\">\n";
  xml += indent + "\t<DependentResources>\n";
  xml += indent + "\t\t<DependentResource id=\"Program\" ref=\"__mpp_p3d_tris_p3n3t2c4__\" />\n";
  xml += indent + "\t\t<DependentResource id=\"Texture\" ref=\"" + xmlEscape(textureRef) + "\" />\n";
  xml += indent + "\t</DependentResources>\n";
  xml += indent + "\t<Definitions>\n" + indent + "\t\t<Definition>\n" + indent + "\t\t\t<Textures>\n";
  xml += indent + "\t\t\t\t<Texture type=\"resource\" sampler=\"TEX1\">Texture</Texture>\n";
  xml += indent + "\t\t\t</Textures>\n" + indent + "\t\t</Definition>\n" + indent + "\t</Definitions>\n";
  xml += indent + "</Resource>\n\n";
}

}  // namespace

std::string buildModelMaterialsXml(const ImportedModel& model, const std::string& defaultFallbackMaterialName) {
  // Only materials at least one mesh actually references -- ImportedModel::materials can (and,
  // for a .mppmodel's own embedded materials, deliberately does: "if the model has embedded
  // material definitions, create them and display them") hold entries no mesh currently points at,
  // e.g. an unused material embedded in a loaded .mppmodel that isn't assigned to anything. Nothing
  // that isn't actually in use belongs in what this model exports.
  std::set<std::size_t> referencedMaterialIndices;
  for (const ImportedMesh& mesh : model.meshes) referencedMaterialIndices.insert(static_cast<std::size_t>(mesh.materialIndex));

  // Group by namespace (first-seen namespace order, materials within a namespace in encounter
  // order), deduplicating leaf names only within their own namespace -- matches willpower's actual
  // uniqueness scope, unlike an earlier version of this function which deduped globally.
  std::vector<std::string> namespaceOrder;
  std::map<std::string, std::vector<std::pair<std::string, std::optional<std::string>>>> byNamespace;  // ns -> [(leaf, texturePath)]
  std::map<std::string, std::set<std::string>> seenLeafNames;

  for (std::size_t i = 0; i < model.materials.size(); ++i) {
    if (!referencedMaterialIndices.count(i)) continue;
    const ImportedMaterial& material = model.materials[i];

    // A DefaultFallback entry's own `name` is the original, never-resolved bare material name
    // (kept for display purposes elsewhere) -- what the saved .mppmodel's mesh.material fields
    // actually reference is the shared fallback material's real resource name instead.
    const std::string qualifiedName = material.origin == MaterialOrigin::DefaultFallback ? defaultFallbackMaterialName : material.name;

    auto [namesp, leaf] = splitQualifiedName(qualifiedName);
    if (leaf.empty()) leaf = "Material";

    std::set<std::string>& seen = seenLeafNames[namesp];
    while (!seen.insert(leaf).second) leaf += "_";

    if (byNamespace.find(namesp) == byNamespace.end()) namespaceOrder.push_back(namesp);
    byNamespace[namesp].emplace_back(leaf, material.diffuseTexturePath);
  }

  std::string xml = "<?xml version=\"1.0\"?>\n<Resources>\n";
  for (const std::string& namesp : namespaceOrder) {
    const bool wrapped = !namesp.empty();
    if (wrapped) xml += "\t<Namespace name=\"" + xmlEscape(namesp) + "\">\n";

    const std::string indent = wrapped ? "\t\t" : "\t";
    for (const auto& [leaf, texturePath] : byNamespace.at(namesp)) writeMaterialResource(xml, indent, leaf, texturePath);

    if (wrapped) xml += "\t</Namespace>\n";
  }
  xml += "</Resources>\n";
  return xml;
}

}  // namespace modeltool
