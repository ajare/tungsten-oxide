// EditorTrackDefinition.hpp — the editor's own authoring-side mirror of
// cpp/core/include/TrackDefinition.hpp.
//
// Every field core's TrackDefinition.hpp already carries is exactly what the schema-10/11
// authoring format needs, so this mirror adds no editor-only fields yet. If a future
// milestone needs authoring-only bookkeeping that never round-trips through the schema (e.g. a
// UI selection hint), add it here, not in cpp/core/include/TrackDefinition.hpp — core's copy stays
// a compiled-runtime-adjacent record, this one is the mutable thing the editor edits and undoes.
//
// Deliberately a separate type from tox::TrackDefinition, not a reuse/subclass of it: the editor
// owns both JSON directions independently of cpp/core (see fromJson/toJson below), so core's loader
// is never forced to accept partially-authored/mid-edit state, and core's normalize() strictness
// (e.g. requiring 4+ position points per path) never blocks the editor from merely holding a
// track someone hasn't finished drawing yet.
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "ModelXml.hpp"
#include "Vec3.hpp"

namespace editor {

enum class PointKind { Position,
                       Roll,
                       Width,
                       CrossSection };

struct TrackPoint {
  PointKind kind{PointKind::Position};
  std::string id;
  tox::Vec3 pos;
  double weight{1.0};
  double t{0.0};
  double roll{0.0};
  // Signed percentage of this Width node's width used to shift the baked road center toward
  // edgeRight. Zero preserves the authored position spline as the road center.
  double width{36.0};
  double centerOffsetPercent{0.0};
  double curvature{0.0};
  double tightness{1.0};
  double thickness{4.0};
};

struct TextureBinding {
  std::string assetId;
  int tile{0};
};

// Mirrors core's tox::ReservationEndCapStyle/tox::ReservationEndCap (CENTRAL_RESERVATION_PLAN.md).
enum class ReservationEndCapStyle { Joined,
                                    Mitred,
                                    Rounded };

struct ReservationEndCap {
  ReservationEndCapStyle style{ReservationEndCapStyle::Joined};
  double width{0.0};
  // Rounded only: how far along the path the dome runs, in metres, independent of `width`.
  // <= 0 means the circular default (`width / 2`) -- see core's tox::ReservationEndCap.
  double noseLength{0.0};
};

// Mirrors core's tox::ReservationWidthMode.
enum class ReservationWidthMode { Fixed,
                                  Percent };

// A central reservation: a void carved out of the road between t0 and t1, tapering from
// `endCap0`/`endCap1`'s width (zero when Joined) at each end to `width` at the midpoint. `width`
// is metres when `widthMode` is Fixed, or a percentage in [0,100] of the road's own width when
// Percent. Mirrors core's tox::ReservationDefinition (CENTRAL_RESERVATION_PLAN.md). No collision
// wall is built around the void (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 2 removed the synthetic
// MeshRegion that used to back it, including the Capped-interior special case, with no interim
// replacement).
struct Reservation {
  std::string id;
  double t0{0.0}, t1{0.0}, width{0.0};
  ReservationWidthMode widthMode{ReservationWidthMode::Fixed};
  ReservationEndCap endCap0, endCap1;
};

struct Path {
  std::string id;
  bool closed{true};
  std::vector<TrackPoint> points;
  std::optional<TextureBinding> texture;
  // Namespace-qualified Willpower TrackMaterial name (e.g. "Tracks/DefaultTrack"), assigned via
  // the Materials panel. Empty only transiently (EditorState::setAvailableMaterials/
  // finishCreateDraft backfill it to the alphabetically-first material as soon as one is known);
  // mirrors core's PathDefinition::material, which this round-trips through schema-10 JSON.
  std::string material;
  std::vector<Reservation> reservations;
};

struct TextureAsset {
  std::string id, name, path;
  int width{1}, height{1}, tileWidth{1}, tileHeight{1};
};

// Mirrors core's tox::ModelPlacementDefinition (originally DRIVABLE_MESH_OBJECTS_PLAN.md Milestone
// 3; renamed and generalized by TRACK_MODEL_LIST_PLAN.md Milestone 1.1 to cover Physical AND
// Decorative model instances, not just drivable ones). `modelId` names an embedded `<Model id>`
// entry in the enclosing Track resource's `<Models>` list (TRACK_MODEL_LIST_PLAN.md) -- not a raw
// `.mppmodel` path. This struct itself is pure authored placement data; TRACK_MODEL_LIST_PLAN.md
// Milestone 4 gives the editor its own from-scratch `.mppmodel` geometry reader to resolve/render a
// placement's referenced model directly (reversing the older "editor never loads a `.mppmodel`"
// note this comment used to carry) -- until that milestone lands, resolution is still host-only, as
// it always has been. `rotation` is yaw/pitch/roll in degrees, applied in that order.
struct ModelPlacement {
  std::string id, modelId;
  tox::Vec3 position;
  tox::Vec3 rotation;
  tox::Vec3 scale{1.0, 1.0, 1.0};
};

// `kind` is `"path"` (`pathId`/`t`/`lateral` valid) or `"meshObject"` (`meshObjectId`/
// `localPosition`/`localYaw` valid instead -- DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 3.5, mirrors
// core's tox::ZoneHostDefinition). `localPosition`/`localYaw` are in the referenced placement's own
// local space, so the zone moves/rotates with the placement automatically.
struct ZoneHost {
  std::string kind{"path"};
  std::string pathId;
  double t{0.5}, lateral{0.0};
  std::string meshObjectId;
  tox::Vec3 localPosition;
  double localYaw{0.0};
};

struct Zone {
  std::string id;
  // "velocityChange" (boost), "jump", or "startGrid".
  std::string effect{"velocityChange"};
  double width{24.0}, length{40.0};
  double factor{1.5}, duration{2.0};
  ZoneHost host;
};

// Same path/meshObject split as ZoneHost above (mirrors core's tox::TriggerHostDefinition).
struct TriggerHost {
  std::string kind{"path"};
  std::string pathId;
  double t{0.5}, lateral{0.0};
  std::string meshObjectId;
  tox::Vec3 localPosition;
};

struct Trigger {
  std::string id;
  std::string type{"dummy"}, role, direction{"both"};
  double width{40.0}, height{12.0}, rotation{0.0};
  // When true, `width` is kept in sync with the host path's
  // baked road width at `host.t` instead of being manually authored -- see TriggersPanel.cpp's
  // auto-width handling. Ignored for a mesh-hosted trigger (no path/t to sample).
  bool autoWidth{false};
  TriggerHost host;
};

// `kind` distinguishes junctions ("", created only as a side effect of joining two paths) from
// disjoint seams ("opened-closed" | "split-open", created by splitting a shared point back into a
// hard, unsmoothed corner). sourcePathId/sourceEnd/targetPathId/targetEnd are junction-only fields;
// pathId (opened-closed) / leftPathId+rightPathId (split-open) are disjoint-seam-only fields --
// this carries both field sets rather than a variant, keyed by `kind`.
struct Connection {
  std::string id, pointId, kind;
  std::string sourcePathId, sourceEnd, targetPathId, targetEnd;
  std::string pathId, leftPathId, rightPathId;
};

struct SelfIntersectionOverride {
  std::string side{"left"}, a, b, action{"keep"};
};

struct Handling {
  double maxSpeed{140.0}, accel{71.0}, turnSpeed{137.5}, weight{1000.0};
};

struct Start {
  int path{0}, point{0};
  bool reverse{false};
};

struct TrackDefinition {
  int version{12};
  std::string name{"Untitled Track"};
  int samples{400};
  std::vector<Path> paths;
  std::map<std::string, TextureAsset> textureAssets;
  std::vector<ModelPlacement> meshObjects;
  std::vector<Zone> zones;
  std::vector<Trigger> triggers;
  std::vector<Connection> disjointSeams, junctions;
  std::vector<SelfIntersectionOverride> selfIntersectionOverrides;
  Handling handling;
  Start start;

