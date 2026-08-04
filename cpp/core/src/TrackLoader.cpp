// TrackLoader.cpp — strict current-schema JSON loading and runtime-subset
// normalization. Older schema versions (below TRACK_SCHEMA_VERSION_MIN_SUPPORTED) are not
// migrated; only schema 10/11 are accepted.
#include "Track.hpp"
#include "TrackBake.hpp"
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

// Parses a reservation end cap (CENTRAL_RESERVATION_PLAN.md). Missing/malformed input -- including
// every pre-existing authored track, which never had this field -- defaults to Joined, width 0:
// byte-for-byte the taper-to-a-point behavior reservations always had before end caps existed.
ReservationEndCap parseEndCap(const json& source, const char* key) {
  ReservationEndCap cap;
  if (!source.is_object() || !source.contains(key) || !source.at(key).is_object()) return cap;
  const json& raw = source.at(key);
  const std::string style = stringOr(raw, "style");
  if (style == "mitred")
    cap.style = ReservationEndCapStyle::Mitred;
  else if (style == "rounded")
    cap.style = ReservationEndCapStyle::Rounded;
  else
    cap.style = ReservationEndCapStyle::Joined;
  cap.width = std::max(0.0, numberOr(raw, "width", 0.0));
  cap.noseLength = std::max(0.0, numberOr(raw, "noseLength", 0.0));
  return cap;
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
      point.centerOffsetPercent = clampNumber(numberOr(source, "centerOffsetPercent", 0.0), -50.0, 50.0);
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
  path.material = stringOr(raw, "material");

  // Central reservations (CENTRAL_RESERVATION_PLAN.md): defensively clamp rather than reject, same
  // policy as every other authored field here. t0 < t1 within [0,1]; degenerate/zero-width entries
  // are dropped rather than baked into a zero-width no-op gap. Overlap between reservations on the
  // same path is the editor's job to prevent at author time (EditorState); the loader tolerates it.
  if (raw.contains("reservations") && raw.at("reservations").is_array()) {
    for (const auto& source : raw.at("reservations")) {
      if (!source.is_object()) continue;
      ReservationDefinition reservation;
      reservation.id = stringOr(source, "id");
      double t0 = clampNumber(numberOr(source, "t0", 0.0), 0.0, 1.0);
      double t1 = clampNumber(numberOr(source, "t1", 0.0), 0.0, 1.0);
      if (t0 > t1) std::swap(t0, t1);
      reservation.widthMode = stringOr(source, "widthMode") == "percent" ? ReservationWidthMode::Percent : ReservationWidthMode::Fixed;
      reservation.width = std::max(0.0, numberOr(source, "width", 0.0));
      if (reservation.widthMode == ReservationWidthMode::Percent) reservation.width = std::min(reservation.width, 100.0);
      if (t1 - t0 <= 1e-6 || reservation.width <= 1e-6) continue;
      reservation.t0 = t0;
      reservation.t1 = t1;
      reservation.endCap0 = parseEndCap(source, "endCap0");
      reservation.endCap1 = parseEndCap(source, "endCap1");
      // End-cap width is always metres, so it's only clamped against the reservation's own peak
      // when that peak is also metres (Fixed) -- in Percent mode `width` is a 0-100 number, not
      // comparable, and the bake clamps the cap against the actual local peak width instead (see
      // TrackBake.cpp's reservationHalfGapAt).
      if (reservation.widthMode == ReservationWidthMode::Fixed) {
        reservation.endCap0.width = std::min(reservation.endCap0.width, reservation.width);
        reservation.endCap1.width = std::min(reservation.endCap1.width, reservation.width);
      }
      path.reservations.push_back(std::move(reservation));
    }
  }
  return path;
}

int textureTileCount(const TextureAssetDefinition& asset) {
  return (asset.width / asset.tileWidth) * (asset.height / asset.tileHeight);
}

