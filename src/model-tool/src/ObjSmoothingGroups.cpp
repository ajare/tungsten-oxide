#include "ObjSmoothingGroups.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>

#include "PositionKey.hpp"

namespace modeltool {
namespace {

struct RawFace {
  std::vector<PositionKey> positions;  // deduplicated, for containment checks below
  int group{0};
};

struct RawPosition {
  double x{0.0}, y{0.0}, z{0.0};
};

std::string lowerExtension(const std::string& utf8Path) {
  const std::size_t dot = utf8Path.find_last_of('.');
  if (dot == std::string::npos) return "";
  std::string ext = utf8Path.substr(dot);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

// Parses the leading integer of an OBJ face-vertex token ("12", "12/4", "12//7", "12/4/7"),
// resolving OBJ's 1-based (or negative, relative-to-current-count) indexing into a 0-based index
// into `positionCount` positions seen so far. Returns nullopt on anything malformed.
std::optional<std::size_t> parseFaceVertexIndex(const std::string& token, std::size_t positionCount) {
  if (token.empty()) return std::nullopt;
  try {
    std::size_t consumed = 0;
    const long long raw = std::stoll(token, &consumed);
    const long long resolved = raw > 0 ? raw - 1 : static_cast<long long>(positionCount) + raw;
    if (resolved < 0 || static_cast<std::size_t>(resolved) >= positionCount) return std::nullopt;
    return static_cast<std::size_t>(resolved);
  } catch (...) {
    return std::nullopt;
  }
}

// Parses the raw file into one RawFace per `f` line, tracking the active smoothing group. `s off`/
// `s 0` gets a fresh, always-unique negative id per face so it never matches another face's group
// (including another `s off` face) -- mirrors OBJ's own "no smoothing" semantics for that face.
std::vector<RawFace> parseObjFaces(std::istream& input) {
  std::vector<RawPosition> positions;
  std::vector<RawFace> faces;
  int currentGroup = 0;
  int nextOffGroupId = -1;

  std::string line;
  while (std::getline(input, line)) {
    std::istringstream tokens(line);
    std::string tag;
    tokens >> tag;
    if (tag == "v") {
      double x = 0.0, y = 0.0, z = 0.0;
      tokens >> x >> y >> z;
      positions.push_back({x, y, z});
    } else if (tag == "s") {
      std::string value;
      tokens >> value;
      currentGroup = (value == "off" || value == "0") ? nextOffGroupId-- : std::atoi(value.c_str());
    } else if (tag == "f") {
      RawFace face;
      face.group = currentGroup;
      std::string vertexToken;
      bool malformed = false;
      std::vector<PositionKey> raw;
      while (tokens >> vertexToken) {
        const auto index = parseFaceVertexIndex(vertexToken, positions.size());
        if (!index.has_value()) {
          malformed = true;
          break;
        }
        const RawPosition& p = positions[*index];
        raw.push_back(quantizePosition(p.x, p.y, p.z));
      }
      if (malformed || raw.size() < 3) continue;
      std::sort(raw.begin(), raw.end());
      raw.erase(std::unique(raw.begin(), raw.end()), raw.end());
      face.positions = std::move(raw);
      faces.push_back(std::move(face));
    }
    // Every other tag (vt, vn, vp, mtllib, usemtl, g, o, #comment, ...) is irrelevant here.
  }
  return faces;
}

}  // namespace

std::optional<std::vector<MeshTriangleGroups>> extractObjSmoothingGroups(const std::string& utf8Path, const ImportedModel& model) {
  if (lowerExtension(utf8Path) != ".obj") return std::nullopt;

  std::ifstream file(utf8Path);
  if (!file) return std::nullopt;
  const std::vector<RawFace> rawFaces = parseObjFaces(file);
  if (rawFaces.empty()) return std::nullopt;

  // Index raw faces by each of their own (deduplicated) positions, so a query triangle only has to
  // check faces that could possibly contain all three of its corners, not every face in the file.
  std::multimap<PositionKey, std::size_t> facesByPosition;
  for (std::size_t i = 0; i < rawFaces.size(); ++i)
    for (const PositionKey& p : rawFaces[i].positions) facesByPosition.emplace(p, i);

  auto candidatesFor = [&](const PositionKey& p) {
    std::vector<std::size_t> out;
    const auto range = facesByPosition.equal_range(p);
    for (auto it = range.first; it != range.second; ++it) out.push_back(it->second);
    std::sort(out.begin(), out.end());
    return out;
  };

  // A triangulated n-gon's every sub-triangle uses only vertices the original face already had, so
  // the correct raw face is whichever candidate's own position set contains all three of the
  // triangle's corners. Ambiguity (more than one candidate containing all three) is only possible
  // for pathological input -- e.g. two exactly coincident faces -- and resolved by taking the first
  // match; documented as an accepted limitation rather than something worth resolving perfectly.
  auto matchFace = [&](const PositionKey& a, const PositionKey& b, const PositionKey& c) -> std::optional<std::size_t> {
    std::vector<std::size_t> candidates = candidatesFor(a);
    for (const PositionKey& corner : {b, c}) {
      const std::vector<std::size_t> next = candidatesFor(corner);
      std::vector<std::size_t> intersected;
      std::set_intersection(candidates.begin(), candidates.end(), next.begin(), next.end(), std::back_inserter(intersected));
      candidates = std::move(intersected);
      if (candidates.empty()) return std::nullopt;
    }
    return candidates.front();
  };

  std::vector<MeshTriangleGroups> result(model.meshes.size());
  bool matchedAny = false;
  for (std::size_t m = 0; m < model.meshes.size(); ++m) {
    const ImportedMesh& mesh = model.meshes[m];
    const std::size_t triangleCount = mesh.indices.size() / 3;
    result[m].triangleGroup.assign(triangleCount, 0);
    for (std::size_t t = 0; t < triangleCount; ++t) {
      const ImportedVertex& va = mesh.vertices[mesh.indices[t * 3 + 0]];
      const ImportedVertex& vb = mesh.vertices[mesh.indices[t * 3 + 1]];
      const ImportedVertex& vc = mesh.vertices[mesh.indices[t * 3 + 2]];
      const auto matched = matchFace(quantizePosition(va.px, va.py, va.pz), quantizePosition(vb.px, vb.py, vb.pz), quantizePosition(vc.px, vc.py, vc.pz));
      if (matched.has_value()) {
        result[m].triangleGroup[t] = rawFaces[*matched].group;
        matchedAny = true;
      }
      // No match (shouldn't normally happen -- every AssImp triangle should trace back to some raw
      // face): falls back to synthetic group 0, i.e. this triangle smooths with every other
      // unmatched triangle in the same mesh, the same safe default as "no smoothing groups at all".
    }
  }
  return matchedAny ? std::make_optional(std::move(result)) : std::nullopt;
}

}  // namespace modeltool