  // Every non-primary embedded <Model> entry (Physical/Decorative props, TRACK_MODEL_LIST_PLAN.md
  // Milestone 6's "Load Model") -- an outer-Track-resource-XML-only concept, deliberately NOT part
  // of the schema-10/12 TrackData JSON this struct's own fromJson/toJson (EditorTrackDefinition.cpp)
  // read/write, so it's excluded from both directions there. Carried here anyway (rather than only
  // in TrackResourceCandidate, which is transient/scan-only) purely so it participates naturally in
  // EditorState's existing whole-struct undo/redo snapshots and in-session mutation, exactly like
  // every other authored list on this struct -- TrackResourceDocument.cpp seeds it from the parsed
  // candidate at load, TrackResourceSave.cpp writes it back out at save (both minus whichever entry
  // is primary, which is regenerated fresh from the bake every save and never lives here).
  std::vector<modelxml::ModelXmlDefinition> models;
};

// Backfills missing position-point ids in place. Called after every track construction/
// replacement (initial load, New, Random, Import). Without this, points have no stable identity at
// all -- every id round-trips as "", not merely "unassigned" -- which also lets freshly-minted ids
// collide with ids already on the track. Existing non-empty ids are preserved verbatim, even if
// duplicated across paths -- aliasing duplicate ids to a single shared point identity is core's job
// (TrackLoader.cpp) and a separate, unimplemented feature here ("shared/disjoint" editing). Called
// by fromJson/normalize() and by EditorState's constructor/replaceTrack, so every TrackDefinition
// an EditorState ever holds has had this run at least once, regardless of where it came from.
void backfillPointIds(TrackDefinition& track);

// Parses schema-10 JSON directly into a TrackDefinition, independently of tox::Track's loader.
// Tolerant of a mid-edit/partial authoring state (e.g. a path with fewer than four position
// points, dangling references not yet cleaned up): missing/malformed fields fall back to schema
// defaults rather than failing the whole load. Throws std::runtime_error only when the input isn't
// parseable as an object at all, or names an explicit, unsupported non-10 schema version (no legacy
// migration lives here or in cpp/core).
TrackDefinition fromJson(const std::string& text);
TrackDefinition fromFile(const std::filesystem::path& path);

// Serializes back to schema-10 JSON: used for save/export, and to hand off to
// tox::Track::fromJson for a live preview bake (core's loader/baker is reused unmodified).
std::string toJson(const TrackDefinition& track);
void toFile(const TrackDefinition& track, const std::filesystem::path& path);

}  // namespace editor
