// EditorState.hpp — mode/selection/drag/create-draft state for point editing
// (EDITOR_CPP_PORT_PLAN.md M3), mirroring js/editor.js's editMode/selectedPointId/dragging/
// createDraft globals and setEditMode/nodeAtTop/deleteSelected/createModeClick functions. M4 adds
// mesh placement select/drag/rotate/delete, mirroring selectedMeshId/meshDragOffset/meshRotateStart
// and the drag branches in editor.js's topCanvas mousedown handler.
//
// M5 adds rail-edge toggling (toggleRailEdge), mirroring TrackMesh.toggleRailEdge and railSel:
// rails live on the shared MeshAsset, not the placement, so toggling one flips it for every placed
// instance of that asset.
//
// M7b adds texture asset registration/deletion/tile-sizing and per-path texture assignment,
// mirroring editor.js's addTextureAsset/deleteTextureAsset/clampTextureTileSize+
// clearInvalidTextureAssignments/assignCurrentCurveTexture/clearCurrentCurveTexture. Image
// decoding and GL upload live in TextureCache.hpp/.cpp instead -- EditorState only ever holds the
// schema-level TextureAsset record (name/path/dimensions), same separation TopDownCanvas.cpp
// keeps between authored data and its own rendering.
//
// Scope: position points and mesh placements only -- roll/width/cross-section handles, segment
// deletion/splitting, shared/disjoint point identity, zone/trigger picking, and mesh *asset*
// authoring (there's no import UI; see main.cpp's hardcoded test asset) are all still
// editor.js-only (later milestones or out of scope).
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "EditorHistory.hpp"
#include "EditorTrackDefinition.hpp"

namespace editor {

enum class EditMode { Edit,
                      Create,
                      Rails };

struct SelectedPoint {
  int pathIndex{-1};
  int pointIndex{-1};
  bool valid() const { return pathIndex >= 0 && pointIndex >= 0; }
};

// The picked-edge highlight, mirroring editor.js's railSel -- meaningful only in Rails mode.
struct SelectedRail {
  std::string meshId;  // placement id, so the highlight follows a specific placed instance
  int edgeId{-1};
};

class EditorState {
 public:
  // backfillPointIds() mirrors js/editor.js's ensureTrackIds(), called after every track
  // construction/replacement there (initial load, New, Random, Import). Without it here, a track
  // built in memory rather than loaded from JSON (main.cpp's buildStarterTrack(), New,
  // generateRandomTrack()) has no point ids at all, which silently defeats both the id-collision
  // fix (ids are minted by scanning for a gap -- irrelevant if nothing has an id yet) and the
  // start-point-preservation fix (which matches by id) the moment this constructor is skipped --
  // see EDITOR_PARITY_FIXES.md findings 1 and 4.
  explicit EditorState(TrackDefinition initial) : track_(std::move(initial)) { backfillPointIds(track_); }

  const TrackDefinition& track() const { return track_; }
  EditMode mode() const { return mode_; }
  SelectedPoint selection() const { return selection_; }
  const std::vector<tox::Vec3>& createDraft() const { return createDraft_; }
  bool dragging() const { return dragging_; }
  const std::optional<std::string>& selectedMeshId() const { return selectedMeshId_; }
  bool meshDragging() const { return meshDragging_; }
  bool meshRotating() const { return meshRotating_; }

  const MeshPlacement* findMeshPlacement(const std::string& id) const {
    for (const auto& placement : track_.meshes)
      if (placement.id == id) return &placement;
    return nullptr;
  }

  History& history() { return history_; }

  // Mirrors editor.js's undo()/redo(): push the current state onto the opposite stack, restore
  // the popped one. Returns false (no-op) if there was nothing to undo/redo.
  bool undo() {
    auto restored = history_.undo(track_);
    if (!restored) return false;
    replaceTrackKeepHistory(std::move(*restored));
    return true;
  }

  bool redo() {
    auto restored = history_.redo(track_);
    if (!restored) return false;
    replaceTrackKeepHistory(std::move(*restored));
    return true;
  }

  // Mirrors setEditMode(): clears the abandoned create draft, drops drag state, so switching
  // modes mid-gesture can never leave a dangling half-mutation.
  void setMode(EditMode mode) {
    mode_ = mode;
    createDraft_.clear();
    dragging_ = false;
    dragMutated_ = false;
    meshDragging_ = meshDragMutated_ = meshRotating_ = meshRotateMutated_ = false;
    // The picked-edge highlight only means anything inside Rails mode (mirrors setEditMode()).
    if (mode != EditMode::Rails) selectedRail_.reset();
  }

