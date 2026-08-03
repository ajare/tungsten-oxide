// Track.hpp — authored current-schema data plus the baked, world-space runtime
// records and renderer-neutral geometry. Track::fromJson/fromFile normalizes the
// definition and compiles its spline paths and mesh placements. The legacy
// parity harness may still construct baked records directly from the committed golden trace corpus.
#pragma once
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include "Vec3.hpp"
#include "TrackDefinition.hpp"
#include "TrackCollision.hpp"
#include "TrackGeometry.hpp"

namespace tox {

struct TrackLoadResult;

// One baked centerline frame. Only the fields the physics reads at runtime are
// carried (h/roll/width were baking-time only; sampleTrack never touches them).
struct Frame {
  Vec3 pos, tangent, h, edgeRight, normal;
  double roll{0.0}, width{0.0}, halfW{0.0}, sLeft{0.0}, sRight{0.0};
  double crossSectionCurvature{0.0}, crossSectionTightness{1.0}, crossSectionThickness{0.0};
  // Half-width of the central-reservation void at this frame (0 when none is active here); the gap
  // spans [-reservationHalfGap, +reservationHalfGap] in cross-section v-space, centered on the
  // path. CENTRAL_RESERVATION_PLAN.md M1. `reservationIndex` (into the owning PathDefinition's
  // `reservations`, -1 when none is active) lets reservationGeometry group frames by which
  // reservation they belong to without needing to re-derive each frame's own `t` -- render frames
  // (unlike physics centerline frames) aren't uniformly spaced in t, so that isn't recoverable from
  // array index alone.
  double reservationHalfGap{0.0};
  int reservationIndex{-1};
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

// A compiled zone, path-hosted (`kind == "path"`, `gLo`/`gHi`/`gMax`/`closed`/`lateral` valid) or
// drivable-mesh-object-hosted (`kind == "meshObject"`, `x`/`z`/`rotation`/`halfLength`/`halfWidth`
// valid instead -- DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 3.5, restoring the flat-rectangle host
// capability the old Mesh-region host provided, generalized off a placement's transform instead of
// a MeshRegion's own x/z/rotation). `rotation` is the placement's yaw, in radians, plus the host's
// own `localYaw` offset -- see TrackBake.cpp. A meshObject-hosted zone gets no render geometry
// (unlike the path-hosted case, which emits a `ZoneSurface` batch) -- deliberately deferred, since
// core has no geometry for the referenced model to place a visual quad against without loading it,
// which it never does (see the plan's "`.mppmodel` loading is host-only" architecture note); the
// zone is still fully functional for physics, just visually absent until a future milestone adds
// host-side (or editor-side) rendering for it.
struct Zone {
  std::string id;
  std::string kind;
  std::string effect;  // "velocityChange" | "jump" | "startGrid"
  double factor{0.0}, duration{0.0};
  int hostPathIndex{0};
  double gLo{0.0}, gHi{0.0}, gMax{1.0};
  bool closed{true};
  double lateral{0.0}, halfWidth{0.0};
  double x{0.0}, z{0.0}, rotation{0.0}, halfLength{0.0};
};

// A compiled trigger gate: baked world-space frame (center + right/up/fwd) and
// extent, plus its checkpoint role.
struct Trigger {
  std::string id, type, role, direction;
  Vec3 center, right, up, fwd;
  double halfWidth{0.0}, height{0.0};
};

// A self-intersecting crossing found on one edge of one path: every place the edge's own polyline
// crosses itself, keyed by the two control points nearest its branches (a stable identity across
// edits/resampling, matching TrackCore.crossingKey) rather than by segment index (which shifts on
// every edit). `a`/`b` are stored order-insensitively sorted so a lookup never needs to check both
// orderings. Whether this crossing was actually collapsed by the bake (span <= the default window,
// or a matching SelfIntersectionOverrideDefinition) is NOT stored here -- that is re-derived at
// draw/lookup time from `span` plus TrackDefinition::selfIntersectionOverrides, so an override
// change never needs re-detection.
struct SelfIntersection {
  std::string side;  // "left" | "right"
  std::string a, b;  // control-point ids, sorted (a <= b)
  int span{0};
  Vec3 point;  // world-space intersection, XZ-plane
};

struct Track {
  // Authored current-schema runtime subset retained alongside its compiled data.
  TrackDefinition definition;

  std::vector<Path> paths;
  std::set<std::string> connectedEndpointIds;
  double trackFloorY{-1e9};
  std::vector<Zone> zones;
  std::vector<Trigger> triggers;
  std::vector<GeometryBatch> geometry;
  // Optional external road triangles supplied by a native Track resource.
  // JSON-only consumers leave this null and retain analytical collision.
  TrackCollisionSurfacePtr collisionSurface;
  // Every self-intersection found across every path/side, from an UNBOUNDED full pairwise scan on
  // the pre-collapse edges -- unlike the bounded, iterative collapse pass that actually mutates
  // the baked geometry, this finds every crossing regardless of span, so
  // the editor can show/cycle markers for far ("auto-keep") crossings too. Populated only when
  // `fromJson`/`fromFile` is called with `detectSelfIntersections` true (the default); left empty
  // otherwise. Empty (not skipped) is indistinguishable from "genuinely no crossings" from this
  // field alone -- callers that need to tell the difference (the editor, mid-drag) track that
  // themselves rather than relying on this field's emptiness.
  std::vector<SelfIntersection> selfIntersections;

  bool endpointConnected(const std::string& id, bool present) const;  // src/Track.cpp

  // `detectSelfIntersections` gates the extra O(N^2) full-pairwise-scan pass that populates
  // `selfIntersections` above -- on by default (every load is a one-time cost everywhere except the
  // editor's own live-preview rebake, which passes false while a drag is in progress and reuses its
  // last good detection result instead).
  static TrackLoadResult fromJson(std::string_view text, bool detectSelfIntersections = true);
  static TrackLoadResult fromFile(const std::filesystem::path& path, bool detectSelfIntersections = true);
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