void warn(std::vector<TrackWarning>& warnings, std::string code, std::string message, std::string id = {}) {
  warnings.push_back({std::move(code), std::move(message), std::move(id)});
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
    throw std::runtime_error("track version is required; only schema " + std::to_string(TrackCore::TRACK_SCHEMA_VERSION_MIN_SUPPORTED) +
                             "-" + std::to_string(TrackCore::TRACK_SCHEMA_VERSION) + " is supported");
  if (!data.at("version").is_number_integer() || data.at("version").get<int>() < TrackCore::TRACK_SCHEMA_VERSION_MIN_SUPPORTED ||
      data.at("version").get<int>() > TrackCore::TRACK_SCHEMA_VERSION)
    throw std::runtime_error("unsupported track schema version; expected " + std::to_string(TrackCore::TRACK_SCHEMA_VERSION_MIN_SUPPORTED) +
                             "-" + std::to_string(TrackCore::TRACK_SCHEMA_VERSION));
  if (!data.contains("paths") || !data.at("paths").is_array() || data.at("paths").empty())
    throw std::runtime_error("a current-schema track needs at least one path");
  // Hard break, no migration (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 2): Mesh regions (placed
  // mesh assets, authored via `meshAssets`/`meshes`) were removed entirely, not silently dropped --
  // a track that still references them fails to load with an explicit, actionable error rather
  // than parsing as if the meshes had simply been deleted.
  if ((data.contains("meshAssets") && !data.at("meshAssets").empty()) ||
      (data.contains("meshes") && !data.at("meshes").empty()))
    throw std::runtime_error(
        "this track uses Mesh regions (meshAssets/meshes), a feature removed in schema " +
        std::to_string(TrackCore::TRACK_SCHEMA_VERSION) +
        "; re-author it without placed mesh assets, or use a track saved before this feature was removed");

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

  // Drivable mesh object placements (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 3): pure authored
  // data, carried through unchanged -- core never loads or compiles the referenced `.mppmodel`
  // (see the plan's "`.mppmodel` loading is host-only" architecture note).
  if (data.contains("meshObjects") && data.at("meshObjects").is_array()) {
    std::size_t i = 0;
    for (const auto& raw : data.at("meshObjects")) {
      ++i;
      if (!raw.is_object()) continue;
      ModelPlacementDefinition placement;
      placement.id = stringOr(raw, "id", "mo" + std::to_string(i));
      if (placement.id.empty()) placement.id = "mo" + std::to_string(i);
      placement.modelId = stringOr(raw, "modelId");
      if (placement.modelId.empty()) continue;
      placement.position = Vec3(numberOr(raw, "x", 0.0), numberOr(raw, "y", 0.0), numberOr(raw, "z", 0.0));
      placement.rotation =
          Vec3(numberOr(raw, "yaw", 0.0), numberOr(raw, "pitch", 0.0), numberOr(raw, "roll", 0.0));
      placement.scale = Vec3(std::max(1e-6, numberOr(raw, "scaleX", 1.0)), std::max(1e-6, numberOr(raw, "scaleY", 1.0)),
                             std::max(1e-6, numberOr(raw, "scaleZ", 1.0)));
      out.meshObjects.push_back(std::move(placement));
    }
  }
  std::set<std::string> usedMeshObjectIds;
  for (const auto& placement : out.meshObjects) usedMeshObjectIds.insert(placement.id);

  if (data.contains("zones") && data.at("zones").is_array()) {
    std::size_t i = 0;
    for (const auto& raw : data.at("zones")) {
      ++i;
      if (!raw.is_object() || !raw.contains("host") || !raw.at("host").is_object()) continue;
      const json& host = raw.at("host");
      ZoneDefinition zone;
      zone.id = stringOr(raw, "id", "z" + std::to_string(i));
      if (zone.id.empty()) zone.id = "z" + std::to_string(i);
      const std::string effect = stringOr(raw, "effect");
      zone.effect = effect == "startGrid" ? "startGrid" : effect == "jump" ? "jump" : "velocityChange";
      zone.width = std::max(0.5, numberOr(raw, "width", 24.0));
      zone.length = std::max(0.5, numberOr(raw, "length", 40.0));
      // "meshObject" (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 3.5) restores the host-surface
      // capability the old Mesh-region host provided; every other value normalizes to "path".
      if (stringOr(host, "kind") == "meshObject") {
        zone.host.kind = "meshObject";
        zone.host.meshObjectId = stringOr(host, "meshObjectId");
        if (zone.host.meshObjectId.empty() || !usedMeshObjectIds.count(zone.host.meshObjectId)) continue;
        const json& local = host.contains("localPosition") && host.at("localPosition").is_object() ? host.at("localPosition") : json::object();
        zone.host.localPosition = Vec3(numberOr(local, "x", 0.0), numberOr(local, "y", 0.0), numberOr(local, "z", 0.0));
        zone.host.localYaw = numberOr(host, "localYaw", 0.0);
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
      // Same path/meshObject split as zones above.
      if (stringOr(host, "kind") == "meshObject") {
        trigger.host.kind = "meshObject";
        trigger.host.meshObjectId = stringOr(host, "meshObjectId");
        if (trigger.host.meshObjectId.empty() || !usedMeshObjectIds.count(trigger.host.meshObjectId)) continue;
        const json& local = host.contains("localPosition") && host.at("localPosition").is_object() ? host.at("localPosition") : json::object();
        trigger.host.localPosition = Vec3(numberOr(local, "x", 0.0), numberOr(local, "y", 0.0), numberOr(local, "z", 0.0));
      } else {
        trigger.host.kind = "path";
        trigger.host.pathId = stringOr(host, "pathId");
        if (trigger.host.pathId.empty() || !usedPathIds.count(trigger.host.pathId)) continue;
        trigger.host.t = clampCoerced(host, "t", 0.0, 1.0, 0.5);
        trigger.host.lateral = numberOr(host, "lateral", 0.0);
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

TrackLoadResult Track::fromJson(std::string_view text, bool detectSelfIntersections) {
  TrackLoadResult result;
  try {
    const json data = json::parse(text.begin(), text.end());
    Track track;
    track.definition = normalize(data, result.warnings);
    if (!bakeTrack(track, result.warnings, result.error, detectSelfIntersections)) return result;
    result.track = std::move(track);
  } catch (const std::exception& error) {
    result.error = error.what();
    result.track.reset();
  }
  return result;
}

TrackLoadResult Track::fromFile(const std::filesystem::path& path, bool detectSelfIntersections) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return {std::nullopt, {}, "cannot open track file: " + path.string()};
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return fromJson(buffer.str(), detectSelfIntersections);
}

namespace {

// Prefixes every id an authored TrackDefinition owns, plus every same-source reference to one of
// those ids, with "<index>:" -- so two sources that each independently reused e.g. "path1"/"p1"
// merge without collision (TRACK_MODEL_LIST_PLAN.md Milestone 1.2). `ModelPlacementDefinition::modelId`
// is deliberately NOT touched: it names an embedded `<Model id>` in the enclosing Track resource's
// `<Models>` list (an outer-XML-only concept `core` never resolves), not an id this TrackDefinition
// owns -- unlike `meshObjects[].id` itself (the placement's OWN id, referenced by
// zones/triggers[].host.meshObjectId), which is this source's to own and therefore is namespaced.
void namespaceIds(TrackDefinition& def, const std::string& prefix) {
  if (prefix.empty()) return;

  for (auto& path : def.paths) {
    path.id = prefix + path.id;
    for (auto& point : path.points)
      if (!point.id.empty()) point.id = prefix + point.id;
    for (auto& reservation : path.reservations) reservation.id = prefix + reservation.id;
  }

  std::map<std::string, TextureAssetDefinition> renamedAssets;
  for (auto& [id, asset] : def.textureAssets) {
    asset.id = prefix + asset.id;
    renamedAssets.emplace(asset.id, std::move(asset));
  }
  def.textureAssets = std::move(renamedAssets);
  for (auto& path : def.paths)
    if (path.texture) path.texture->assetId = prefix + path.texture->assetId;

  for (auto& placement : def.meshObjects) placement.id = prefix + placement.id;

  for (auto& zone : def.zones) {
    zone.id = prefix + zone.id;
    if (zone.host.kind == "meshObject")
      zone.host.meshObjectId = prefix + zone.host.meshObjectId;
    else
      zone.host.pathId = prefix + zone.host.pathId;
  }

  for (auto& trigger : def.triggers) {
    trigger.id = prefix + trigger.id;
    if (trigger.host.kind == "meshObject")
      trigger.host.meshObjectId = prefix + trigger.host.meshObjectId;
    else
      trigger.host.pathId = prefix + trigger.host.pathId;
  }

  auto namespaceConnection = [&](ConnectionDefinition& c) {
    c.id = prefix + c.id;
    if (!c.pointId.empty()) c.pointId = prefix + c.pointId;
    if (!c.sourcePathId.empty()) c.sourcePathId = prefix + c.sourcePathId;
    if (!c.targetPathId.empty()) c.targetPathId = prefix + c.targetPathId;
  };
  for (auto& c : def.disjointSeams) namespaceConnection(c);
  for (auto& c : def.junctions) namespaceConnection(c);

  for (auto& o : def.selfIntersectionOverrides) {
    if (!o.a.empty()) o.a = prefix + o.a;
    if (!o.b.empty()) o.b = prefix + o.b;
  }
}

// Concatenates N already-normalized TrackDefinitions' list fields into one, after namespaceIds()
// above has made every source's ids collision-free. Singular/global fields (name/samples/handling/
// start/version) come from the first source only -- there is exactly one of each in a merged Track,
// not one per source, and "first source wins" mirrors the "primary Track-type model" the Track
// resource's own `<Models>` list designates (TRACK_MODEL_LIST_PLAN.md).
TrackDefinition mergeTrackDefinitions(std::vector<TrackDefinition> defs) {
  for (std::size_t i = 0; i < defs.size(); ++i)
    if (defs.size() > 1) namespaceIds(defs[i], std::to_string(i) + ":");

  TrackDefinition merged = std::move(defs.front());
  for (std::size_t i = 1; i < defs.size(); ++i) {
    TrackDefinition& src = defs[i];
    merged.paths.insert(merged.paths.end(), std::make_move_iterator(src.paths.begin()), std::make_move_iterator(src.paths.end()));
    merged.textureAssets.insert(std::make_move_iterator(src.textureAssets.begin()), std::make_move_iterator(src.textureAssets.end()));
    merged.meshObjects.insert(merged.meshObjects.end(), std::make_move_iterator(src.meshObjects.begin()), std::make_move_iterator(src.meshObjects.end()));
    merged.zones.insert(merged.zones.end(), std::make_move_iterator(src.zones.begin()), std::make_move_iterator(src.zones.end()));
    merged.triggers.insert(merged.triggers.end(), std::make_move_iterator(src.triggers.begin()), std::make_move_iterator(src.triggers.end()));
    merged.disjointSeams.insert(merged.disjointSeams.end(), std::make_move_iterator(src.disjointSeams.begin()), std::make_move_iterator(src.disjointSeams.end()));
    merged.junctions.insert(merged.junctions.end(), std::make_move_iterator(src.junctions.begin()), std::make_move_iterator(src.junctions.end()));
    merged.selfIntersectionOverrides.insert(merged.selfIntersectionOverrides.end(), std::make_move_iterator(src.selfIntersectionOverrides.begin()),
                                             std::make_move_iterator(src.selfIntersectionOverrides.end()));
  }
  return merged;
}

}  // namespace

TrackLoadResult Track::fromTrackDataFiles(const std::vector<std::filesystem::path>& paths, bool detectSelfIntersections) {
  if (paths.empty()) return {std::nullopt, {}, "fromTrackDataFiles requires at least one TrackData path"};

  std::vector<TrackDefinition> defs;
  std::vector<TrackWarning> warnings;
  for (const auto& path : paths) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {std::nullopt, {}, "cannot open track file: " + path.string()};
    std::ostringstream buffer;
    buffer << input.rdbuf();
    try {
      const json data = json::parse(buffer.str());
      defs.push_back(normalize(data, warnings));
    } catch (const std::exception& error) {
      return {std::nullopt, {}, "failed to load '" + path.string() + "': " + error.what()};
    }
  }

  TrackLoadResult result;
  result.warnings = std::move(warnings);
  Track track;
  track.definition = mergeTrackDefinitions(std::move(defs));
  if (!bakeTrack(track, result.warnings, result.error, detectSelfIntersections)) return result;
  result.track = std::move(track);
  return result;
}

}  // namespace tox