  // Returns true if a position point was hit within `pickRadiusWorld` of (worldX, worldZ).
  // Selects it (Edit mode's plain click) but does not start a drag -- call beginDrag separately
  // once the caller knows the mouse is actually moving.
  bool selectPositionAt(double worldX, double worldZ, double pickRadiusWorld) {
    const auto hit = hitTestPosition(worldX, worldZ, pickRadiusWorld);
    if (!hit) return false;
    selection_ = *hit;
    selectedMeshId_.reset();  // path points and mesh regions share one selection (props panel)
    return true;
  }

  void clearSelection() { selection_ = {}; }

  // ---- Mesh placements (EDITOR_CPP_PORT_PLAN.md M4) ----

  void selectMesh(const std::string& placementId) {
    selectedMeshId_ = placementId;
    selection_ = {};
  }

  void clearMeshSelection() { selectedMeshId_.reset(); }

  // Adds a new placement of an already-registered mesh asset (see track().meshAssets) at
  // (x, z), unrotated, selecting it. There is no asset-authoring UI yet, so the caller is
  // responsible for the asset already existing.
  bool placeMeshAsset(const std::string& assetId, double x, double z) {
    if (!track_.meshAssets.count(assetId)) return false;
    history_.push(track_);
    MeshPlacement placement;
    placement.id = newMeshPlacementId();
    placement.assetId = assetId;
    placement.x = std::round(x * 10.0) / 10.0;
    placement.z = std::round(z * 10.0) / 10.0;
    const std::string placedId = placement.id;
    track_.meshes.push_back(std::move(placement));
    selectMesh(placedId);
    return true;
  }

  // Registers a freshly parsed mesh (EDITOR_NATIVE_FILE_IO_PLAN.md M9, e.g. from
  // parseMeshAssetJson) under a fresh id derived from `name`, rails every boundary edge by
  // default (mirrors railBoundaryEdges -- an imported region should be enclosed the instant it
  // lands, not a bare rim the ship slides straight off), and drops one placement of it centred at
  // (centerWorldX, centerWorldZ) -- the caller passes either the current view centre (toolbar
  // import) or a click position (paste-from-context-menu), mirroring addMeshAsset's `at` param.
  // Returns the new asset id.
  std::string importMeshAsset(MeshAsset asset, const std::string& name, double centerWorldX, double centerWorldZ) {
    railBoundaryEdgesOf(asset);

    history_.push(track_);
    const std::string assetId = uniqueMeshAssetId(name);
    asset.id = assetId;
    asset.name = assetId;

    double minX = std::numeric_limits<double>::infinity(), maxX = -std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity(), maxY = -std::numeric_limits<double>::infinity();
    for (const auto& v : asset.vertices) {
      minX = std::min(minX, v.x);
      maxX = std::max(maxX, v.x);
      minY = std::min(minY, v.y);
      maxY = std::max(maxY, v.y);
    }
    const double centroidX = std::isfinite(minX) ? (minX + maxX) / 2.0 : 0.0;
    const double centroidY = std::isfinite(minY) ? (minY + maxY) / 2.0 : 0.0;

    track_.meshAssets.emplace(assetId, std::move(asset));

    MeshPlacement placement;
    placement.id = newMeshPlacementId();
    placement.assetId = assetId;
    placement.x = std::round((centerWorldX - centroidX) * 10.0) / 10.0;
    placement.z = std::round((centerWorldZ - centroidY) * 10.0) / 10.0;
    const std::string placedId = placement.id;
    track_.meshes.push_back(std::move(placement));
    selectMesh(placedId);
    return assetId;
  }

  // Parses `text` (a file's contents or the clipboard) as a mesh export and imports it via
  // importMeshAsset if it parses -- the shared path behind both the toolbar's Import/Paste Mesh
  // buttons and the top-down canvas's right-click "Paste Mesh" (EDITOR_NATIVE_FILE_IO_PLAN.md M9,
  // mirrors importMeshFile/pasteMeshFromClipboard sharing js/editor.js's parseMeshJSON). Returns
  // the parse error, or nullopt on success.
  std::optional<std::string> importMeshFromJsonText(const std::string& text, const std::string& name, double centerWorldX,
                                                     double centerWorldZ) {
    MeshAssetParseResult parsed = parseMeshAssetJson(text);
    if (!parsed.asset) return parsed.error;
    importMeshAsset(std::move(*parsed.asset), name, centerWorldX, centerWorldZ);
    return std::nullopt;
  }

  // One pushUndo() per drag gesture, mirroring dragSelectedTo. `worldX`/`worldZ` is the mouse's
  // world position at drag start; the offset to the placement's current x/z is preserved for the
  // whole gesture so the shape doesn't jump to the cursor.
  void beginMeshDrag(double worldX, double worldZ) {
    MeshPlacement* placement = mutableSelectedMeshPlacement();
    if (!placement) return;
    meshDragOffsetX_ = placement->x - worldX;
    meshDragOffsetZ_ = placement->z - worldZ;
    meshDragging_ = true;
    meshDragMutated_ = false;
  }

