// TrackLoader.cpp — strict current-schema JSON loading and runtime-subset
// normalization. Historical migrations remain JavaScript/editor-only.
#include "Track.hpp"
#include "TrackCore.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "nlohmann/json.hpp"

namespace tox {
namespace {

using nlohmann::json;

bool finite(double value) { return std::isfinite(value); }

double numberOr(const json& object, const char* key, double fallback) {
  if (!object.is_object() || !object.contains(key) || !object.at(key).is_number()) return fallback;
  const double value = object.at(key).get<double>();
  return finite(value) ? value : fallback;
}

double finiteOrMin(const json& object, const char* key, double fallback, double minimum) {
  return std::max(minimum, numberOr(object, key, fallback));
}

double jsNumber(const json& value, double fallback) {
  if (value.is_number()) {
    const double result = value.get<double>();
    return finite(result) ? result : fallback;
  }
  if (value.is_boolean()) return value.get<bool>() ? 1.0 : 0.0;
  if (value.is_null()) return 0.0;
  if (value.is_string()) {
    try {
      const std::string text = value.get<std::string>();
      std::size_t used = 0;
      const double result = std::stod(text, &used);
      if (used == text.size() && finite(result)) return result;
    } catch (...) {
    }
  }
  return fallback;
}

double clampCoerced(const json& object, const char* key, double lo, double hi, double fallback) {
  if (!object.is_object() || !object.contains(key)) return fallback;
  return std::max(lo, std::min(hi, jsNumber(object.at(key), fallback)));
}

double clampNumber(double value, double lo, double hi) { return std::max(lo, std::min(hi, value)); }

double clampSignedUnit(double value) { return finite(value) ? clampNumber(value, -1.0, 1.0) : 0.0; }

double clampTightness(double value) { return finite(value) ? clampNumber(value, 0.2, 4.0) : 1.0; }

double clampThickness(double value) {
  return finite(value) ? std::max(0.0, value) : TrackCore::DEFAULT_CROSS_SECTION_THICKNESS;
}

std::string stringOr(const json& object, const char* key, const std::string& fallback = {}) {
  return object.is_object() && object.contains(key) && object.at(key).is_string() ? object.at(key).get<std::string>() : fallback;
}

bool jsonTruthy(const json& value) {
  if (value.is_null()) return false;
  if (value.is_boolean()) return value.get<bool>();
  if (value.is_number()) return value.get<double>() != 0.0;
  if (value.is_string()) return !value.get_ref<const std::string&>().empty();
  return true;
}

TextureBindingDefinition normalizePathTexture(const json& raw, bool& valid) {
  TextureBindingDefinition result;
  valid = raw.is_object() && raw.contains("asset") && raw.at("asset").is_string() && !raw.at("asset").get_ref<const std::string&>().empty();
  if (!valid) return result;
  result.assetId = raw.at("asset").get<std::string>();
  result.tile = raw.contains("tile") && raw.at("tile").is_number_integer() && raw.at("tile").get<int>() >= 0 ? raw.at("tile").get<int>() : 0;
  return result;
}

TrackPointDefinition normalizePosition(const json& raw, std::size_t pathIndex) {
  if (!raw.is_object() || !raw.contains("pos") || !raw.at("pos").is_array() || raw.at("pos").size() != 3) {
    throw std::runtime_error("path " + std::to_string(pathIndex) + ": pos must be [x,y,z] numbers");
  }
  const json& pos = raw.at("pos");
  for (const auto& value : pos)
    if (!value.is_number() || !finite(value.get<double>()))
      throw std::runtime_error("path " + std::to_string(pathIndex) + ": pos must be [x,y,z] numbers");

  TrackPointDefinition point;
  point.kind = TrackPointKind::Position;
  point.id = stringOr(raw, "id");
  point.pos = Vec3(pos[0].get<double>(), pos[1].get<double>(), pos[2].get<double>());
  point.weight = std::max(0.01, numberOr(raw, "weight", 1.0));
  return point;
}

PathDefinition normalizePath(const json& raw, std::size_t pathIndex, double topLevelCurvature) {
  if (!raw.is_object() || !raw.contains("points") || !raw.at("points").is_array())
    throw std::runtime_error("path " + std::to_string(pathIndex) + ": no points array found");

  PathDefinition path;
  path.id = stringOr(raw, "id");
  path.closed = !(raw.contains("closed") && raw.at("closed").is_boolean() && !raw.at("closed").get<bool>());
  bool hasRoll = false, hasWidth = false, hasCrossSection = false;
  for (const auto& source : raw.at("points")) {
    const std::string type = stringOr(source, "type");
    TrackPointDefinition point;
    if (type == "roll") {
      point.kind = TrackPointKind::Roll;
      point.t = clampNumber(numberOr(source, "t", 0.0), 0.0, 1.0);
      point.roll = clampNumber(numberOr(source, "roll", 0.0), -180.0, 180.0);
      hasRoll = true;
    } else if (type == "width") {
      point.kind = TrackPointKind::Width;
      point.t = clampNumber(numberOr(source, "t", 0.0), 0.0, 1.0);
      point.width = std::max(1.0, numberOr(source, "width", TrackCore::DEFAULT_WIDTH));
      hasWidth = true;
    } else if (type == "crossSection") {
      point.kind = TrackPointKind::CrossSection;
      point.t = clampNumber(numberOr(source, "t", 0.0), 0.0, 1.0);
      point.curvature = clampSignedUnit(numberOr(source, "curvature", 0.0));
      point.tightness = clampTightness(numberOr(source, "tightness", 1.0));
      point.thickness = clampThickness(numberOr(source, "thickness", TrackCore::DEFAULT_CROSS_SECTION_THICKNESS));
      hasCrossSection = true;
    } else {
      point = normalizePosition(source, pathIndex);
    }
    path.points.push_back(std::move(point));
  }

  const auto positionCount = std::count_if(path.points.begin(), path.points.end(), [](const auto& p) { return p.kind == TrackPointKind::Position; });
  if (positionCount < 4) throw std::runtime_error("path " + std::to_string(pathIndex) + ": a track path needs at least 4 position control points");
  const double endT = path.closed ? 0.5 : 1.0;
  if (!hasRoll) {
    TrackPointDefinition a, b;
    a.kind = b.kind = TrackPointKind::Roll;
    b.t = endT;
    path.points.push_back(a);
    path.points.push_back(b);
  }
  if (!hasWidth) {
    TrackPointDefinition a, b;
    a.kind = b.kind = TrackPointKind::Width;
    a.width = b.width = TrackCore::DEFAULT_WIDTH;
    b.t = endT;
    path.points.push_back(a);
    path.points.push_back(b);
  }
  if (!hasCrossSection) {
    TrackPointDefinition a, b;
    a.kind = b.kind = TrackPointKind::CrossSection;
    a.curvature = b.curvature = clampSignedUnit(topLevelCurvature);
    a.tightness = b.tightness = 1.0;
    a.thickness = b.thickness = TrackCore::DEFAULT_CROSS_SECTION_THICKNESS;
    b.t = endT;
    path.points.push_back(a);
    path.points.push_back(b);
  }

  bool textureValid = false;
  if (raw.contains("texture")) {
    auto texture = normalizePathTexture(raw.at("texture"), textureValid);
    if (textureValid) path.texture = std::move(texture);
  }
  return path;
}

int textureTileCount(const TextureAssetDefinition& asset) {
  return (asset.width / asset.tileWidth) * (asset.height / asset.tileHeight);
}

void warn(std::vector<TrackWarning>& warnings, std::string code, std::string message, std::string id = {}) {
  warnings.push_back({std::move(code), std::move(message), std::move(id)});
}

std::optional<MeshAssetDefinition> normalizeMeshAsset(const std::string& id, const json& entry, std::vector<TrackWarning>& warnings) {
  if (!entry.is_object()) return std::nullopt;
  const json& mesh = entry.contains("mesh") && entry.at("mesh").is_object() ? entry.at("mesh") : entry;
  if (!mesh.contains("vertices") || !mesh.at("vertices").is_array() || !mesh.contains("polygons") || !mesh.at("polygons").is_array()) return std::nullopt;

  MeshAssetDefinition asset;
  asset.id = id;
  asset.name = stringOr(entry, "name", id);
  asset.railHeight = finiteOrMin(entry, "railHeight", TrackCore::DEFAULT_RAIL_HEIGHT, 0.0);
  try {
    for (const auto& raw : mesh.at("vertices")) {
      if (!raw.is_object() || !raw.contains("id") || !raw.at("id").is_number_integer() || !raw.contains("position") || !raw.at("position").is_object())
        throw std::runtime_error("invalid vertex record");
      const json& pos = raw.at("position");
      const double x = numberOr(pos, "x", 0.0), y = numberOr(pos, "y", 0.0);
      asset.vertices.push_back({raw.at("id").get<int>(), x, y});
    }
    if (mesh.contains("edges") && mesh.at("edges").is_array()) {
      for (const auto& raw : mesh.at("edges")) {
        if (!raw.is_object() || !raw.contains("id") || !raw.at("id").is_number_integer() || !raw.contains("vertices") ||
            !raw.at("vertices").is_array() || raw.at("vertices").size() != 2 || !raw.at("vertices")[0].is_number_integer() ||
            !raw.at("vertices")[1].is_number_integer())
          throw std::runtime_error("invalid edge record");
        bool rail = false;
        if (raw.contains("attributes") && raw.at("attributes").is_object() && raw.at("attributes").contains("rail"))
          rail = jsonTruthy(raw.at("attributes").at("rail"));
        asset.edges.push_back({raw.at("id").get<int>(), raw.at("vertices")[0].get<int>(), raw.at("vertices")[1].get<int>(), rail});
      }
    }
    for (const auto& raw : mesh.at("polygons")) {
      if (!raw.is_object() || !raw.contains("id") || !raw.at("id").is_number_integer() || !raw.contains("edges") || !raw.at("edges").is_array())
        throw std::runtime_error("invalid polygon record");
      MeshPolygonDefinition polygon;
      polygon.id = raw.at("id").get<int>();
      polygon.hole = raw.value("hole", false);
      for (const auto& directed : raw.at("edges")) {
        if (!directed.is_object() || !directed.contains("edge") || !directed.contains("v0") || !directed.contains("v1") ||
            !directed.at("edge").is_number_integer() || !directed.at("v0").is_number_integer() || !directed.at("v1").is_number_integer())
          throw std::runtime_error("invalid directed polygon edge");
        polygon.edges.push_back({directed.at("edge").get<int>(), directed.at("v0").get<int>(), directed.at("v1").get<int>()});
      }
      if (raw.contains("holes") && raw.at("holes").is_array())
        for (const auto& hole : raw.at("holes"))
          if (hole.is_number_integer()) polygon.holes.push_back(hole.get<int>());
      asset.polygons.push_back(std::move(polygon));
    }
    if (asset.vertices.empty() || asset.polygons.empty()) throw std::runtime_error("mesh contains no usable polygons");
  } catch (const std::exception& error) {
    warn(warnings, "mesh-asset-invalid", error.what(), id);
    return std::nullopt;
  }
  return asset;
}

ConnectionDefinition connection(const json& raw) {
  ConnectionDefinition result;
  result.id = stringOr(raw, "id");
  result.pointId = stringOr(raw, "pointId");
  result.kind = stringOr(raw, "kind");
  result.sourcePathId = stringOr(raw, "sourcePathId");
  result.sourceEnd = stringOr(raw, "sourceEnd");
  result.targetPathId = stringOr(raw, "targetPathId");
  result.targetEnd = stringOr(raw, "targetEnd");
  return result;
}

TrackDefinition normalize(const json& data, std::vector<TrackWarning>& warnings) {
  if (!data.is_object()) throw std::runtime_error("track JSON must be an object");
  if (!data.contains("version"))
    throw std::runtime_error("track version is required; only schema " + std::to_string(TrackCore::TRACK_SCHEMA_VERSION) + " is supported");
  if (!data.at("version").is_number_integer() || data.at("version").get<int>() != TrackCore::TRACK_SCHEMA_VERSION)
    throw std::runtime_error("unsupported track schema version; expected " + std::to_string(TrackCore::TRACK_SCHEMA_VERSION));
  if (!data.contains("paths") || !data.at("paths").is_array() || data.at("paths").empty())
    throw std::runtime_error("a current-schema track needs at least one path");

  TrackDefinition out;
  out.version = TrackCore::TRACK_SCHEMA_VERSION;
  const std::string name = stringOr(data, "name");
  out.name = name.empty() ? "Untitled Track" : name;
  if (data.contains("samples") && data.at("samples").is_number_integer() && data.at("samples").get<int>() != 0)
    out.samples = data.at("samples").get<int>();
  else
    out.samples = TrackCore::N_DEFAULT;

  const double topLevelCurvature = clampSignedUnit(numberOr(data, "crossSectionCurvature", 0.0));
  for (std::size_t i = 0; i < data.at("paths").size(); ++i) out.paths.push_back(normalizePath(data.at("paths")[i], i, topLevelCurvature));

  std::set<std::string> usedPathIds;
  for (std::size_t i = 0; i < out.paths.size(); ++i) {
    std::string id = out.paths[i].id.empty() ? "path" + std::to_string(i + 1) : out.paths[i].id;
    int suffix = 2;
    while (usedPathIds.count(id)) id = "path" + std::to_string(i + 1) + "-" + std::to_string(suffix++);
    out.paths[i].id = id;
    usedPathIds.insert(id);
  }

  std::unordered_map<std::string, TrackPointDefinition> pointsById;
  int nextPointId = 1;
  for (auto& path : out.paths) {
    for (auto& point : path.points) {
      if (point.kind != TrackPointKind::Position) continue;
      if (point.id.empty()) {
        do {
          point.id = "p" + std::to_string(nextPointId++);
        } while (pointsById.count(point.id));
      }
      const auto found = pointsById.find(point.id);
      if (found != pointsById.end())
        point = found->second;
      else
        pointsById.emplace(point.id, point);
    }
  }

  if (data.contains("textureAssets") && data.at("textureAssets").is_object()) {
    for (auto it = data.at("textureAssets").begin(); it != data.at("textureAssets").end(); ++it) {
      if (it.key().empty() || !it.value().is_object()) continue;
      const json& raw = it.value();
      TextureAssetDefinition asset;
      asset.id = it.key();
      asset.name = stringOr(raw, "name", asset.id);
      asset.path = stringOr(raw, "path");
      if (asset.path.empty()) continue;
      asset.width = std::max(1, static_cast<int>(std::floor(numberOr(raw, "width", 1.0))));
      asset.height = std::max(1, static_cast<int>(std::floor(numberOr(raw, "height", 1.0))));
      asset.tileWidth = std::max(1, std::min(asset.width, static_cast<int>(std::floor(numberOr(raw, "tileWidth", asset.width)))));
      asset.tileHeight = std::max(1, std::min(asset.height, static_cast<int>(std::floor(numberOr(raw, "tileHeight", asset.height)))));
      out.textureAssets.emplace(asset.id, std::move(asset));
    }
  }
  for (auto& path : out.paths) {
    if (!path.texture) continue;
    const auto asset = out.textureAssets.find(path.texture->assetId);
    if (asset == out.textureAssets.end() || path.texture->tile >= textureTileCount(asset->second)) path.texture.reset();
  }

  if (data.contains("meshAssets") && data.at("meshAssets").is_object()) {
    for (auto it = data.at("meshAssets").begin(); it != data.at("meshAssets").end(); ++it) {
      if (it.key().empty()) continue;
      auto asset = normalizeMeshAsset(it.key(), it.value(), warnings);
      if (asset) out.meshAssets.emplace(it.key(), std::move(*asset));
    }
  }
  if (data.contains("meshes") && data.at("meshes").is_array()) {
    std::size_t i = 0;
    for (const auto& raw : data.at("meshes")) {
      ++i;
      if (!raw.is_object() || !raw.contains("asset") || !raw.at("asset").is_string() || raw.at("asset").get_ref<const std::string&>().empty()) continue;
      MeshPlacementDefinition placement;
      placement.id = stringOr(raw, "id", "m" + std::to_string(i));
      if (placement.id.empty()) placement.id = "m" + std::to_string(i);
      placement.assetId = raw.at("asset").get<std::string>();
      placement.x = numberOr(raw, "x", 0.0);
      placement.z = numberOr(raw, "z", 0.0);
      placement.rotation = numberOr(raw, "rotation", 0.0);
      placement.elevation = numberOr(raw, "elevation", 0.0);
      if (!out.meshAssets.count(placement.assetId)) {
        warn(warnings, "mesh-placement-missing-asset", "mesh placement references a missing or invalid asset", placement.id);
        continue;
      }
      out.meshes.push_back(std::move(placement));
    }
  }

  std::set<std::string> meshIds;
  for (const auto& mesh : out.meshes) meshIds.insert(mesh.id);
  if (data.contains("zones") && data.at("zones").is_array()) {
    std::size_t i = 0;
    for (const auto& raw : data.at("zones")) {
      ++i;
      if (!raw.is_object() || !raw.contains("host") || !raw.at("host").is_object()) continue;
      const json& host = raw.at("host");
      ZoneDefinition zone;
      zone.id = stringOr(raw, "id", "z" + std::to_string(i));
      if (zone.id.empty()) zone.id = "z" + std::to_string(i);
      zone.effect = stringOr(raw, "effect") == "startGrid" ? "startGrid" : "velocityChange";
      zone.width = std::max(0.5, numberOr(raw, "width", 24.0));
      zone.length = std::max(0.5, numberOr(raw, "length", 40.0));
      if (stringOr(host, "kind") == "mesh") {
        zone.host.kind = "mesh";
        zone.host.meshId = stringOr(host, "meshId");
        if (zone.host.meshId.empty() || !meshIds.count(zone.host.meshId)) continue;
        zone.host.x = numberOr(host, "x", 0.0);
        zone.host.z = numberOr(host, "z", 0.0);
        zone.host.rotation = numberOr(host, "rotation", 0.0);
      } else {
        zone.host.kind = "path";
        zone.host.pathId = stringOr(host, "pathId");
        if (zone.host.pathId.empty() || !usedPathIds.count(zone.host.pathId)) continue;
        zone.host.t = clampCoerced(host, "t", 0.0, 1.0, 0.5);
        zone.host.lateral = numberOr(host, "lateral", 0.0);
      }
      if (zone.effect == "velocityChange") {
        zone.factor = clampCoerced(raw, "factor", 0.1, 5.0, 1.5);
        zone.duration = clampCoerced(raw, "duration", 0.1, 30.0, 2.0);
      }
      out.zones.push_back(std::move(zone));
    }
  }

  if (data.contains("triggers") && data.at("triggers").is_array()) {
    std::size_t i = 0;
    for (const auto& raw : data.at("triggers")) {
      ++i;
      if (!raw.is_object() || !raw.contains("host") || !raw.at("host").is_object()) continue;
      const json& host = raw.at("host");
      TriggerDefinition trigger;
      trigger.id = stringOr(raw, "id", "tr" + std::to_string(i));
      if (trigger.id.empty()) trigger.id = "tr" + std::to_string(i);
      trigger.type = stringOr(raw, "type") == "checkpoint" ? "checkpoint" : "dummy";
      trigger.direction = stringOr(raw, "direction");
      if (trigger.direction != "forward" && trigger.direction != "backward") trigger.direction = "both";
      trigger.width = std::max(0.5, numberOr(raw, "width", 40.0));
      trigger.height = std::max(0.5, numberOr(raw, "height", 12.0));
      trigger.rotation = numberOr(raw, "rotation", 0.0);
      if (trigger.type == "checkpoint") trigger.role = stringOr(raw, "role") == "finish" ? "finish" : "intermediate";
      if (stringOr(host, "kind") == "mesh") {
        trigger.host.kind = "mesh";
        trigger.host.meshId = stringOr(host, "meshId");
        if (trigger.host.meshId.empty() || !meshIds.count(trigger.host.meshId)) continue;
        trigger.host.x = numberOr(host, "x", 0.0);
        trigger.host.z = numberOr(host, "z", 0.0);
      } else {
        trigger.host.kind = "path";
        trigger.host.pathId = stringOr(host, "pathId");
        if (trigger.host.pathId.empty() || !usedPathIds.count(trigger.host.pathId)) continue;
        trigger.host.t = clampCoerced(host, "t", 0.0, 1.0, 0.5);
      }
      out.triggers.push_back(std::move(trigger));
    }
  }
  bool finishSeen = false;
  for (auto& trigger : out.triggers) {
    if (trigger.type != "checkpoint" || trigger.role != "finish") continue;
    if (finishSeen)
      trigger.role = "intermediate";
    else
      finishSeen = true;
  }

  auto loadConnections = [&](const char* key, std::vector<ConnectionDefinition>& target) {
    if (!data.contains(key) || !data.at(key).is_array()) return;
    for (const auto& raw : data.at(key))
      if (raw.is_object()) target.push_back(connection(raw));
  };
  loadConnections("disjointSeams", out.disjointSeams);
  loadConnections("junctions", out.junctions);

  if (data.contains("selfIntersectionOverrides") && data.at("selfIntersectionOverrides").is_array()) {
    for (const auto& raw : data.at("selfIntersectionOverrides")) {
      const std::string a = stringOr(raw, "a"), b = stringOr(raw, "b");
      if (a.empty() || b.empty()) continue;
      out.selfIntersectionOverrides.push_back({stringOr(raw, "side") == "right" ? "right" : "left", a, b,
                                               stringOr(raw, "action") == "collapse" ? "collapse" : "keep"});
    }
  }

  const json empty = json::object();
  const json& handling = data.contains("handling") && data.at("handling").is_object() ? data.at("handling") : empty;
  out.handling.maxSpeed = clampCoerced(handling, "maxSpeed", 10.0, 1000.0, 140.0);
  out.handling.accel = clampCoerced(handling, "accel", 5.0, 1000.0, 71.0);
  out.handling.turnSpeed = clampCoerced(handling, "turnSpeed", 10.0, 720.0, 137.5);
  out.handling.weight = clampCoerced(handling, "weight", 50.0, 100000.0, 1000.0);

  const json& start = data.contains("start") && data.at("start").is_object() ? data.at("start") : empty;
  int startPath = start.contains("path") && start.at("path").is_number_integer() ? start.at("path").get<int>() : 0;
  startPath = std::max(0, std::min(static_cast<int>(out.paths.size()) - 1, startPath));
  const int positionCount = static_cast<int>(std::count_if(out.paths[startPath].points.begin(), out.paths[startPath].points.end(),
                                                           [](const auto& p) { return p.kind == TrackPointKind::Position; }));
  int startPoint = start.contains("point") && start.at("point").is_number_integer() ? start.at("point").get<int>() : 0;
  out.start.path = startPath;
  out.start.point = std::max(0, std::min(positionCount - 1, startPoint));
  out.start.reverse = start.contains("reverse") && jsonTruthy(start.at("reverse"));
  return out;
}

}  // namespace

TrackLoadResult Track::fromJson(std::string_view text) {
  TrackLoadResult result;
  try {
    const json data = json::parse(text.begin(), text.end());
    Track track;
    track.definition = normalize(data, result.warnings);
    result.track = std::move(track);
  } catch (const std::exception& error) {
    result.error = error.what();
    result.track.reset();
  }
  return result;
}

TrackLoadResult Track::fromFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return {std::nullopt, {}, "cannot open track file: " + path.string()};
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return fromJson(buffer.str());
}

}  // namespace tox
