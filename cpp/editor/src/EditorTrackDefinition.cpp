// EditorTrackDefinition.cpp — schema-10 JSON <-> editor::TrackDefinition, independent of
// cpp/core's tox::Track loader (see EditorTrackDefinition.hpp for why). JSON field names and
// defaults are kept in lockstep with cpp/core/src/TrackLoader.cpp's normalize() -- that is the
// source of truth for what schema 10 looks like; diverge from it only where noted (the mid-edit
// tolerance described above).
#include "EditorTrackDefinition.hpp"

#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

#include "nlohmann/json.hpp"

namespace editor {
namespace {

using nlohmann::json;

// 12: Mesh regions (meshAssets/meshes, mesh-hosted zone/trigger host) removed
// (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 2) -- mirrors TrackCore::TRACK_SCHEMA_VERSION.
constexpr int kSchemaVersion = 12;
// Oldest schema version still readable (no reservations field, always empty) -- mirrors
// TrackCore::TRACK_SCHEMA_VERSION_MIN_SUPPORTED (CENTRAL_RESERVATION_PLAN.md M0). The editor always
// writes kSchemaVersion; reading stays lenient across the version gap.
constexpr int kSchemaVersionMinSupported = 10;

bool finite(double value) { return std::isfinite(value); }
double clampNumber(double value, double lo, double hi) { return std::max(lo, std::min(hi, value)); }
double clampSignedUnit(double value) { return finite(value) ? clampNumber(value, -1.0, 1.0) : 0.0; }
double clampTightness(double value) { return finite(value) ? clampNumber(value, 0.2, 4.0) : 1.0; }
double clampThickness(double value) { return finite(value) ? std::max(0.0, value) : 4.0; }

double numberOr(const json& object, const char* key, double fallback) {
  if (!object.is_object() || !object.contains(key) || !object.at(key).is_number()) return fallback;
  const double value = object.at(key).get<double>();
  return finite(value) ? value : fallback;
}

double clampCoerced(const json& object, const char* key, double lo, double hi, double fallback) {
  return clampNumber(numberOr(object, key, fallback), lo, hi);
}

std::string stringOr(const json& object, const char* key, const std::string& fallback = {}) {
  return object.is_object() && object.contains(key) && object.at(key).is_string() ? object.at(key).get<std::string>() : fallback;
}

// Mirrors core's TrackLoader.cpp parseEndCap: missing/malformed input defaults to Joined, width 0.
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

const char* endCapStyleName(ReservationEndCapStyle style) {
  switch (style) {
    case ReservationEndCapStyle::Mitred:
      return "mitred";
    case ReservationEndCapStyle::Rounded:
      return "rounded";
    default:
      return "joined";
  }
}

bool jsonTruthy(const json& value) {
  if (value.is_null()) return false;
  if (value.is_boolean()) return value.get<bool>();
  if (value.is_number()) return value.get<double>() != 0.0;
  if (value.is_string()) return !value.get_ref<const std::string&>().empty();
  return true;
}

std::optional<TextureBinding> normalizePathTexture(const json& raw) {
  if (!raw.is_object() || !raw.contains("asset") || !raw.at("asset").is_string() || raw.at("asset").get_ref<const std::string&>().empty())
    return std::nullopt;
  TextureBinding binding;
  binding.assetId = raw.at("asset").get<std::string>();
  binding.tile = raw.contains("tile") && raw.at("tile").is_number_integer() && raw.at("tile").get<int>() >= 0 ? raw.at("tile").get<int>() : 0;
  return binding;
}

// Unlike core's normalizePosition, a position with a malformed/missing "pos" is kept (as the
// origin) rather than rejecting the whole path load — a point mid-drag or freshly added in the
// editor may briefly be incomplete.
TrackPoint normalizePosition(const json& raw) {
  TrackPoint point;
  point.kind = PointKind::Position;
  point.id = stringOr(raw, "id");
  if (raw.is_object() && raw.contains("pos") && raw.at("pos").is_array() && raw.at("pos").size() == 3) {
    const json& pos = raw.at("pos");
    const double x = pos[0].is_number() ? pos[0].get<double>() : 0.0;
    const double y = pos[1].is_number() ? pos[1].get<double>() : 0.0;
    const double z = pos[2].is_number() ? pos[2].get<double>() : 0.0;
    point.pos = tox::Vec3(finite(x) ? x : 0.0, finite(y) ? y : 0.0, finite(z) ? z : 0.0);
  }
  point.weight = std::max(0.01, numberOr(raw, "weight", 1.0));
  return point;
}

// Unlike core's normalizePath, does NOT require >=4 position points and does NOT synthesize
// missing roll/width/crossSection endpoints -- an in-progress path is stored exactly as authored.
Path normalizePath(const json& raw, double topLevelCurvature) {
  Path path;
  path.id = stringOr(raw, "id");
  path.closed = !(raw.is_object() && raw.contains("closed") && raw.at("closed").is_boolean() && !raw.at("closed").get<bool>());
  if (raw.is_object() && raw.contains("points") && raw.at("points").is_array()) {
    for (const auto& source : raw.at("points")) {
      const std::string type = stringOr(source, "type");
      TrackPoint point;
      if (type == "roll") {
        point.kind = PointKind::Roll;
        point.t = clampNumber(numberOr(source, "t", 0.0), 0.0, 1.0);
        point.roll = clampNumber(numberOr(source, "roll", 0.0), -180.0, 180.0);
      } else if (type == "width") {
        point.kind = PointKind::Width;
        point.t = clampNumber(numberOr(source, "t", 0.0), 0.0, 1.0);
        point.width = std::max(1.0, numberOr(source, "width", 36.0));
        point.centerOffsetPercent = clampNumber(numberOr(source, "centerOffsetPercent", 0.0), -50.0, 50.0);
      } else if (type == "crossSection") {
        point.kind = PointKind::CrossSection;
        point.t = clampNumber(numberOr(source, "t", 0.0), 0.0, 1.0);
        point.curvature = clampSignedUnit(numberOr(source, "curvature", topLevelCurvature));
        point.tightness = clampTightness(numberOr(source, "tightness", 1.0));
        point.thickness = clampThickness(numberOr(source, "thickness", 4.0));
      } else {
        point = normalizePosition(source);
      }
      path.points.push_back(std::move(point));
    }
  }
  if (raw.is_object() && raw.contains("texture")) path.texture = normalizePathTexture(raw.at("texture"));
  path.material = stringOr(raw, "material");

  // Mirrors core's TrackLoader.cpp normalizePath: clamp defensively rather than reject, matching
  // this function's own "in-progress state, never fails" posture. Unlike core, does not drop
  // degenerate entries -- an editor mid-edit reservation (e.g. width not typed in yet) is kept as
  // authored so the panel doesn't lose it out from under the user.
  if (raw.is_object() && raw.contains("reservations") && raw.at("reservations").is_array()) {
    for (const auto& source : raw.at("reservations")) {
      if (!source.is_object()) continue;
      Reservation reservation;
      reservation.id = stringOr(source, "id");
      reservation.t0 = clampNumber(numberOr(source, "t0", 0.0), 0.0, 1.0);
      reservation.t1 = clampNumber(numberOr(source, "t1", 0.0), 0.0, 1.0);
      reservation.widthMode = stringOr(source, "widthMode") == "percent" ? ReservationWidthMode::Percent : ReservationWidthMode::Fixed;
      reservation.width = std::max(0.0, numberOr(source, "width", 0.0));
      reservation.endCap0 = parseEndCap(source, "endCap0");
      reservation.endCap1 = parseEndCap(source, "endCap1");
      path.reservations.push_back(std::move(reservation));
    }
  }
  return path;
}

Connection normalizeConnection(const json& raw) {
  Connection result;
  result.id = stringOr(raw, "id");
  result.pointId = stringOr(raw, "pointId");
  result.kind = stringOr(raw, "kind");
  result.sourcePathId = stringOr(raw, "sourcePathId");
  result.sourceEnd = stringOr(raw, "sourceEnd");
  result.targetPathId = stringOr(raw, "targetPathId");
  result.targetEnd = stringOr(raw, "targetEnd");
  result.pathId = stringOr(raw, "pathId");
  result.leftPathId = stringOr(raw, "leftPathId");
  result.rightPathId = stringOr(raw, "rightPathId");
  return result;
}

TrackDefinition normalize(const json& data) {
  if (!data.is_object()) throw std::runtime_error("track JSON must be an object");
  if (data.contains("version") && data.at("version").is_number_integer() &&
      (data.at("version").get<int>() < kSchemaVersionMinSupported || data.at("version").get<int>() > kSchemaVersion))
    throw std::runtime_error("unsupported track schema version; the editor only reads schema " + std::to_string(kSchemaVersionMinSupported) +
                             "-" + std::to_string(kSchemaVersion) + " and writes schema " + std::to_string(kSchemaVersion));
  // Hard break, no migration (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 2, mirrors
  // TrackLoader.cpp's core-side check): Mesh regions were removed entirely, not silently dropped.
  if ((data.contains("meshAssets") && !data.at("meshAssets").empty()) ||
      (data.contains("meshes") && !data.at("meshes").empty()))
    throw std::runtime_error(
        "this track uses Mesh regions (meshAssets/meshes), a feature removed in schema " +
        std::to_string(kSchemaVersion) +
        "; re-author it without placed mesh assets, or use a track saved before this feature was removed");

  TrackDefinition out;
  out.version = kSchemaVersion;
  const std::string name = stringOr(data, "name");
  out.name = name.empty() ? "Untitled Track" : name;
  out.samples = data.contains("samples") && data.at("samples").is_number_integer() && data.at("samples").get<int>() != 0
                    ? data.at("samples").get<int>()
                    : 400;

  const double topLevelCurvature = clampSignedUnit(numberOr(data, "crossSectionCurvature", 0.0));
  if (data.contains("paths") && data.at("paths").is_array())
    for (const auto& raw : data.at("paths")) out.paths.push_back(normalizePath(raw, topLevelCurvature));

  backfillPointIds(out);

  if (data.contains("textureAssets") && data.at("textureAssets").is_object()) {
    for (auto it = data.at("textureAssets").begin(); it != data.at("textureAssets").end(); ++it) {
      if (it.key().empty() || !it.value().is_object()) continue;
      const json& raw = it.value();
      TextureAsset asset;
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

  if (data.contains("zones") && data.at("zones").is_array()) {
    std::size_t i = 0;
    for (const auto& raw : data.at("zones")) {
      ++i;
      if (!raw.is_object()) continue;
      const json empty = json::object();
      const json& host = raw.contains("host") && raw.at("host").is_object() ? raw.at("host") : empty;
      Zone zone;
      zone.id = stringOr(raw, "id", "z" + std::to_string(i));
      if (zone.id.empty()) zone.id = "z" + std::to_string(i);
      const std::string effect = stringOr(raw, "effect");
      zone.effect = effect == "startGrid" ? "startGrid" : effect == "jump" ? "jump" : "velocityChange";
      zone.width = std::max(0.5, numberOr(raw, "width", 24.0));
      zone.length = std::max(0.5, numberOr(raw, "length", 40.0));
      // Mesh-hosted zones were removed along with MeshRegion (DRIVABLE_MESH_OBJECTS_PLAN.md
      // Milestone 2); every zone is path-hosted now.
      zone.host.kind = "path";
      zone.host.pathId = stringOr(host, "pathId");
      zone.host.t = clampCoerced(host, "t", 0.0, 1.0, 0.5);
      zone.host.lateral = numberOr(host, "lateral", 0.0);
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
      if (!raw.is_object()) continue;
      const json empty = json::object();
      const json& host = raw.contains("host") && raw.at("host").is_object() ? raw.at("host") : empty;
      Trigger trigger;
      trigger.id = stringOr(raw, "id", "tr" + std::to_string(i));
      if (trigger.id.empty()) trigger.id = "tr" + std::to_string(i);
      trigger.type = stringOr(raw, "type") == "checkpoint" ? "checkpoint" : "dummy";
      trigger.direction = stringOr(raw, "direction");
      if (trigger.direction != "forward" && trigger.direction != "backward") trigger.direction = "both";
      trigger.width = std::max(0.5, numberOr(raw, "width", 40.0));
      trigger.height = std::max(0.5, numberOr(raw, "height", 12.0));
      trigger.rotation = numberOr(raw, "rotation", 0.0);
      trigger.autoWidth = raw.contains("autoWidth") && jsonTruthy(raw.at("autoWidth"));
      if (trigger.type == "checkpoint") trigger.role = stringOr(raw, "role") == "finish" ? "finish" : "intermediate";
      // Same "always path-hosted now" note as zones above.
      trigger.host.kind = "path";
      trigger.host.pathId = stringOr(host, "pathId");
      trigger.host.t = clampCoerced(host, "t", 0.0, 1.0, 0.5);
      trigger.host.lateral = numberOr(host, "lateral", 0.0);
      out.triggers.push_back(std::move(trigger));
    }
  }

  auto loadConnections = [&](const char* key, std::vector<Connection>& target) {
    if (!data.contains(key) || !data.at(key).is_array()) return;
    for (const auto& raw : data.at(key))
      if (raw.is_object()) target.push_back(normalizeConnection(raw));
  };
  loadConnections("disjointSeams", out.disjointSeams);
  loadConnections("junctions", out.junctions);

  if (data.contains("selfIntersectionOverrides") && data.at("selfIntersectionOverrides").is_array()) {
    for (const auto& raw : data.at("selfIntersectionOverrides")) {
      const std::string a = stringOr(raw, "a"), b = stringOr(raw, "b");
      if (a.empty() || b.empty()) continue;
      out.selfIntersectionOverrides.push_back(
          {stringOr(raw, "side") == "right" ? "right" : "left", a, b, stringOr(raw, "action") == "collapse" ? "collapse" : "keep"});
    }
  }

  const json empty = json::object();
  const json& handling = data.contains("handling") && data.at("handling").is_object() ? data.at("handling") : empty;
  out.handling.maxSpeed = clampCoerced(handling, "maxSpeed", 10.0, 1000.0, 140.0);
  out.handling.accel = clampCoerced(handling, "accel", 5.0, 1000.0, 71.0);
  out.handling.turnSpeed = clampCoerced(handling, "turnSpeed", 10.0, 720.0, 137.5);
  out.handling.weight = clampCoerced(handling, "weight", 50.0, 100000.0, 1000.0);

  const json& start = data.contains("start") && data.at("start").is_object() ? data.at("start") : empty;
  out.start.path = start.contains("path") && start.at("path").is_number_integer() ? start.at("path").get<int>() : 0;
  out.start.point = start.contains("point") && start.at("point").is_number_integer() ? start.at("point").get<int>() : 0;
  out.start.reverse = start.contains("reverse") && jsonTruthy(start.at("reverse"));

  return out;
}

json pointToJson(const TrackPoint& point) {
  switch (point.kind) {
    case PointKind::Roll:
      return json{{"type", "roll"}, {"t", point.t}, {"roll", point.roll}};
    case PointKind::Width: {
      json out = {{"type", "width"}, {"t", point.t}, {"width", point.width}};
      if (point.centerOffsetPercent != 0.0) out["centerOffsetPercent"] = point.centerOffsetPercent;
      return out;
    }
    case PointKind::CrossSection:
      return json{{"type", "crossSection"}, {"t", point.t}, {"curvature", point.curvature}, {"tightness", point.tightness}, {"thickness", point.thickness}};
    case PointKind::Position:
    default:
      return json{{"type", "position"}, {"id", point.id}, {"pos", json::array({point.pos.x, point.pos.y, point.pos.z})}, {"weight", point.weight}};
  }
}

json pathToJson(const Path& path) {
  json points = json::array();
  for (const auto& point : path.points) points.push_back(pointToJson(point));
  json out = {{"id", path.id}, {"closed", path.closed}, {"points", std::move(points)}};
  if (path.texture) out["texture"] = json{{"asset", path.texture->assetId}, {"tile", path.texture->tile}};
  if (!path.material.empty()) out["material"] = path.material;
  if (!path.reservations.empty()) {
    json reservations = json::array();
    for (const auto& reservation : path.reservations) {
      json entry = {{"id", reservation.id}, {"t0", reservation.t0}, {"t1", reservation.t1}, {"width", reservation.width}};
      // Omitted (rather than always writing "fixed") when Fixed, matching the loader's default and
      // every pre-existing reservation's file shape.
      if (reservation.widthMode == ReservationWidthMode::Percent) entry["widthMode"] = "percent";
      // Omitted (rather than always writing style "joined") when Joined, matching the loader's
      // parseEndCap default and every pre-existing reservation's file shape.
      auto endCapJson = [](const ReservationEndCap& cap) {
        return json{{"style", endCapStyleName(cap.style)}, {"width", cap.width}, {"noseLength", cap.noseLength}};
      };
      if (reservation.endCap0.style != ReservationEndCapStyle::Joined) entry["endCap0"] = endCapJson(reservation.endCap0);
      if (reservation.endCap1.style != ReservationEndCapStyle::Joined) entry["endCap1"] = endCapJson(reservation.endCap1);
      reservations.push_back(std::move(entry));
    }
    out["reservations"] = std::move(reservations);
  }
  return out;
}

json zoneToJson(const Zone& zone) {
  json host = json{{"kind", "path"}, {"pathId", zone.host.pathId}, {"t", zone.host.t}, {"lateral", zone.host.lateral}};
  json out = {{"id", zone.id}, {"effect", zone.effect}, {"width", zone.width}, {"length", zone.length}, {"host", std::move(host)}};
  if (zone.effect == "velocityChange") {
    out["factor"] = zone.factor;
    out["duration"] = zone.duration;
  }
  return out;
}

json triggerToJson(const Trigger& trigger) {
  json host = json{{"kind", "path"}, {"pathId", trigger.host.pathId}, {"t", trigger.host.t}, {"lateral", trigger.host.lateral}};
  json out = {{"id", trigger.id},
              {"type", trigger.type},
              {"direction", trigger.direction},
              {"width", trigger.width},
              {"height", trigger.height},
              {"rotation", trigger.rotation},
              {"autoWidth", trigger.autoWidth},
              {"host", std::move(host)}};
  if (trigger.type == "checkpoint") out["role"] = trigger.role;
  return out;
}

json connectionToJson(const Connection& connection) {
  return json{{"id", connection.id},
              {"pointId", connection.pointId},
              {"kind", connection.kind},
              {"sourcePathId", connection.sourcePathId},
              {"sourceEnd", connection.sourceEnd},
              {"targetPathId", connection.targetPathId},
              {"targetEnd", connection.targetEnd},
              {"pathId", connection.pathId},
              {"leftPathId", connection.leftPathId},
              {"rightPathId", connection.rightPathId}};
}

}  // namespace

void backfillPointIds(TrackDefinition& track) {
  std::set<std::string> usedPointIds;
  for (const auto& path : track.paths)
    for (const auto& point : path.points)
      if (point.kind == PointKind::Position && !point.id.empty()) usedPointIds.insert(point.id);
  int nextPointId = 1;
  for (auto& path : track.paths) {
    for (auto& point : path.points) {
      if (point.kind != PointKind::Position || !point.id.empty()) continue;
      std::string candidate;
      do {
        candidate = "p" + std::to_string(nextPointId++);
      } while (usedPointIds.count(candidate));
      point.id = candidate;
      usedPointIds.insert(candidate);
    }
  }
}

TrackDefinition fromJson(const std::string& text) { return normalize(json::parse(text)); }

TrackDefinition fromFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("cannot open track file: " + path.string());
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return fromJson(buffer.str());
}

std::string toJson(const TrackDefinition& track) {
  json paths = json::array();
  for (const auto& path : track.paths) paths.push_back(pathToJson(path));

  json textureAssets = json::object();
  for (const auto& [id, asset] : track.textureAssets)
    textureAssets[id] = json{{"name", asset.name}, {"path", asset.path}, {"width", asset.width}, {"height", asset.height}, {"tileWidth", asset.tileWidth}, {"tileHeight", asset.tileHeight}};

  json zones = json::array();
  for (const auto& zone : track.zones) zones.push_back(zoneToJson(zone));

  json triggers = json::array();
  for (const auto& trigger : track.triggers) triggers.push_back(triggerToJson(trigger));

  json disjointSeams = json::array();
  for (const auto& c : track.disjointSeams) disjointSeams.push_back(connectionToJson(c));
  json junctions = json::array();
  for (const auto& c : track.junctions) junctions.push_back(connectionToJson(c));

  json selfIntersectionOverrides = json::array();
  for (const auto& o : track.selfIntersectionOverrides)
    selfIntersectionOverrides.push_back(json{{"side", o.side}, {"a", o.a}, {"b", o.b}, {"action", o.action}});

  // "samples" is intentionally omitted from the written JSON, even though it is read back in if
  // present: `TrackDefinition::samples` is kept in memory only so a load-time value still feeds a
  // live preview bake within this session, not as a field this editor round-trips.
  // Falls back at serialize time only -- an empty in-memory name (mid-edit, e.g. the track-name
  // field cleared but not yet retyped) is otherwise left alone rather than forced to a placeholder
  // the instant it's empty (see EditorState::setTrackName).
  const json out = {
      {"version", kSchemaVersion},
      {"name", track.name.empty() ? "Untitled Track" : track.name},
      {"start", json{{"path", track.start.path}, {"point", track.start.point}, {"reverse", track.start.reverse}}},
      {"handling", json{{"maxSpeed", track.handling.maxSpeed},
                        {"accel", track.handling.accel},
                        {"turnSpeed", track.handling.turnSpeed},
                        {"weight", track.handling.weight}}},
      {"zones", std::move(zones)},
      {"triggers", std::move(triggers)},
      {"disjointSeams", std::move(disjointSeams)},
      {"junctions", std::move(junctions)},
      {"selfIntersectionOverrides", std::move(selfIntersectionOverrides)},
      {"textureAssets", std::move(textureAssets)},
      {"paths", std::move(paths)},
  };
  return out.dump(2);
}

void toFile(const TrackDefinition& track, const std::filesystem::path& path) {
  std::ofstream output(path, std::ios::binary);
  if (!output) throw std::runtime_error("cannot open track file for writing: " + path.string());
  output << toJson(track);
}

}  // namespace editor