  void dragMeshTo(double worldX, double worldZ) {
    MeshPlacement* placement = mutableSelectedMeshPlacement();
    if (!meshDragging_ || !placement) return;
    if (!meshDragMutated_) {
      history_.push(track_);
      meshDragMutated_ = true;
    }
    placement->x = std::round((worldX + meshDragOffsetX_) * 10.0) / 10.0;
    placement->z = std::round((worldZ + meshDragOffsetZ_) * 10.0) / 10.0;
  }

  void endMeshDrag() {
    meshDragging_ = false;
    meshDragMutated_ = false;
  }

  // Shift+drag rotate: `startAngleDeg` is the mouse's angle-from-placement-origin at drag start
  // (atan2(dz, dx) in degrees, matching TrackMesh's localToWorld convention -- see
  // js/track-mesh.js's placementTrig). The offset between that and the placement's rotation at
  // mousedown is preserved for the whole gesture, so the shape doesn't jump to face the cursor the
  // instant the drag begins (CLAUDE.md's editor-conventions note on this exact interaction).
  void beginMeshRotate(double startAngleDeg) {
    MeshPlacement* placement = mutableSelectedMeshPlacement();
    if (!placement) return;
    meshRotateOriginRotation_ = placement->rotation;
    meshRotateStartAngle_ = startAngleDeg;
    meshRotating_ = true;
    meshRotateMutated_ = false;
  }

  void dragMeshRotateTo(double currentAngleDeg) {
    MeshPlacement* placement = mutableSelectedMeshPlacement();
    if (!meshRotating_ || !placement) return;
    if (!meshRotateMutated_) {
      history_.push(track_);
      meshRotateMutated_ = true;
    }
    placement->rotation = meshRotateOriginRotation_ + (currentAngleDeg - meshRotateStartAngle_);
  }

  void endMeshRotate() {
    meshRotating_ = false;
    meshRotateMutated_ = false;
  }

  // ---- Rails (EDITOR_CPP_PORT_PLAN.md M5) ----

  const std::optional<SelectedRail>& selectedRail() const { return selectedRail_; }

  // Flips a shared MeshAsset edge's rail flag -- rails live on the asset, not the placement, so
  // toggling one affects every placed instance of that asset at once (mirrors
  // TrackMesh.toggleRailEdge). `meshId` is the placement the edge was picked through, kept only
  // for the selection highlight. Returns false if the asset/edge no longer exist.
  bool toggleRailEdge(const std::string& meshId, const std::string& assetId, int edgeId) {
    const auto assetIt = track_.meshAssets.find(assetId);
    if (assetIt == track_.meshAssets.end()) return false;
    const auto edgeIt = std::find_if(assetIt->second.edges.begin(), assetIt->second.edges.end(),
                                     [&](const MeshEdge& e) { return e.id == edgeId; });
    if (edgeIt == assetIt->second.edges.end()) return false;
    history_.push(track_);
    edgeIt->rail = !edgeIt->rail;
    selectedMeshId_ = meshId;
    selection_ = {};
    selectedRail_ = SelectedRail{meshId, edgeId};
    return true;
  }

  void clearRailSelection() { selectedRail_.reset(); }

  // ---- Texture assets (EDITOR_CPP_PORT_PLAN.md M7b) ----

  // Registers a newly loaded image as a texture asset (mirrors addTextureAsset): the id is
  // derived from `name` (mirrors TrackMesh.uniqueAssetId -- sanitized, deduped against existing
  // ids), and the tile size starts at the full image (one tile), same as editor.js.
  std::string addTextureAsset(const std::string& name, const std::string& path, int width, int height) {
    history_.push(track_);
    const std::string id = uniqueTextureAssetId(name);
    TextureAsset asset;
    asset.id = id;
    asset.name = name;
    asset.path = path;
    asset.width = std::max(1, width);
    asset.height = std::max(1, height);
    asset.tileWidth = asset.width;
    asset.tileHeight = asset.height;
    track_.textureAssets.emplace(id, std::move(asset));
    return id;
  }

  // Mirrors deleteTextureAsset: also clears any path currently bound to it.
  bool deleteTextureAsset(const std::string& assetId) {
    if (!track_.textureAssets.count(assetId)) return false;
    history_.push(track_);
    track_.textureAssets.erase(assetId);
    for (auto& path : track_.paths)
      if (path.texture && path.texture->assetId == assetId) path.texture.reset();
    return true;
  }

