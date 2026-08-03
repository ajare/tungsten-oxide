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
  // Signed percentage of this Width node's width used to shift the baked road center toward
  // edgeRight. Zero preserves the authored position spline as the road center.
  double centerOffsetPercent{0.0};
  double curvature{0.0};
  double tightness{1.0};
  double thickness{4.0};
};

struct TextureBindingDefinition {
  std::string assetId;
  int tile{0};
};

// How a reservation's void meets the road surface at one end. Joined ramps all the way to zero
// width there (a point), matching the original single-`width` taper. Mitred and Rounded instead
// hold the taper at `ReservationEndCap::width` (a full cross-track width, clamped to at most the
// reservation's own `width`): Mitred cuts off square at exactly that width, Rounded closes the last
// `noseLength` metres as an elliptical dome instead -- `width` across, `noseLength` along the path
// -- leaving the tip with the vertical tangent that makes a rounded end read as rounded.
//
// `noseLength` is deliberately independent of `width`. Tying the two together (a true half-circle,
// `width / 2` long) makes the dome a couple of metres long on a reservation running hundreds of
// them -- geometrically honest, but far too small to see at track zoom. `noseLength <= 0` falls
// back to that circular `width / 2`, which is also what a file written before this field existed
// means.
enum class ReservationEndCapStyle { Joined,
                                    Mitred,
                                    Rounded };

struct ReservationEndCap {
  ReservationEndCapStyle style{ReservationEndCapStyle::Joined};
  double width{0.0};
  double noseLength{0.0};
};

// Whether ReservationDefinition::width is an absolute metres value (Fixed, the original and
// default behavior) or a percentage in [0,100] of the road's own width at each point along the
// span (Percent) -- the void's peak then rises and falls with the road's own authored width curve
// rather than staying a fixed number of metres. End-cap widths are unaffected either way; they stay
// an absolute metres value regardless of the reservation's own mode.
enum class ReservationWidthMode { Fixed,
                                  Percent };

// A central reservation: a void carved out of the road between t0 and t1, tapering from
// `endCap0`/`endCap1`'s width (zero when Joined) at each end to `width` at the midpoint
// (CENTRAL_RESERVATION_PLAN.md). t0 < t1, both in [0,1]. endCap0 governs the t0 end, endCap1 the
// t1 end. The void is carved purely into the road surface/underside (TrackBake.cpp's pathGeometry)
// -- no collision wall is built around it (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 2 removed the
// synthetic MeshRegion that used to back the reservation's wall/floor physics, including the
// Capped-interior special case, with no interim replacement; a car can currently drive straight
// off the void's edge in analytic mode). Milestone 5 is expected to replace reservations with real
// drivable mesh object placements, which will restore collidable walls.
struct ReservationDefinition {
  std::string id;
  double t0{0.0}, t1{0.0}, width{0.0};
  ReservationWidthMode widthMode{ReservationWidthMode::Fixed};
  ReservationEndCap endCap0, endCap1;
};

struct PathDefinition {
  std::string id;
  bool closed{true};
  std::vector<TrackPointDefinition> points;
  std::optional<TextureBindingDefinition> texture;
  // Namespace-qualified Willpower TrackMaterial name (e.g. "Tracks/DefaultTrack") authored by
  // cpp/editor's Materials panel; empty when absent (older tracks never set this).
  // TrackBake.cpp falls back to the legacy "road" materialKey when empty, so
  // output is unaffected for tracks that never carry this field.
  std::string material;
  std::vector<ReservationDefinition> reservations;
};

struct TextureAssetDefinition {
  std::string id, name, path;
  int width{1}, height{1}, tileWidth{1}, tileHeight{1};
};

// An instance of an externally modeled 3D mesh (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 3),
// replacing the old flat 2D Mesh region. `modelId` references a `.mppmodel` resource by id -- the
// geometry itself is never embedded in track JSON, and `core` never loads or compiles it (see the
// plan's "`.mppmodel` loading is host-only" architecture note): this struct is pure authored data,
// carried through loading/normalization unchanged for the game host to resolve into world-space
// triangles at runtime. `rotation` is yaw/pitch/roll in degrees, applied in that order.
struct DrivableMeshObjectPlacementDefinition {
  std::string id, modelId;
  Vec3 position;
  Vec3 rotation;
  Vec3 scale{1.0, 1.0, 1.0};
};

// `kind` is always "path" now -- the mesh-hosted variant (meshId + x/z/rotation) was removed along
// with MeshRegion (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 2); kept as a string discriminator for
// Milestone 3.5's eventual drivable-mesh-object-hosted variant to reuse.
struct ZoneHostDefinition {
  std::string kind{"path"};
  std::string pathId;
  double t{0.5}, lateral{0.0};
};

struct ZoneDefinition {
  std::string id;
  // "velocityChange" (boost), "jump", or "startGrid".
  std::string effect{"velocityChange"};
  double width{24.0}, length{40.0};
  double factor{1.5}, duration{2.0};
  ZoneHostDefinition host;
};

// Same "always path now" note as ZoneHostDefinition above.
struct TriggerHostDefinition {
  std::string kind{"path"};
  std::string pathId;
  double t{0.5}, lateral{0.0};
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
  std::map<std::string, TextureAssetDefinition> textureAssets;
  std::vector<DrivableMeshObjectPlacementDefinition> meshObjects;
  std::vector<ZoneDefinition> zones;
  std::vector<TriggerDefinition> triggers;
  std::vector<ConnectionDefinition> disjointSeams, junctions;
  std::vector<SelfIntersectionOverrideDefinition> selfIntersectionOverrides;
  HandlingDefinition handling;
  StartDefinition start;
};

}  // namespace tox
