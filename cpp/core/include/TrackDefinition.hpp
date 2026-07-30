// TrackDefinition.hpp — normalized, authored current-schema track records.
// These are deliberately separate from Track.hpp's baked physics records: the
// loader fills this runtime subset first, then native bake/mesh adapters compile it.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "Vec3.hpp"

namespace tox {

enum class TrackPointKind { Position,
                            Roll,
                            Width,
                            CrossSection };

struct TrackPointDefinition {
  TrackPointKind kind{TrackPointKind::Position};
  std::string id;
  Vec3 pos;
  double weight{1.0};
  double t{0.0};
  double roll{0.0};
  double width{36.0};
  double curvature{0.0};
  double tightness{1.0};
  double thickness{4.0};
};

struct TextureBindingDefinition {
  std::string assetId;
  int tile{0};
};

// A central reservation: a void carved out of the road between t0 and t1, tapering from zero width
// at each end to `width` at the midpoint (CENTRAL_RESERVATION_PLAN.md). t0 < t1, both in [0,1].
struct ReservationDefinition {
  std::string id;
  double t0{0.0}, t1{0.0}, width{0.0};
};

struct PathDefinition {
  std::string id;
  bool closed{true};
  std::vector<TrackPointDefinition> points;
  std::optional<TextureBindingDefinition> texture;
  // Namespace-qualified Willpower TrackMaterial name (e.g. "Tracks/DefaultTrack") authored by
  // cpp/editor's Materials panel; empty when absent (JS-originated tracks never set this).
  // TrackBake.cpp falls back to the legacy "road" materialKey when empty, so JS<->C++ geometry
  // parity is unaffected for tracks that never carry this field.
  std::string material;
  std::vector<ReservationDefinition> reservations;
};

struct MeshVertexDefinition {
  int id{-1};
  double x{0.0}, y{0.0};
};

struct MeshEdgeDefinition {
  int id{-1};
  int vertex0{-1}, vertex1{-1};
  bool rail{false};
};

struct DirectedMeshEdgeDefinition {
  int edge{-1}, v0{-1}, v1{-1};
};

struct MeshPolygonDefinition {
  int id{-1};
  std::vector<DirectedMeshEdgeDefinition> edges;
  std::vector<int> holes;
  bool hole{false};
};

struct MeshAssetDefinition {
  std::string id;
  std::string name;
  double railHeight{6.0};
  std::vector<MeshVertexDefinition> vertices;
  std::vector<MeshEdgeDefinition> edges;
  std::vector<MeshPolygonDefinition> polygons;
};

struct MeshPlacementDefinition {
  std::string id;
  std::string assetId;
  double x{0.0}, z{0.0}, rotation{0.0}, elevation{0.0};
};

struct TextureAssetDefinition {
  std::string id, name, path;
  int width{1}, height{1}, tileWidth{1}, tileHeight{1};
};

struct ZoneHostDefinition {
  std::string kind{"path"};
  std::string pathId, meshId;
  double t{0.5}, lateral{0.0};
  double x{0.0}, z{0.0}, rotation{0.0};
};

struct ZoneDefinition {
  std::string id;
  std::string effect{"velocityChange"};
  double width{24.0}, length{40.0};
  double factor{1.5}, duration{2.0};
  ZoneHostDefinition host;
};

struct TriggerHostDefinition {
  std::string kind{"path"};
  std::string pathId, meshId;
  double t{0.5}, lateral{0.0}, x{0.0}, z{0.0};
};

struct TriggerDefinition {
  std::string id;
  std::string type{"dummy"}, role, direction{"both"};
  double width{40.0}, height{12.0}, rotation{0.0};
  TriggerHostDefinition host;
};

struct ConnectionDefinition {
  std::string id, pointId, kind;
  std::string sourcePathId, sourceEnd, targetPathId, targetEnd;
};

struct SelfIntersectionOverrideDefinition {
  std::string side{"left"}, a, b, action{"keep"};
};

struct HandlingDefinition {
  double maxSpeed{140.0}, accel{71.0}, turnSpeed{137.5}, weight{1000.0};
};

struct StartDefinition {
  int path{0}, point{0};
  bool reverse{false};
};

struct TrackDefinition {
  int version{10};
  std::string name{"Untitled Track"};
  int samples{400};
  std::vector<PathDefinition> paths;
  std::map<std::string, MeshAssetDefinition> meshAssets;
  std::vector<MeshPlacementDefinition> meshes;
  std::map<std::string, TextureAssetDefinition> textureAssets;
  std::vector<ZoneDefinition> zones;
  std::vector<TriggerDefinition> triggers;
  std::vector<ConnectionDefinition> disjointSeams, junctions;
  std::vector<SelfIntersectionOverrideDefinition> selfIntersectionOverrides;
  HandlingDefinition handling;
  StartDefinition start;
};

}  // namespace tox