  // Mirrors clampTextureTileSize + clearInvalidTextureAssignments: shrinking a tile size below
  // the count a path is currently pointing at clears that binding rather than leaving it dangling.
  bool setTextureTileSize(const std::string& assetId, bool isWidth, int value) {
    const auto it = track_.textureAssets.find(assetId);
    if (it == track_.textureAssets.end()) return false;
    history_.push(track_);
    TextureAsset& asset = it->second;
    const int maxSide = isWidth ? asset.width : asset.height;
    const int clamped = std::max(1, std::min(maxSide, value));
    if (isWidth) asset.tileWidth = clamped; else asset.tileHeight = clamped;
    const int cols = asset.tileWidth > 0 ? asset.width / asset.tileWidth : 0;
    const int rows = asset.tileHeight > 0 ? asset.height / asset.tileHeight : 0;
    const int count = cols * rows;
    for (auto& path : track_.paths)
      if (path.texture && path.texture->assetId == assetId && path.texture->tile >= count) path.texture.reset();
    return true;
  }

  // Mirrors assignCurrentCurveTexture: no-op (no history push) if already assigned exactly this
  // asset/tile, same guard as the JS version.
  bool assignPathTexture(int pathIndex, const std::string& assetId, int tile) {
    if (pathIndex < 0 || pathIndex >= static_cast<int>(track_.paths.size())) return false;
    if (!track_.textureAssets.count(assetId)) return false;
    Path& path = track_.paths[pathIndex];
    if (path.texture && path.texture->assetId == assetId && path.texture->tile == tile) return false;
    history_.push(track_);
    path.texture = TextureBinding{assetId, tile};
    return true;
  }

  // Mirrors clearCurrentCurveTexture.
  bool clearPathTexture(int pathIndex) {
    if (pathIndex < 0 || pathIndex >= static_cast<int>(track_.paths.size())) return false;
    Path& path = track_.paths[pathIndex];
    if (!path.texture) return false;
    history_.push(track_);
    path.texture.reset();
    return true;
  }

  bool deleteSelectedMesh() {
    if (!selectedMeshId_.has_value()) return false;
    const auto it = std::find_if(track_.meshes.begin(), track_.meshes.end(),
                                 [&](const MeshPlacement& m) { return m.id == *selectedMeshId_; });
    if (it == track_.meshes.end()) return false;
    history_.push(track_);
    track_.meshes.erase(it);
    selectedMeshId_.reset();
    return true;
  }

  // Wholesale replacement, e.g. loading a file: clears interaction state that no longer refers to
  // anything meaningful in the new track (mirrors setEditMode's own resets). Does NOT touch
  // history -- callers that want the old state to remain undoable should push() it first.
  void replaceTrack(TrackDefinition replacement) { replaceTrackKeepHistory(std::move(replacement)); }

  // One pushUndo() per drag gesture, on the first actual mutation (mirrors dragMutated).
  void beginDrag() {
    dragging_ = true;
    dragMutated_ = false;
  }

  void dragSelectedTo(double worldX, double worldZ) {
    if (!dragging_ || !selectionInRange()) return;
    if (!dragMutated_) {
      history_.push(track_);
      dragMutated_ = true;
    }
    TrackPoint& point = track_.paths[selection_.pathIndex].points[selection_.pointIndex];
    point.pos.x = std::round(worldX * 10.0) / 10.0;
    point.pos.z = std::round(worldZ * 10.0) / 10.0;
  }

  // Elevation-view counterpart to dragSelectedTo: same point, same drag lifecycle
  // (beginDrag/endDrag/one-push-per-gesture), different axis -- mirrors editor.js's
  // dragging === 'elev' branch (curPoint().pos[1] = ...), which shares the same `dragging` state
  // machine the top-down view's x/z drag uses.
  void dragSelectedElevationTo(double y) {
    if (!dragging_ || !selectionInRange()) return;
    if (!dragMutated_) {
      history_.push(track_);
      dragMutated_ = true;
    }
    track_.paths[selection_.pathIndex].points[selection_.pointIndex].pos.y = std::round(y * 10.0) / 10.0;
  }

  void endDrag() {
    dragging_ = false;
    dragMutated_ = false;
  }

  // Direct index-based selection, for callers (the elevation view) that already know exactly
  // which point they hit rather than needing a world-space radius search.
  void selectPoint(int pathIndex, int pointIndex) {
    selection_ = {pathIndex, pointIndex};
    selectedMeshId_.reset();
  }

