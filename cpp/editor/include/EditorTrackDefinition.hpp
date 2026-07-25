// EditorTrackDefinition.hpp — the editor's own authoring-side mirror of
// cpp/core/include/TrackDefinition.hpp (EDITOR_CPP_PORT_PLAN.md M1).
//
// Audited field-for-field against track-core.js's parseTrack/serializeTrack (the JS authoring
// source of truth): every field core's TrackDefinition.hpp already carries is exactly what the
// schema-10 authoring format needs, so this mirror adds no editor-only fields yet. If a future
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
  double width{36.0};
  double curvature{0.0};
  double tightness{1.0};
  double thickness{4.0};
};

struct TextureBinding {
  std::string assetId;
  int tile{0};
};

struct Path {
  std::string id;
  bool closed{true};
  std::vector<TrackPoint> points;
  std::optional<TextureBinding> texture;
};

// `attributesJson` carries the geometry-js "attributes" object verbatim as opaque serialized JSON
// text (default "{}"), so vertex/polygon attributes this editor doesn't understand (UVs, colours,
// material keys) survive a load/save round trip rather than being silently dropped -- see
// EDITOR_PARITY_FIXES.md finding 6. Kept as text rather than a structured type so this header
// doesn't have to depend on a JSON library; EditorTrackDefinition.cpp parses/merges it as needed.
struct MeshVertex {
  int id{-1};
  double x{0.0}, y{0.0};
  std::string attributesJson{"{}"};
};

// `attributesJson` holds every edge attribute EXCEPT "rail", which stays its own structured field
// since the editor actively reads/writes it (Rails mode). The two are merged back together on
// serialize (see meshAssetToJson).
struct MeshEdge {
  int id{-1};
  int vertex0{-1}, vertex1{-1};
  bool rail{false};
  std::string attributesJson{"{}"};
};

struct DirectedMeshEdge {
  int edge{-1}, v0{-1}, v1{-1};
};

struct MeshPolygon {
  int id{-1};
  std::vector<DirectedMeshEdge> edges;
  std::vector<int> holes;
  bool hole{false};
  std::string attributesJson{"{}"};
};

struct MeshAsset {
  std::string id;
  std::string name;
  double railHeight{6.0};
  std::vector<MeshVertex> vertices;
  std::vector<MeshEdge> edges;
  std::vector<MeshPolygon> polygons;
};

struct MeshPlacement {
  std::string id;
  std::string assetId;
  double x{0.0}, z{0.0}, rotation{0.0}, elevation{0.0};
};

struct TextureAsset {
  std::string id, name, path;
  int width{1}, height{1}, tileWidth{1}, tileHeight{1};
};

struct ZoneHost {
  std::string kind{"path"};
  std::string pathId, meshId;
  double t{0.5}, lateral{0.0};
  double x{0.0}, z{0.0}, rotation{0.0};
};

struct Zone {
  std::string id;
  std::string effect{"velocityChange"};
  double width{24.0}, length{40.0};
  double factor{1.5}, duration{2.0};
  ZoneHost host;
};

struct TriggerHost {
  std::string kind{"path"};
  std::string pathId, meshId;
  double t{0.5}, x{0.0}, z{0.0};
};

struct Trigger {
  std::string id;
  std::string type{"dummy"}, role, direction{"both"};
  double width{40.0}, height{12.0}, rotation{0.0};
  TriggerHost host;
};

struct Connection {
  std::string id, pointId, kind;
  std::string sourcePathId, sourceEnd, targetPathId, targetEnd;
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
  int version{10};
  std::string name{"Untitled Track"};
  int samples{400};
  std::vector<Path> paths;
  std::map<std::string, MeshAsset> meshAssets;
  std::vector<MeshPlacement> meshes;
  std::map<std::string, TextureAsset> textureAssets;
  std::vector<Zone> zones;
  std::vector<Trigger> triggers;
  std::vector<Connection> disjointSeams, junctions;
  std::vector<SelfIntersectionOverride> selfIntersectionOverrides;
  Handling handling;
  Start start;
};

// Backfills missing position-point ids in place, mirroring track-core.js's parseTrack id
// assignment (track-core.js:1665-1678) and, for state that doesn't come from JSON at all, its
// ensureTrackIds() (called after every track construction/replacement in js/editor.js: initial
// load, New, Random, Import). Without this, points have no stable identity at all -- every id
// round-trips as "", not merely "unassigned" -- which is also what let freshly-minted ids collide
// with ids already on the track (EDITOR_PARITY_FIXES.md findings 1, 2, 4). Existing non-empty ids
// are preserved verbatim, even if duplicated across paths -- aliasing duplicate ids to a single
// shared point identity is core's job (TrackLoader.cpp) and a separate, unimplemented feature here
// (EDITOR_PARITY_FIXES.md gap 5, "shared/disjoint" editing). Called by fromJson/normalize() and by
// EditorState's constructor/replaceTrack, so every TrackDefinition an EditorState ever holds has
// had this run at least once, regardless of where it came from.
void backfillPointIds(TrackDefinition& track);

// Parses schema-10 JSON directly into a TrackDefinition, independently of tox::Track's loader.
// Tolerant of a mid-edit/partial authoring state (e.g. a path with fewer than four position
// points, dangling references not yet cleaned up): missing/malformed fields fall back to schema
// defaults rather than failing the whole load, mirroring editor.js's own lenient parseTrack.
// Throws std::runtime_error only when the input isn't parseable as an object at all, or names an
// explicit, unsupported non-10 schema version (no legacy migration lives here or in cpp/core).
TrackDefinition fromJson(const std::string& text);
TrackDefinition fromFile(const std::filesystem::path& path);

// Serializes back to schema-10 JSON: used for save/export, and to hand off to
// tox::Track::fromJson for a live preview bake (core's loader/baker is reused unmodified).
std::string toJson(const TrackDefinition& track);
void toFile(const TrackDefinition& track, const std::filesystem::path& path);

struct MeshAssetParseResult {
  std::optional<MeshAsset> asset;
  std::string error;  // set only when asset is nullopt
};

// Parses a standalone geometry-js mesh export (the "mesh" field of one track.meshAssets[id]
// entry, or a bare {vertices,edges,polygons} document straight from the ext/geoemetry-js editor's
// "Copy JSON" button) for mesh import/paste (EDITOR_NATIVE_FILE_IO_PLAN.md M9). Mirrors
// js/editor.js's parseMeshJSON: never throws, reports why parsing failed instead. The returned
// asset's `id`/`name` are unset -- callers assign a fresh id when registering it (see
// EditorState::importMeshAsset).
MeshAssetParseResult parseMeshAssetJson(const std::string& text);

}  // namespace editor
