#include "ModelResourceExport.hpp"

#include <set>

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

}  // namespace

std::string buildModelMaterialsXml(const ImportedModel& model, const std::string& namespaceName) {
  std::string xml = "<?xml version=\"1.0\"?>\n<Resources>\n\t<Namespace name=\"" + xmlEscape(namespaceName) + "\">\n";

  // Dedupe resource names within this file only -- ImportedMaterial::name comes straight from the
  // source asset (AI_MATKEY_NAME) and commonly repeats ("material", "" for unnamed materials) or
  // collides across meshes that otherwise use distinct materials.
  std::set<std::string> seenNames;
  for (const ImportedMaterial& material : model.materials) {
    std::string materialName = material.name.empty() ? "Material" : material.name;
    while (!seenNames.insert(materialName).second) materialName += "_";

    // Real texture: its own Image resource, named after the material. No texture (ADR 0001 D7):
    // mpp's built-in "__mpp_tex_none__" sentinel, already registered in every process that
    // constructs a RenderSystem -- no Image declaration needed or possible for it here.
    std::string textureRef = "__mpp_tex_none__";
    if (material.diffuseTexturePath.has_value()) {
      textureRef = materialName + "Texture";
      xml += "\t\t<Resource type=\"Image\" name=\"" + xmlEscape(textureRef) + "\" location=\"" +
             xmlEscape(*material.diffuseTexturePath) + "\" />\n";
    }

    xml += "\t\t<Resource type=\"Material\" name=\"" + xmlEscape(materialName) + "\">\n";
    xml += "\t\t\t<DependentResources>\n";
    xml += "\t\t\t\t<DependentResource id=\"Program\" ref=\"__mpp_p3d_tris_p3n3t2c4__\" />\n";
    xml += "\t\t\t\t<DependentResource id=\"Texture\" ref=\"" + xmlEscape(textureRef) + "\" />\n";
    xml += "\t\t\t</DependentResources>\n";
    xml += "\t\t\t<Definitions>\n\t\t\t\t<Definition>\n\t\t\t\t\t<Textures>\n";
    xml += "\t\t\t\t\t\t<Texture type=\"resource\" sampler=\"TEX1\">Texture</Texture>\n";
    xml += "\t\t\t\t\t</Textures>\n\t\t\t\t</Definition>\n\t\t\t</Definitions>\n";
    xml += "\t\t</Resource>\n\n";
  }

  xml += "\t</Namespace>\n</Resources>\n";
  return xml;
}

}  // namespace modeltool