  // Mirrors deleteSelected() for a position point (refuses to drop a path below 4, since a track
  // path needs that many to bake) generalized to also delete a selected roll/width/crossSection
  // point (EDITOR_PARITY_FIXES.md gap 1), which JS handles separately (the rollSel/widthSel/
  // crossSectionSel branches of renderProps()'s Delete buttons, js/editor.js:2383/2416/2449) with
  // no minimum-count guard at all -- a path can have zero roll/width/crossSection points; core's
  // baker treats that as flat/default-width/uncurved. No shared/disjoint-id guard -- this editor
  // doesn't alias points by id yet (see EditorTrackDefinition.hpp). Preserves track_.start across
  // the deletion (mirrors deleteSelected()'s preserveStartPoint call, EDITOR_PARITY_FIXES.md
  // finding 4) -- harmless no-op when the deleted point isn't a/the position point start refers to.
  bool deleteSelectedPoint() {
    if (!selectionInRange()) return false;
    Path& path = track_.paths[selection_.pathIndex];
    const PointKind kind = path.points[selection_.pointIndex].kind;
    if (kind == PointKind::Position) {
      const auto positionCount =
          std::count_if(path.points.begin(), path.points.end(), [](const TrackPoint& p) { return p.kind == PointKind::Position; });
      if (positionCount <= 4) return false;
    }

    const std::string startPointId = currentStartPointId();
    history_.push(track_);
    path.points.erase(path.points.begin() + selection_.pointIndex);
    selection_ = {};
    preserveStartPoint(startPointId);
    return true;
  }

  // ---- Roll/width/cross-section point editing (EDITOR_PARITY_FIXES.md gap 1) ----
  //
  // Scoped to add/edit-fields/delete via a properties panel (PropertiesPanel.hpp/.cpp), NOT the
  // draggable-handle-on-canvas interaction js/editor.js's top-down and elevation views offer
  // (rollHandleAtTop/widthHandleAtTop/crossSectionHandleAtTop/rollHandleAtElev) -- that needs
  // deriving a screen-space handle position from an arbitrary t along the baked centerline, which
  // is materially more work than the schema-authoring capability itself. This still makes
  // banking/width/cross-section fully authorable; on-canvas dragging remains a gap.

  TrackPoint* mutablePointAt(int pathIndex, int pointIndex) {
    if (pathIndex < 0 || pathIndex >= static_cast<int>(track_.paths.size())) return nullptr;
    auto& points = track_.paths[pathIndex].points;
    if (pointIndex < 0 || pointIndex >= static_cast<int>(points.size())) return nullptr;
    return &points[pointIndex];
  }

  // Appends a new roll/width/crossSection point to `pathIndex` at parameter `t`, using the schema
  // defaults every TrackPoint already carries (roll 0, width 36, curvature 0/tightness 1/thickness
  // 4 -- js/editor.js's insertRollPointAtWorld/insertWidthPoint/insertCrossSectionPoint instead
  // interpolate the curve's *current* value at that t so a fresh point doesn't visibly kink the
  // curve; this editor doesn't, so a newly added point may need its value adjusted immediately).
  // Selects the new point and returns its raw index, or nullopt if pathIndex is invalid or `kind`
  // is Position (use finishCreateDraft/createModeClick for that).
  std::optional<int> addAuxPoint(int pathIndex, PointKind kind, double t) {
    if (pathIndex < 0 || pathIndex >= static_cast<int>(track_.paths.size()) || kind == PointKind::Position) return std::nullopt;
    history_.push(track_);
    TrackPoint point;
    point.kind = kind;
    point.t = std::clamp(t, 0.0, 1.0);
    Path& path = track_.paths[pathIndex];
    path.points.push_back(point);
    const int index = static_cast<int>(path.points.size()) - 1;
    selectPoint(pathIndex, index);
    return index;
  }

  // Mutates the selected point's roll/width/crossSection fields (t plus whichever value fields the
  // point's own kind uses) via `mutate`, pushing one undo step first and re-clamping afterward to
  // the same bounds normalizePath() enforces on load. No-op (returns false, no history push) for a
  // Position point or an out-of-range selection -- use setSelectedPositionFields for the former.
  template <typename Mutate>
  bool editAuxPoint(int pathIndex, int pointIndex, Mutate&& mutate) {
    TrackPoint* point = mutablePointAt(pathIndex, pointIndex);
    if (!point || point->kind == PointKind::Position) return false;
    history_.push(track_);
    mutate(*point);
    point->t = std::clamp(point->t, 0.0, 1.0);
    if (point->kind == PointKind::Roll) point->roll = std::clamp(point->roll, -180.0, 180.0);
    if (point->kind == PointKind::Width) point->width = std::max(1.0, point->width);
    if (point->kind == PointKind::CrossSection) {
      point->curvature = std::clamp(point->curvature, -1.0, 1.0);
      point->tightness = std::clamp(point->tightness, 0.2, 4.0);
      point->thickness = std::max(0.0, point->thickness);
    }
    return true;
  }

  // Numeric-field counterpart to dragSelectedTo/dragSelectedElevationTo for a Position point, for
  // the properties panel's typed X/Y/Z/Weight inputs rather than a canvas drag.
  bool setSelectedPositionFields(double x, double y, double z, double weight) {
    if (!selectionInRange()) return false;
    TrackPoint& point = track_.paths[selection_.pathIndex].points[selection_.pointIndex];
    if (point.kind != PointKind::Position) return false;
    history_.push(track_);
    point.pos = tox::Vec3(x, y, z);
    point.weight = std::max(0.01, weight);
    return true;
  }

