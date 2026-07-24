// Track.hpp — authored current-schema data plus the baked, world-space runtime
// records and renderer-neutral geometry. Track::fromJson/fromFile normalizes the
// definition and compiles its spline paths and mesh placements. The legacy
// parity harness may still construct baked records directly from committed JS traces.
#pragma once
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include "Vec3.hpp"
#include "TrackDefinition.hpp"
#include "TrackGeometry.hpp"
#include "TrackMesh.hpp"

namespace tox {

struct TrackLoadResult;

// One baked centerline frame. Only the fields the physics reads at runtime are
// carried (h/roll/width were baking-time only; sampleTrack never touches them).
struct Frame {
  Vec3 pos, tangent, h, edgeRight, normal;
  double roll{0.0}, width{0.0}, halfW{0.0}, sLeft{0.0}, sRight{0.0};
  double crossSectionCurvature{0.0}, crossSectionTightness{1.0}, crossSectionThickness{0.0};
};

struct EndpointIds {
  std::string start, end;
  bool hasStart{false}, hasEnd{false};
};

struct Path {
  bool closed{true};
  EndpointIds endpointIds;
  std::vector<Vec3> anchors;
  std::vector<Frame> centerline;
};

// A compiled path-hosted boost zone (mirror of track-game.js buildZones / the
// bake in js/track-bake.js). Detection compares the sampled path by index
// (JS compares the path object identity `sample.pathObj === z.hostPath`).
struct Zone {
  std::string id;
  std::string kind;    // "path" (mesh zones are out of scope, corpus emits none)
  std::string effect;  // "velocityChange" | "startGrid"
  double factor{0.0}, duration{0.0};
  int hostPathIndex{0};
  double gLo{0.0}, gHi{0.0}, gMax{1.0};
  bool closed{true};
  double lateral{0.0}, halfWidth{0.0};
};

// A compiled trigger gate: baked world-space frame (center + right/up/fwd) and
// extent, plus its checkpoint role (mirror of track-game.js buildTriggers).
struct Trigger {
  std::string id, type, role, direction;
  Vec3 center, right, up, fwd;
  double halfWidth{0.0}, height{0.0};
};

struct Track {
  // Authored current-schema runtime subset retained alongside its compiled data.
  TrackDefinition definition;

  std::vector<Path> paths;
  std::set<std::string> connectedEndpointIds;
  double trackFloorY{-1e9};
  std::vector<Zone> zones;
  std::vector<Trigger> triggers;
  std::vector<MeshRegion> meshRegions;
  std::vector<GeometryBatch> geometry;

  bool endpointConnected(const std::string& id, bool present) const;  // src/Track.cpp

  static TrackLoadResult fromJson(std::string_view text);
  static TrackLoadResult fromFile(const std::filesystem::path& path);
};

struct TrackWarning {
  std::string code, message, objectId;
};

struct TrackLoadResult {
  std::optional<Track> track;
  std::vector<TrackWarning> warnings;
  std::string error;

  explicit operator bool() const { return track.has_value() && error.empty(); }
};

}  // namespace tox
