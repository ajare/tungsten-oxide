#include "USDExport.hpp"

#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace editor {
namespace {

// Mirrors usd-export.js's sanitizeUsdIdentifier: USD prim/property names are C-identifier-like.
std::string sanitizeUsdIdentifier(const std::string& value, const std::string& fallback) {
  std::string out;
  out.reserve(value.size());
  for (char c : value) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') out += c;
    else if (!out.empty() && out.back() != '_') out += '_';
  }
  while (!out.empty() && out.back() == '_') out.pop_back();
  std::size_t start = 0;
  while (start < out.size() && out[start] == '_') ++start;
  out = out.substr(start);
  if (out.empty()) out = fallback;
  if (!std::isalpha(static_cast<unsigned char>(out[0])) && out[0] != '_') out = "_" + out;
  return out;
}

std::string uniqueName(const std::string& base, std::set<std::string>& used) {
  const std::string clean = sanitizeUsdIdentifier(base, "Prim");
  if (used.insert(clean).second) return clean;
  for (int i = 2;; ++i) {
    const std::string candidate = clean + "_" + std::to_string(i);
    if (used.insert(candidate).second) return candidate;
  }
}

// Mirrors usd-export.js's fmt: six decimal places, trailing zeros trimmed, -0 folded to 0.
std::string fmt(double value) {
  const double v = std::abs(value) < 1e-12 ? 0.0 : value;
  std::ostringstream out;
  out.precision(6);
  out << std::fixed << v;
  std::string s = out.str();
  const auto dot = s.find('.');
  if (dot != std::string::npos) {
    std::size_t last = s.find_last_not_of('0');
    if (last == dot) --last;  // drop a bare trailing "."
    s.erase(last + 1);
  }
  if (s == "-0") s = "0";
  return s;
}

// Minimal USD string-literal escaping (backslash, double-quote) for the free-text trackName --
// mirrors what JSON.stringify does for usd-export.js's equivalent field.
std::string escapeUsdString(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (char c : value) {
    if (c == '\\' || c == '"') out += '\\';
    out += c;
  }
  return out;
}

std::string point3(const tox::Vec3& p) { return "(" + fmt(p.x) + ", " + fmt(p.y) + ", " + fmt(p.z) + ")"; }
std::string texCoord2(const tox::Vec2d& uv) { return "(" + fmt(uv.x) + ", " + fmt(uv.y) + ")"; }

struct MaterialStyle {
  double r, g, b, roughness;
};

// A small fixed palette keyed by core's semantic materialKey strings (TrackBake.cpp/TrackMesh.cpp:
// "road", "shell", "rail", "mesh-region", "zone-<effect>"); unrecognized/future keys fall back to
// a neutral gray rather than failing the export.
MaterialStyle styleFor(const std::string& materialKey) {
  if (materialKey == "road") return {0.32, 0.55, 0.65, 0.75};
  if (materialKey == "shell") return {0.23, 0.36, 0.45, 0.9};
  if (materialKey == "rail") return {0.75, 0.35, 0.25, 0.6};
  if (materialKey == "mesh-region") return {0.42, 0.31, 0.59, 0.8};
  if (materialKey.rfind("zone-", 0) == 0) return {0.85, 0.75, 0.2, 0.5};
  return {0.6, 0.6, 0.6, 0.8};
}

void writeMaterials(std::vector<std::string>& lines, const std::vector<std::string>& materialKeys) {
  lines.push_back("    def Scope \"Materials\"");
  lines.push_back("    {");
  for (const auto& key : materialKeys) {
    const std::string name = sanitizeUsdIdentifier(key, "Material");
    const MaterialStyle style = styleFor(key);
    lines.push_back("        def Material \"" + name + "\"");
    lines.push_back("        {");
    lines.push_back("            def Shader \"PreviewSurface\"");
    lines.push_back("            {");
    lines.push_back("                uniform token info:id = \"UsdPreviewSurface\"");
    lines.push_back("                color3f inputs:diffuseColor = (" + fmt(style.r) + ", " + fmt(style.g) + ", " + fmt(style.b) + ")");
    lines.push_back("                float inputs:roughness = " + fmt(style.roughness));
    lines.push_back("                token outputs:surface");
    lines.push_back("            }");
    lines.push_back("            token outputs:surface.connect = </Track/Materials/" + name + "/PreviewSurface.outputs:surface>");
    lines.push_back("        }");
  }
  lines.push_back("    }");
}

void writeMeshPrim(std::vector<std::string>& lines, const tox::GeometryBatch& batch, std::set<std::string>& usedNames) {
  const std::string name = uniqueName(batch.id.empty() ? batch.materialKey : batch.id, usedNames);
  const std::string materialName = sanitizeUsdIdentifier(batch.materialKey, "Material");

  lines.push_back("    def Mesh \"" + name + "\" (");
  lines.push_back("        prepend apiSchemas = [\"MaterialBindingAPI\"]");
  lines.push_back("    )");
  lines.push_back("    {");
  lines.push_back("        rel material:binding = </Track/Materials/" + materialName + ">");

  std::string points = "        point3f[] points = [";
  for (std::size_t i = 0; i < batch.vertices.size(); ++i) {
    if (i > 0) points += ", ";
    points += point3(batch.vertices[i].position);
  }
  points += "]";
  lines.push_back(points);

  const std::size_t triangleCount = batch.indices.size() / 3;
  std::string counts = "        int[] faceVertexCounts = [";
  for (std::size_t i = 0; i < triangleCount; ++i) counts += (i > 0 ? ", 3" : "3");
  counts += "]";
  lines.push_back(counts);

  std::string indices = "        int[] faceVertexIndices = [";
  for (std::size_t i = 0; i < batch.indices.size(); ++i) {
    if (i > 0) indices += ", ";
    indices += std::to_string(batch.indices[i]);
  }
  indices += "]";
  lines.push_back(indices);

  if (batch.hasUv) {
    std::string uvs = "        texCoord2f[] primvars:st = [";
    for (std::size_t i = 0; i < batch.vertices.size(); ++i) {
      if (i > 0) uvs += ", ";
      uvs += texCoord2(batch.vertices[i].uv);
    }
    uvs += "]";
    lines.push_back(uvs);
    lines.push_back("        uniform token primvars:st:interpolation = \"vertex\"");
  }
  lines.push_back("        uniform token subdivisionScheme = \"none\"");
  lines.push_back("    }");
}

}  // namespace

USDExportResult exportTrackToUSDA(const tox::Track& track) {
  std::vector<std::string> materialKeys;
  std::set<std::string> seenMaterialKeys;
  for (const auto& batch : track.geometry) {
    if (seenMaterialKeys.insert(batch.materialKey).second) materialKeys.push_back(batch.materialKey);
  }

  std::vector<std::string> lines = {
      "#usda 1.0",
      "(",
      "    defaultPrim = \"Track\"",
      "    metersPerUnit = 1",
      "    upAxis = \"Y\"",
      ")",
      "",
      "def Xform \"Track\"",
      "{",
      "    custom string trackName = \"" + escapeUsdString(track.definition.name) + "\"",
  };
  writeMaterials(lines, materialKeys);

  std::set<std::string> usedNames = {"Materials"};
  for (const auto& batch : track.geometry) writeMeshPrim(lines, batch, usedNames);

  lines.push_back("}");
  lines.push_back("");

  std::string text;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    text += lines[i];
    if (i + 1 < lines.size()) text += '\n';
  }
  return {text, track.geometry.size()};
}

}  // namespace editor