  // Track name (EDITOR_PARITY_FIXES.md gap 2). js/editor.js's #nameInput commits every keystroke
  // live, collapsing a whole typing session into one undo step (nameHistoryArmed,
  // js/editor.js:3637-3644); this instead commits once when the caller's text field is deactivated
  // after an edit (ImGui::IsItemDeactivatedAfterEdit), the same single-commit-per-session outcome
  // without needing focus/blur state threaded in from the caller -- consistent with every other
  // typed-field setter in this file (setSelectedPositionFields, editAuxPoint), which all commit
  // once rather than living-sync every keystroke. An empty name is accepted here (matches
  // track.name = e.target.value having no fallback); only serialization falls back to "Untitled
  // Track" (toJson), same as serializeTrack's `track.name || 'Untitled Track'`.
  bool setTrackName(const std::string& name) {
    if (name == track_.name) return false;
    history_.push(track_);
    track_.name = name;
    return true;
  }

  // Create mode: click adds a point to the in-progress draft, unless the click lands on the
  // draft's first point (closes as a closed path) or last point (finishes as open) -- mirrors
  // createModeClick/finishCreateDraft. Returns true if the draft was just finished into a new
  // path (the caller may want to switch back to Edit mode, matching setEditMode('edit') in JS).
  bool createModeClick(double worldX, double worldZ, double pickRadiusWorld) {
    if (!createDraft_.empty()) {
      if (withinPick(createDraft_.front(), worldX, worldZ, pickRadiusWorld)) return finishCreateDraft(true);
      if (createDraft_.size() > 1 && withinPick(createDraft_.back(), worldX, worldZ, pickRadiusWorld)) return finishCreateDraft(false);
    }
    createDraft_.emplace_back(std::round(worldX * 10.0) / 10.0, 0.0, std::round(worldZ * 10.0) / 10.0);
    return false;
  }

  void cancelCreateDraft() { createDraft_.clear(); }

 private:
  void replaceTrackKeepHistory(TrackDefinition replacement) {
    track_ = std::move(replacement);
    backfillPointIds(track_);  // see the constructor's comment on why this must never be skipped
    selection_ = {};
    dragging_ = false;
    dragMutated_ = false;
    createDraft_.clear();
    selectedMeshId_.reset();
    meshDragging_ = meshDragMutated_ = meshRotating_ = meshRotateMutated_ = false;
    selectedRail_.reset();
  }

  MeshPlacement* mutableSelectedMeshPlacement() {
    if (!selectedMeshId_.has_value()) return nullptr;
    for (auto& placement : track_.meshes)
      if (placement.id == *selectedMeshId_) return &placement;
    return nullptr;
  }

  // Mirrors TrackMesh.uniqueAssetId, ported for texture asset ids: strip a trailing extension,
  // collapse runs of non-alphanumeric/underscore/hyphen characters to a single hyphen, trim
  // leading/trailing hyphens, lowercase, then dedupe against existing ids with a "-2", "-3", ...
  // suffix.
  static std::string sanitizeAssetId(const std::string& filename) {
    std::string base = filename.empty() ? "texture" : filename;
    const auto dot = base.find_last_of('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    std::string out;
    bool lastWasSep = false;
    for (char c : base) {
      const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
      if (ok) {
        out += c;
        lastWasSep = false;
      } else if (!lastWasSep) {
        out += '-';
        lastWasSep = true;
      }
    }
    const auto startIdx = out.find_first_not_of('-');
    if (startIdx == std::string::npos) return "texture";
    const auto endIdx = out.find_last_not_of('-');
    out = out.substr(startIdx, endIdx - startIdx + 1);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out.empty() ? "texture" : out;
  }

  std::string uniqueTextureAssetId(const std::string& filename) const {
    const std::string base = sanitizeAssetId(filename);
    if (!track_.textureAssets.count(base)) return base;
    for (int i = 2;; ++i) {
      std::string candidate = base + "-" + std::to_string(i);
      if (!track_.textureAssets.count(candidate)) return candidate;
    }
  }

  // Mirrors TrackMesh.uniqueAssetId: same sanitizeAssetId scheme as texture ids, deduped against
  // mesh asset ids instead -- a re-import of the same file always yields a fresh asset rather than
  // disturbing existing placements.
  std::string uniqueMeshAssetId(const std::string& filename) const {
    const std::string base = sanitizeAssetId(filename);
    if (!track_.meshAssets.count(base)) return base;
    for (int i = 2;; ++i) {
      std::string candidate = base + "-" + std::to_string(i);
      if (!track_.meshAssets.count(candidate)) return candidate;
    }
  }

  // SelectedPoint::valid() only checks that both indices are non-negative, not that they're
  // in-range for the CURRENT track -- every structural mutation today happens to clear the
  // selection first, so this was unreachable, but it was a latent out-of-bounds write one edit
  // away (EDITOR_PARITY_FIXES.md finding 12). Mutating methods that index by selection_ should use
  // this instead of selection_.valid() directly.
  bool selectionInRange() const {
    return selection_.pathIndex >= 0 && selection_.pathIndex < static_cast<int>(track_.paths.size()) && selection_.pointIndex >= 0 &&
           selection_.pointIndex < static_cast<int>(track_.paths[selection_.pathIndex].points.size());
  }

  // track_.start.point is a POSITION-only index (matches core's TrackDefinition.hpp/StartGrid.cpp:
  // clamped against positionCount, not points.size()) -- unlike SelectedPoint::pointIndex, which is
  // a raw index into Path::points (mixed position/roll/width/crossSection). These two convert
  // between them; mirrors js/editor.js's positionIndices()/parts().controlPoints indexing split.
  static int positionIndexToRaw(const Path& path, int positionIndex) {
    int seen = -1;
    for (int i = 0; i < static_cast<int>(path.points.size()); ++i) {
      if (path.points[i].kind != PointKind::Position) continue;
      if (++seen == positionIndex) return i;
    }
    return -1;
  }

  // Mirrors startPointObject(): the id of the point track_.start currently refers to, or empty if
  // start is out of range or the track has no paths.
  std::string currentStartPointId() const {
    if (track_.start.path < 0 || track_.start.path >= static_cast<int>(track_.paths.size())) return {};
    const Path& path = track_.paths[track_.start.path];
    const int rawIndex = positionIndexToRaw(path, track_.start.point);
    return rawIndex >= 0 ? path.points[rawIndex].id : std::string();
  }

  // Mirrors preserveStartPoint(): re-finds the point that used to be at track_.start by id (first
  // match, scanning paths in authored order -- same as findPointOccurrence) so a structural edit
  // doesn't leave start silently pointing at a different physical point. Falls back to clampStart()
  // when the point is gone (EDITOR_PARITY_FIXES.md finding 4).
  void preserveStartPoint(const std::string& startPointId) {
    if (!startPointId.empty()) {
      for (int pi = 0; pi < static_cast<int>(track_.paths.size()); ++pi) {
        int posIdx = -1;
        for (const auto& point : track_.paths[pi].points) {
          if (point.kind != PointKind::Position) continue;
          ++posIdx;
          if (point.id == startPointId) {
            track_.start.path = pi;
            track_.start.point = posIdx;
            return;
          }
        }
      }
    }
    clampStart();
  }

  // Mirrors clampStart(): keeps track_.start's indices in range after paths/points are added or
  // removed. Does not try to track "the same" point through a restructure that has no id match --
  // same caveat as the JS original.
  void clampStart() {
    if (track_.paths.empty()) {
      track_.start.path = 0;
      track_.start.point = 0;
      return;
    }
    track_.start.path = std::clamp(track_.start.path, 0, static_cast<int>(track_.paths.size()) - 1);
    const Path& path = track_.paths[track_.start.path];
    const int positionCount =
        static_cast<int>(std::count_if(path.points.begin(), path.points.end(), [](const TrackPoint& p) { return p.kind == PointKind::Position; }));
    track_.start.point = std::clamp(track_.start.point, 0, std::max(0, positionCount - 1));
  }

  // Scans for the first unused "<prefix><N>" id starting at N=1, collision-proof by construction
  // (mirrors js/editor.js's newId/newMeshPlacementId -- see EDITOR_PARITY_FIXES.md finding 1,
  // which replaced an ever-incrementing-but-never-seeded counter that collided with ids already
  // present in a loaded track). `reserved` lets a caller mint several ids in one call before any of
  // them exist on track_ yet (see finishCreateDraft).
  static std::string firstUnusedId(const std::string& prefix, const std::set<std::string>& used) {
    for (int i = 1;; ++i) {
      std::string candidate = prefix + std::to_string(i);
      if (!used.count(candidate)) return candidate;
    }
  }

  std::string newPathId() const {
    std::set<std::string> used;
    for (const auto& path : track_.paths) used.insert(path.id);
    return firstUnusedId("path", used);
  }

  std::string newPointId(const std::set<std::string>& reserved = {}) const {
    std::set<std::string> used = reserved;
    for (const auto& path : track_.paths)
      for (const auto& point : path.points)
        if (point.kind == PointKind::Position && !point.id.empty()) used.insert(point.id);
    return firstUnusedId("p", used);
  }

  // "m" prefix (not "mesh") mirrors js/editor.js's newMeshPlacementId exactly
  // (EDITOR_PARITY_FIXES.md finding 11).
  std::string newMeshPlacementId() const {
    std::set<std::string> used;
    for (const auto& placement : track_.meshes) used.insert(placement.id);
    return firstUnusedId("m", used);
  }

  // Mirrors TrackMesh.railBoundaryEdges: an edge is on the region's rim exactly when a single
  // polygon claims it (two owners means an interior seam, zero means dangling geometry). Counted
  // by directed-edge occurrence across every polygon's loop rather than a live Willpower mesh's
  // edge->polygon backrefs, since editor::MeshAsset (unlike wp::geometry::Mesh) doesn't retain
  // those -- equivalent as long as no polygon lists the same edge twice, which a valid mesh export
  // never does.
  static void railBoundaryEdgesOf(MeshAsset& asset) {
    std::map<int, int> ownerCount;
    for (const auto& polygon : asset.polygons)
      for (const auto& directed : polygon.edges) ++ownerCount[directed.edge];
    for (auto& edge : asset.edges)
      if (ownerCount[edge.id] == 1) edge.rail = true;
  }

  static bool withinPick(const tox::Vec3& p, double worldX, double worldZ, double pickRadiusWorld) {
    const double dx = p.x - worldX, dz = p.z - worldZ;
    return (dx * dx + dz * dz) <= pickRadiusWorld * pickRadiusWorld;
  }

  std::optional<SelectedPoint> hitTestPosition(double worldX, double worldZ, double pickRadiusWorld) const {
    for (int pi = 0; pi < static_cast<int>(track_.paths.size()); ++pi) {
      const auto& points = track_.paths[pi].points;
      for (int i = 0; i < static_cast<int>(points.size()); ++i) {
        if (points[i].kind != PointKind::Position) continue;
        if (withinPick(points[i].pos, worldX, worldZ, pickRadiusWorld)) return SelectedPoint{pi, i};
      }
    }
    return std::nullopt;
  }

  // Mirrors flatRollWidthDefaults(): a freshly created path needs synthetic roll/width/
  // cross-section endpoints too, since (per EDITOR_CPP_PORT_PLAN.md M1) fromJson/toJson never
  // synthesize them on the editor's behalf the way core's loader does.
  static void appendDefaultAuxPoints(Path& path) {
    const double endT = path.closed ? 0.5 : 1.0;
    for (double t : {0.0, endT}) {
      TrackPoint roll;
      roll.kind = PointKind::Roll;
      roll.t = t;
      path.points.push_back(roll);
    }
    for (double t : {0.0, endT}) {
      TrackPoint width;
      width.kind = PointKind::Width;
      width.t = t;
      width.width = 36.0;
      path.points.push_back(width);
    }
    for (double t : {0.0, endT}) {
      TrackPoint crossSection;
      crossSection.kind = PointKind::CrossSection;
      crossSection.t = t;
      path.points.push_back(crossSection);
    }
  }

  bool finishCreateDraft(bool closed) {
    if (createDraft_.size() < 4) {
      createDraft_.clear();  // matches the JS alert-and-bail; an editor-only guard, not a schema rule
      return false;
    }
    history_.push(track_);
    Path path;
    path.id = newPathId();
    path.closed = closed;
    // Points minted in this same loop aren't in track_ yet, so newPointId's scan can't see them --
    // `reserved` tracks ids minted so far this call so two points in one draft can never collide
    // with each other, only with what's already on the track (mirrors js/editor.js's newId, which
    // has the same problem solved by a single shared, ever-advancing counter -- see
    // EDITOR_PARITY_FIXES.md finding 1).
    std::set<std::string> reserved;
    for (const auto& pos : createDraft_) {
      TrackPoint point;
      point.kind = PointKind::Position;
      point.id = newPointId(reserved);
      reserved.insert(point.id);
      point.pos = pos;
      path.points.push_back(point);
    }
    // Position points occupy raw indices [0, draft size) -- appendDefaultAuxPoints only appends
    // roll/width/crossSection points after them, so the last-position index must be captured now.
    const int lastPositionIndex = static_cast<int>(path.points.size()) - 1;
    appendDefaultAuxPoints(path);
    createDraft_.clear();
    track_.paths.push_back(std::move(path));
    selection_ = {static_cast<int>(track_.paths.size()) - 1, closed ? 0 : lastPositionIndex};
    return true;
  }

  TrackDefinition track_;
  History history_;
  EditMode mode_{EditMode::Edit};
  SelectedPoint selection_;
  bool dragging_{false};
  bool dragMutated_{false};
  std::vector<tox::Vec3> createDraft_;

  std::optional<std::string> selectedMeshId_;
  bool meshDragging_{false}, meshDragMutated_{false};
  double meshDragOffsetX_{0.0}, meshDragOffsetZ_{0.0};
  bool meshRotating_{false}, meshRotateMutated_{false};
  double meshRotateOriginRotation_{0.0}, meshRotateStartAngle_{0.0};

  std::optional<SelectedRail> selectedRail_;
};

}  // namespace editor
