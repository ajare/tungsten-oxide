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
  const std::optional<std::string>& selectedZoneId() const { return selectedZoneId_; }
  const std::optional<std::string>& selectedTriggerId() const { return selectedTriggerId_; }

  // ---- Curve management (EDITOR_PARITY_FIXES.md gap 5) ----
  //
  // "Current curve": mirrors js/editor.js's `sel.path`, which the curve-selector dropdown sets
  // directly and a control-point click overrides (selectPositionAt/selectPoint always win while a
  // point is selected; the dropdown only matters once nothing is). explicitCurrentPathIndex_ holds
  // the dropdown's own choice, clamped to the track's current path count.
  int currentPathIndex() const {
    if (track_.paths.empty()) return 0;
    if (selection_.valid() && selection_.pathIndex >= 0 && selection_.pathIndex < static_cast<int>(track_.paths.size()))
      return selection_.pathIndex;
    return std::clamp(explicitCurrentPathIndex_, 0, static_cast<int>(track_.paths.size()) - 1);
  }

  void setCurrentPathIndex(int index) {
    explicitCurrentPathIndex_ = track_.paths.empty() ? 0 : std::clamp(index, 0, static_cast<int>(track_.paths.size()) - 1);
  }

  const std::vector<Connection>& junctions() const { return track_.junctions; }
  const std::vector<Connection>& disjointSeams() const { return track_.disjointSeams; }

  const Zone* findZone(const std::string& id) const {
    for (const auto& zone : track_.zones)
      if (zone.id == id) return &zone;
    return nullptr;
  }

  const Trigger* findTrigger(const std::string& id) const {
    for (const auto& trigger : track_.triggers)
      if (trigger.id == id) return &trigger;
    return nullptr;
  }

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
    // Points/mesh regions/zones/triggers share one selection (props panel), mirrors
    // clearMeshSelection() also clearing selectedZoneId/selectedTriggerId in js/editor.js.
    selectedMeshId_.reset();
    selectedZoneId_.reset();
    selectedTriggerId_.reset();
    return true;
  }

  void clearSelection() { selection_ = {}; }

  // ---- Mesh placements (EDITOR_CPP_PORT_PLAN.md M4) ----

  void selectMesh(const std::string& placementId) {
    selectedMeshId_ = placementId;
    selection_ = {};
    selectedZoneId_.reset();
    selectedTriggerId_.reset();
  }

  void clearMeshSelection() { selectedMeshId_.reset(); }

  // ---- Zones (EDITOR_PARITY_FIXES.md gap 3) ----
  //
  // Scoped like gap 1: full add/edit-fields/delete via a dedicated panel (ZonesPanel.hpp/.cpp),
  // plus on-canvas rendering and click-to-select (reusing core's own baked tox::Zone records --
  // see TopDownCanvas.cpp's zoneOutlineWorld/zoneAtWorld), but NOT js/editor.js's on-canvas drag
  // (`dragging === 'zoneTop'`), which continuously re-projects the mouse onto the nearest path via
  // TrackCore.makeEvaluator -- there is no equivalent spline evaluator exposed to cpp/editor (core
  // keeps its own Evaluator private to TrackBake.cpp), so reproducing that exactly would mean
  // porting or exposing one. Only path-hosted zone creation is wired up in the panel (the common
  // case: boost pads/start grids on a driven path); mesh-hosted zones can still be loaded, viewed,
  // selected and edited, just not created from scratch here.

  void selectZone(const std::string& id) {
    selectedZoneId_ = id;
    selection_ = {};
    selectedMeshId_.reset();
    selectedTriggerId_.reset();
  }

  void clearZoneSelection() { selectedZoneId_.reset(); }

  // Adds a path-hosted zone at parameter `t` along `pathIndex` with schema defaults (width 24,
  // length 40, and factor 1.5/duration 2 if boost -- TrackDefinition.hpp's own field defaults,
  // which already match TrackCore.DEFAULT_ZONE_WIDTH/LENGTH/DEFAULT_BOOST_FACTOR/DURATION exactly).
  // `effect` must be "velocityChange" (boost) or "startGrid"; anything else is treated as boost,
  // mirroring addZoneAt's own `effect === 'startGrid' ? 'startGrid' : 'velocityChange'` coercion.
  // Selects the new zone and returns its id, or nullopt if pathIndex is invalid.
  std::optional<std::string> addPathZone(int pathIndex, const std::string& effect, double t, double lateral) {
    if (pathIndex < 0 || pathIndex >= static_cast<int>(track_.paths.size())) return std::nullopt;
    history_.push(track_);
    Zone zone;
    zone.id = newZoneId();
    zone.effect = effect == "startGrid" ? "startGrid" : "velocityChange";
    zone.host.kind = "path";
    zone.host.pathId = track_.paths[pathIndex].id;
    zone.host.t = std::clamp(t, 0.0, 1.0);
    zone.host.lateral = lateral;
    track_.zones.push_back(std::move(zone));
    const std::string zoneId = track_.zones.back().id;
    selectZone(zoneId);
    return zoneId;
  }

  // Mutates the zone by id via `mutate`, pushing one undo step first and re-clamping afterward to
  // the same bounds normalize() enforces on load (mirrors editAuxPoint's pattern for points).
  template <typename Mutate>
  bool editZone(const std::string& id, Mutate&& mutate) {
    const auto it = std::find_if(track_.zones.begin(), track_.zones.end(), [&](const Zone& z) { return z.id == id; });
    if (it == track_.zones.end()) return false;
    history_.push(track_);
    mutate(*it);
    it->width = std::max(0.5, it->width);
    it->length = std::max(0.5, it->length);
    if (it->effect == "velocityChange") {
      it->factor = std::clamp(it->factor, 0.1, 5.0);
      it->duration = std::clamp(it->duration, 0.1, 30.0);
    }
    if (it->host.kind == "path") it->host.t = std::clamp(it->host.t, 0.0, 1.0);
    return true;
  }

  bool deleteSelectedZone() {
    if (!selectedZoneId_.has_value()) return false;
    const auto it = std::find_if(track_.zones.begin(), track_.zones.end(), [&](const Zone& z) { return z.id == *selectedZoneId_; });
    if (it == track_.zones.end()) return false;
    history_.push(track_);
    track_.zones.erase(it);
    selectedZoneId_.reset();
    return true;
  }

  // ---- Triggers (EDITOR_PARITY_FIXES.md gap 4) ----
  //
  // Same scope reduction as zones (gap 3): full add/edit-fields/delete via a dedicated panel
  // (TriggersPanel.hpp/.cpp) plus on-canvas rendering and click-to-select, reusing core's own
  // baked tox::Trigger records. Unlike zones, core already bakes a trigger's complete world-space
  // gate frame (center/right/up/fwd/halfWidth/height) directly -- no centerline-interpolation
  // approximation is needed here at all (see TopDownCanvas.cpp's drawTriggers/triggerAtWorld).
  // NOT implemented: on-canvas drag (`dragging === 'triggerTop'`), for the same reason zone drag
  // isn't (it continuously re-projects onto the nearest path via a live spline evaluator that
  // isn't exposed to cpp/editor). Only path-hosted trigger creation is wired up in the panel;
  // mesh-hosted triggers can still be loaded, viewed, selected and edited.

  void selectTrigger(const std::string& id) {
    selectedTriggerId_ = id;
    selection_ = {};
    selectedMeshId_.reset();
    selectedZoneId_.reset();
  }

  void clearTriggerSelection() { selectedTriggerId_.reset(); }

  // Adds a path-hosted trigger at parameter `t` along `pathIndex` with schema defaults (width 40,
  // height 12 -- TrackDefinition.hpp's own field defaults, matching TrackCore.DEFAULT_TRIGGER_WIDTH/
  // HEIGHT). `type` must be "dummy" or "checkpoint"; anything else is treated as dummy, mirroring
  // addTriggerAt's own `type === 'checkpoint' ? 'checkpoint' : 'dummy'` coercion. A fresh checkpoint
  // starts as role "intermediate", same as addTriggerAt. Selects the new trigger and returns its
  // id, or nullopt if pathIndex is invalid.
  std::optional<std::string> addPathTrigger(int pathIndex, const std::string& type, double t) {
    if (pathIndex < 0 || pathIndex >= static_cast<int>(track_.paths.size())) return std::nullopt;
    history_.push(track_);
    Trigger trigger;
    trigger.id = newTriggerId();
    trigger.type = type == "checkpoint" ? "checkpoint" : "dummy";
    trigger.host.kind = "path";
    trigger.host.pathId = track_.paths[pathIndex].id;
    trigger.host.t = std::clamp(t, 0.0, 1.0);
    if (trigger.type == "checkpoint") trigger.role = "intermediate";
    track_.triggers.push_back(std::move(trigger));
    const std::string triggerId = track_.triggers.back().id;
    selectTrigger(triggerId);
    return triggerId;
  }

  // Mutates the trigger by id via `mutate`, pushing one undo step first and re-clamping afterward.
  // Also mirrors setTriggerRole's finish-uniqueness invariant (js/editor.js:2318-2323): if `mutate`
  // leaves this trigger's role as "finish", any other checkpoint currently marked finish is demoted
  // to "intermediate" so at most one finish ever exists (harmless to re-run when nothing changed).
  template <typename Mutate>
  bool editTrigger(const std::string& id, Mutate&& mutate) {
    const auto it = std::find_if(track_.triggers.begin(), track_.triggers.end(), [&](const Trigger& t) { return t.id == id; });
    if (it == track_.triggers.end()) return false;
    history_.push(track_);
    mutate(*it);
    it->width = std::max(0.5, it->width);
    it->height = std::max(0.5, it->height);
    if (it->direction != "forward" && it->direction != "backward") it->direction = "both";
    if (it->type == "checkpoint") {
      if (it->role != "finish" && it->role != "intermediate") it->role = "intermediate";
      if (it->role == "finish") {
        for (auto& other : track_.triggers)
          if (&other != &*it && other.type == "checkpoint" && other.role == "finish") other.role = "intermediate";
      }
    } else {
      it->role.clear();
    }
    if (it->host.kind == "path") it->host.t = std::clamp(it->host.t, 0.0, 1.0);
    return true;
  }

  // Mirrors deleteSelectedTrigger's finish-role guard: a checkpoint marked "finish" can't be
  // deleted until another is promoted first, since a track needs exactly one finish trigger for
  // lap detection. Returns false (no-op) rather than the JS alert() when blocked.
  bool deleteSelectedTrigger() {
    if (!selectedTriggerId_.has_value()) return false;
    const auto it =
        std::find_if(track_.triggers.begin(), track_.triggers.end(), [&](const Trigger& t) { return t.id == *selectedTriggerId_; });
    if (it == track_.triggers.end()) return false;
    if (it->type == "checkpoint" && it->role == "finish") return false;
    history_.push(track_);
    track_.triggers.erase(it);
    selectedTriggerId_.reset();
    return true;
  }

  // Mirrors deleteSelectedCurve(): removes the current path, fixes track_.start, and prunes every
  // dangling zone/trigger/junction/disjoint-seam/self-intersection-override reference the deletion
  // left behind (mirrors removeStaleSeams()). Refuses to drop the last path -- a track needs at
  // least one curve. Returns false (no-op) if there's nothing to delete.
  bool deleteCurrentPath() {
    if (track_.paths.size() <= 1) return false;
    const int deleteIndex = currentPathIndex();
    if (deleteIndex < 0 || deleteIndex >= static_cast<int>(track_.paths.size())) return false;
    history_.push(track_);
    track_.paths.erase(track_.paths.begin() + deleteIndex);
    if (track_.start.path == deleteIndex) {
      track_.start.path = std::clamp(deleteIndex, 0, static_cast<int>(track_.paths.size()) - 1);
      track_.start.point = 0;
    } else if (track_.start.path > deleteIndex) {
      --track_.start.path;
    }
    pruneStaleReferences();
    selection_ = {};
    selectedZoneId_.reset();
    selectedTriggerId_.reset();
    explicitCurrentPathIndex_ = std::clamp(deleteIndex, 0, static_cast<int>(track_.paths.size()) - 1);
    clampStart();
    return true;
  }

  // ---- Connect/join (EDITOR_PARITY_FIXES.md gap 5) ----
  //
  // Endpoint-to-endpoint only -- mirrors performJoin()'s first two branches (same-path closes the
  // loop; different-path shares the target endpoint's identity and records a junction), but NOT
  // its third case (joining onto an INTERIOR point of an open path, which JS handles by splitting
  // the target path there first via splitTargetPathAt). That's out of scope here; connecting to an
  // existing curve's middle isn't offered by this panel. `pathA`/`pathB` must each be an OPEN path;
  // `aAtEnd`/`bAtEnd` pick which of that path's two endpoints (false = first point, true = last).
  bool joinPathEndpoints(int pathAIndex, bool aAtEnd, int pathBIndex, bool bAtEnd) {
    if (pathAIndex < 0 || pathAIndex >= static_cast<int>(track_.paths.size())) return false;
    if (pathBIndex < 0 || pathBIndex >= static_cast<int>(track_.paths.size())) return false;
    if (track_.paths[pathAIndex].closed || track_.paths[pathBIndex].closed) return false;
    if (hasDisjointSeamOnPath(track_.paths[pathAIndex].id) || hasDisjointSeamOnPath(track_.paths[pathBIndex].id)) return false;

    if (pathAIndex == pathBIndex) {
      if (aAtEnd == bAtEnd) return false;  // must be the path's two distinct endpoints
      history_.push(track_);
      track_.paths[pathAIndex].closed = true;
      selection_ = {};
      return true;
    }

    TrackPoint* sourcePoint = aAtEnd ? lastPositionMutable(track_.paths[pathAIndex]) : firstPositionMutable(track_.paths[pathAIndex]);
    TrackPoint* targetPoint = bAtEnd ? lastPositionMutable(track_.paths[pathBIndex]) : firstPositionMutable(track_.paths[pathBIndex]);
    if (!sourcePoint || !targetPoint) return false;
    const TrackPoint targetCopy = *targetPoint;

    history_.push(track_);
    TrackPoint* sourceSlot = aAtEnd ? lastPositionMutable(track_.paths[pathAIndex]) : firstPositionMutable(track_.paths[pathAIndex]);
    *sourceSlot = targetCopy;  // shares identity by copying the whole point (id included) -- mirrors replacePositionOccurrence
    Connection junction;
    junction.id = newConnectionId("j");
    junction.pointId = targetCopy.id;
    junction.sourcePathId = track_.paths[pathAIndex].id;
    junction.sourceEnd = aAtEnd ? "end" : "start";
    junction.targetPathId = track_.paths[pathBIndex].id;
    track_.junctions.push_back(std::move(junction));
    selection_ = {};
    return true;
  }

  // ---- Disjoint / reconnect (EDITOR_PARITY_FIXES.md gap 5) ----
  //
  // "Disjoint" splits a shared/smooth control point into a hard, unsmoothed seam -- mirrors
  // makeDisjoint(). The point ID itself stays shared (this is a smoothing annotation, not an
  // identity split): core's baker reads disjointSeams to skip tangent/roll continuity there
  // (TrackBake.cpp), so both sides remain physically coincident. Guarded like disjointDisabledReason:
  // refuses an already-disjoint open endpoint, and refuses an open-path split that would leave
  // fewer than 4 position points on either side.
  //
  // Unlike JS's rollWidthForSourceRange/sampleRollWidthForClosedReconnect/
  // sampleRollWidthFromJoinedPaths, a path rebuilt by makeDisjoint/reconnectDisjoint here does NOT
  // proportionally redistribute its roll/width/cross-section points from the original curve --
  // they're reset to schema defaults (appendDefaultAuxPoints, the same helper finishCreateDraft
  // uses). Banking/width authored before a split/reconnect is lost on the rebuilt path(s) and must
  // be re-entered via the Point Properties panel; this is the same "authoring capability over
  // pixel-perfect parity" scope reduction gaps 1/3/4 already document.
  bool makeDisjoint(int pathIndex, int pointIndex) {
    TrackPoint* point = mutablePointAt(pathIndex, pointIndex);
    if (!point || point->kind != PointKind::Position) return false;
    if (seamForPointId(point->id) != nullptr) return false;
    Path& path = track_.paths[pathIndex];
    const int positionIndex = rawIndexToPositionIndex(path, pointIndex);
    if (positionIndex < 0) return false;
    const int positionCount = static_cast<int>(
        std::count_if(path.points.begin(), path.points.end(), [](const TrackPoint& p) { return p.kind == PointKind::Position; }));
    if (!path.closed) {
      if (positionIndex == 0 || positionIndex == positionCount - 1) return false;  // already disjoint
      if (positionIndex + 1 < 4 || positionCount - positionIndex < 4) return false;
    }

    const std::string startPointId = currentStartPointId();
    const std::string pointId = point->id;
    history_.push(track_);
    Connection seam;
    seam.id = newConnectionId("seam");
    seam.pointId = pointId;

    Path& mutablePath = track_.paths[pathIndex];
    std::vector<TrackPoint> positions;
    for (const auto& p : mutablePath.points)
      if (p.kind == PointKind::Position) positions.push_back(p);

    if (mutablePath.closed) {
      std::rotate(positions.begin(), positions.begin() + positionIndex, positions.end());
      positions.push_back(positions.front());  // duplicate the seam point at both ends
      Path rebuilt;
      rebuilt.id = mutablePath.id;
      rebuilt.closed = false;
      rebuilt.points = std::move(positions);
      rebuilt.texture = mutablePath.texture;
      appendDefaultAuxPoints(rebuilt);
      seam.kind = "opened-closed";
      seam.pathId = rebuilt.id;
      mutablePath = std::move(rebuilt);
      selection_ = {pathIndex, 0};
    } else {
      std::set<std::string> usedPathIds;
      for (const auto& p : track_.paths) usedPathIds.insert(p.id);
      const std::string leftId = firstUnusedId("path", usedPathIds);
      usedPathIds.insert(leftId);
      const std::string rightId = firstUnusedId("path", usedPathIds);

      Path leftPath, rightPath;
      leftPath.id = leftId;
      leftPath.closed = false;
      leftPath.points.assign(positions.begin(), positions.begin() + positionIndex + 1);
      leftPath.texture = mutablePath.texture;
      appendDefaultAuxPoints(leftPath);
      rightPath.id = rightId;
      rightPath.closed = false;
      rightPath.points.assign(positions.begin() + positionIndex, positions.end());
      rightPath.texture = mutablePath.texture;
      appendDefaultAuxPoints(rightPath);

      seam.kind = "split-open";
      seam.leftPathId = leftId;
      seam.rightPathId = rightId;

      track_.paths.erase(track_.paths.begin() + pathIndex);
      track_.paths.insert(track_.paths.begin() + pathIndex, {leftPath, rightPath});
      selection_ = {pathIndex + 1, 0};
    }

    track_.disjointSeams.push_back(std::move(seam));
    preserveStartPoint(startPointId);
    pruneStaleReferences();
    return true;
  }

  // Removes a disjoint seam, restoring smooth continuity: closes the path again (opened-closed) or
  // re-merges the two split halves into one open path (split-open) -- mirrors reconnectDisjoint().
  // Returns false if the seam id doesn't exist or its recorded path(s)/endpoints no longer match
  // (mirrors seamIsValid's staleness checks).
  bool reconnectDisjoint(const std::string& seamId) {
    const auto seamIt = std::find_if(track_.disjointSeams.begin(), track_.disjointSeams.end(),
                                     [&](const Connection& s) { return s.id == seamId; });
    if (seamIt == track_.disjointSeams.end()) return false;
    const Connection seam = *seamIt;

    if (seam.kind == "opened-closed") {
      const auto pathIt = std::find_if(track_.paths.begin(), track_.paths.end(), [&](const Path& p) { return p.id == seam.pathId; });
      if (pathIt == track_.paths.end()) return false;
      std::vector<TrackPoint> positions;
      for (const auto& p : pathIt->points)
        if (p.kind == PointKind::Position) positions.push_back(p);
      if (positions.size() < 2 || positions.front().id != seam.pointId || positions.back().id != seam.pointId) return false;

      const std::string startPointId = currentStartPointId();
      history_.push(track_);
      Path& mutablePath = track_.paths[std::distance(track_.paths.begin(), pathIt)];
      positions.pop_back();  // drop the duplicated end -- front/back were the same point id
      Path rebuilt;
      rebuilt.id = mutablePath.id;
      rebuilt.closed = true;
      rebuilt.points = std::move(positions);
      rebuilt.texture = mutablePath.texture;
      appendDefaultAuxPoints(rebuilt);
      mutablePath = std::move(rebuilt);
      eraseDisjointSeamById(seamId);
      preserveStartPoint(startPointId);
      pruneStaleReferences();
      return true;
    }

    if (seam.kind == "split-open") {
      const auto leftIt = std::find_if(track_.paths.begin(), track_.paths.end(), [&](const Path& p) { return p.id == seam.leftPathId; });
      const auto rightIt = std::find_if(track_.paths.begin(), track_.paths.end(), [&](const Path& p) { return p.id == seam.rightPathId; });
      if (leftIt == track_.paths.end() || rightIt == track_.paths.end()) return false;
      const TrackPoint* leftLast = lastPosition(*leftIt);
      const TrackPoint* rightFirst = firstPosition(*rightIt);
      if (!leftLast || !rightFirst || leftLast->id != seam.pointId || rightFirst->id != seam.pointId) return false;

      std::vector<TrackPoint> merged;
      for (const auto& p : leftIt->points)
        if (p.kind == PointKind::Position) merged.push_back(p);
      bool skippedFirst = false;
      for (const auto& p : rightIt->points) {
        if (p.kind != PointKind::Position) continue;
        if (!skippedFirst) {
          skippedFirst = true;
          continue;  // drop the duplicated shared point
        }
        merged.push_back(p);
      }

      const std::string startPointId = currentStartPointId();
      history_.push(track_);
      Path mergedPath;
      mergedPath.id = leftIt->id;
      mergedPath.closed = false;
      mergedPath.points = std::move(merged);
      mergedPath.texture = leftIt->texture;
      appendDefaultAuxPoints(mergedPath);

      const int leftIndex = static_cast<int>(std::distance(track_.paths.begin(), leftIt));
      const int rightIndex = static_cast<int>(std::distance(track_.paths.begin(), rightIt));
      const int lo = std::min(leftIndex, rightIndex), hi = std::max(leftIndex, rightIndex);
      track_.paths.erase(track_.paths.begin() + hi);
      track_.paths[lo] = std::move(mergedPath);

      eraseDisjointSeamById(seamId);
      preserveStartPoint(startPointId);
      pruneStaleReferences();
      return true;
    }

    return false;
  }

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

  // `worldX`/`worldZ` here is already offset+snapped by the caller (mirrors editor.js computing
  // `moved = snapWorldXZ({x: w.x + offset.dx, z: w.z + offset.dz})` before assigning
  // placement.x/z) -- see meshDragOffsetX/Z below for the raw offset a caller needs to do that.
  void dragMeshTo(double worldX, double worldZ) {
    MeshPlacement* placement = mutableSelectedMeshPlacement();
    if (!meshDragging_ || !placement) return;
    if (!meshDragMutated_) {
      history_.push(track_);
      meshDragMutated_ = true;
    }
    placement->x = std::round(worldX * 10.0) / 10.0;
    placement->z = std::round(worldZ * 10.0) / 10.0;
  }

  double meshDragOffsetX() const { return meshDragOffsetX_; }
  double meshDragOffsetZ() const { return meshDragOffsetZ_; }

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
    selectedZoneId_.reset();
    selectedTriggerId_.reset();
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
    if (isWidth)
      asset.tileWidth = clamped;
    else
      asset.tileHeight = clamped;
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

  // ---- Segment selection/deletion/splitting; insert-point-on-segment (EDITOR_PARITY_FIXES.md
  // gap 11) ----
  //
  // `i` throughout is a POSITION-space index (0-based, counting only Position points on the
  // path) identifying the segment running from position i to position i+1 (wrapping for a closed
  // path) -- mirrors js/editor.js's segSel/{path,i} exactly, which is always derived this way
  // (positionIndices(path)[i]), never a raw Path::points index.
  //
  // Deliberately does NOT port editor.js's own click-to-select-a-segment (`segmentAtTop`) --
  // that function exists in JS but is never called from anywhere in js/editor.js itself (dead
  // code); the shipped UI only ever derives a segment from the *currently selected control
  // point* via selectedOutgoingSegment/selectedIncomingSegment below, so that's the only path
  // ported here too.
  struct SegmentRef {
    int pathIndex, i;
  };

  static int positionCount(const Path& path) {
    return static_cast<int>(std::count_if(path.points.begin(), path.points.end(), [](const TrackPoint& p) { return p.kind == PointKind::Position; }));
  }

  // The segment leaving the currently selected control point, or nullopt if nothing satisfies it
  // (no selection, an aux point selected, or an open path's last point). Mirrors
  // selectedOutgoingSegment() exactly, including that it returns null while a roll/width/
  // crossSection point is selected -- this port keeps all point kinds in one `selection_` rather
  // than JS's separate rollSel/widthSel/crossSectionSel variables, so that guard becomes "the
  // selected point must be a Position point".
  std::optional<SegmentRef> selectedOutgoingSegment() const {
    if (!selectionInRange()) return std::nullopt;
    const Path& path = track_.paths[selection_.pathIndex];
    if (path.points[selection_.pointIndex].kind != PointKind::Position) return std::nullopt;
    const int posIndex = rawIndexToPositionIndex(path, selection_.pointIndex);
    if (posIndex < 0) return std::nullopt;
    if (path.closed) return SegmentRef{selection_.pathIndex, posIndex};
    if (posIndex < positionCount(path) - 1) return SegmentRef{selection_.pathIndex, posIndex};
    return std::nullopt;
  }

  // The segment arriving at the currently selected control point. Mirrors
  // selectedIncomingSegment() exactly.
  std::optional<SegmentRef> selectedIncomingSegment() const {
    if (!selectionInRange()) return std::nullopt;
    const Path& path = track_.paths[selection_.pathIndex];
    if (path.points[selection_.pointIndex].kind != PointKind::Position) return std::nullopt;
    const int posIndex = rawIndexToPositionIndex(path, selection_.pointIndex);
    if (posIndex < 0) return std::nullopt;
    const int n = positionCount(path);
    if (path.closed) return SegmentRef{selection_.pathIndex, (posIndex - 1 + n) % n};
    if (posIndex > 0) return SegmentRef{selection_.pathIndex, posIndex - 1};
    return std::nullopt;
  }

  // Deletes the segment running from position `i` to position `i+1` (wrapping if closed) on
  // `pathIndex` -- mirrors deleteSegment(pi, i) exactly:
  //  - closed path: opens it by rotating the point array so the cut becomes the new start/end
  //    (no point removed -- opening a loop needs no duplicate endpoint, unlike makeDisjoint's
  //    opened-closed case, which marks a *smoothing seam* at a point that stays shared).
  //  - open path, first or last segment: shrinks by dropping that one endpoint (refused below the
  //    4-point floor).
  //  - open path, an interior segment: splits into two new open paths at the cut, with fresh
  //    default roll/width/crossSection points (refused if either half would drop below 4 points)
  //    -- same "authoring capability over pixel-perfect parity" tradeoff as makeDisjoint's split
  //    (no proportional roll/width redistribution).
  // Refuses (returns false, no history push) if the path carries a disjoint seam -- mirrors
  // deleteSegment's pathHasDisjointSeam guard: reconnect it first, same as editor.html asks.
  bool deleteSegmentAt(int pathIndex, int i) {
    if (pathIndex < 0 || pathIndex >= static_cast<int>(track_.paths.size())) return false;
    Path& path = track_.paths[pathIndex];
    if (hasDisjointSeamOnPath(path.id)) return false;
    std::vector<int> idxs;
    for (int k = 0; k < static_cast<int>(path.points.size()); ++k)
      if (path.points[k].kind == PointKind::Position) idxs.push_back(k);
    const int n = static_cast<int>(idxs.size());
    if (i < 0 || i >= n) return false;

    const std::string startPointId = currentStartPointId();

    if (path.closed) {
      history_.push(track_);
      const int cut = (i + 1) % n;
      std::vector<TrackPoint> posObjs;
      posObjs.reserve(n);
      for (int k : idxs) posObjs.push_back(path.points[k]);
      std::vector<TrackPoint> rotated;
      rotated.reserve(n);
      for (int k = cut; k < n; ++k) rotated.push_back(posObjs[k]);
      for (int k = 0; k < cut; ++k) rotated.push_back(posObjs[k]);
      for (int k = 0; k < n; ++k) path.points[idxs[k]] = rotated[k];
      path.closed = false;
    } else if (i == 0) {
      if (n - 1 < 4) return false;
      history_.push(track_);
      path.points.erase(path.points.begin() + idxs[0]);
    } else if (i == n - 2) {
      if (n - 1 < 4) return false;
      history_.push(track_);
      path.points.erase(path.points.begin() + idxs[n - 1]);
    } else {
      std::vector<TrackPoint> posObjs;
      posObjs.reserve(n);
      for (int k : idxs) posObjs.push_back(path.points[k]);
      std::vector<TrackPoint> left(posObjs.begin(), posObjs.begin() + i + 1);
      std::vector<TrackPoint> right(posObjs.begin() + i + 1, posObjs.end());
      if (static_cast<int>(left.size()) < 4 || static_cast<int>(right.size()) < 4) return false;
      history_.push(track_);
      std::set<std::string> usedPathIds;
      for (const auto& p : track_.paths) usedPathIds.insert(p.id);
      Path leftPath, rightPath;
      leftPath.id = firstUnusedId("path", usedPathIds);
      usedPathIds.insert(leftPath.id);
      rightPath.id = firstUnusedId("path", usedPathIds);
      leftPath.closed = false;
      rightPath.closed = false;
      leftPath.texture = path.texture;
      rightPath.texture = path.texture;
      leftPath.points = std::move(left);
      appendDefaultAuxPoints(leftPath);
      rightPath.points = std::move(right);
      appendDefaultAuxPoints(rightPath);
      track_.paths.erase(track_.paths.begin() + pathIndex);
      track_.paths.insert(track_.paths.begin() + pathIndex, {leftPath, rightPath});
    }

    selection_ = {};
    preserveStartPoint(startPointId);
    pruneStaleReferences();
    return true;
  }

  // Mirrors deleteSelectedSegment(which): derives the segment from the current selection via
  // selectedOutgoingSegment()/selectedIncomingSegment() and deletes it. Returns false (silent
  // no-op, no alert -- consistent with this editor's other disabled-button-instead-of-alert()
  // panel affordances) if no such segment exists.
  bool deleteSelectedSegment(bool outgoing) {
    const std::optional<SegmentRef> seg = outgoing ? selectedOutgoingSegment() : selectedIncomingSegment();
    if (!seg.has_value()) return false;
    return deleteSegmentAt(seg->pathIndex, seg->i);
  }

  // Inserts a new Position point into `pathIndex` at position-space index `insertAtPositionIndex`
  // (clamped to [0, positionCount]), mirroring insertNear's insertPositionAt/selectPosition --
  // used by the "Position" context-menu item (EDITOR_PARITY_FIXES.md gap 13's deferred item,
  // finished here since it needs this same segment-insertion machinery). Selects the new point
  // and returns its raw Path::points index, or nullopt if pathIndex is invalid.
  std::optional<int> insertPositionOnSegment(int pathIndex, int insertAtPositionIndex, double x, double y, double z) {
    if (pathIndex < 0 || pathIndex >= static_cast<int>(track_.paths.size())) return std::nullopt;
    history_.push(track_);
    Path& path = track_.paths[pathIndex];
    std::vector<int> idxs;
    for (int k = 0; k < static_cast<int>(path.points.size()); ++k)
      if (path.points[k].kind == PointKind::Position) idxs.push_back(k);
    const int n = static_cast<int>(idxs.size());
    const int clampedAt = std::clamp(insertAtPositionIndex, 0, n);
    const int rawAt = clampedAt < n ? idxs[clampedAt] : static_cast<int>(path.points.size());
    TrackPoint point;
    point.kind = PointKind::Position;
    point.id = newPointId();
    point.pos = tox::Vec3(x, y, z);
    point.weight = 1.0;
    path.points.insert(path.points.begin() + rawAt, point);
    selectPoint(pathIndex, rawAt);
    return rawAt;
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

  // Direction toggle (EDITOR_PARITY_FIXES.md gap 6), mirrors editor.html's #dirBtn handler:
  // clampStart() first (start may be stale from a prior structural edit), then flip the flag.
  void toggleStartReverse() {
    clampStart();
    history_.push(track_);
    track_.start.reverse = !track_.start.reverse;
  }

  // "Set as start point" (EDITOR_PARITY_FIXES.md gap 6), mirrors the Selected Point panel's
  // #startBtn: repoints track_.start at the current selection, keeping the existing reverse flag.
  // No-ops when the selection isn't a Position point or is already the start point, same as JS
  // disabling the button in that state.
  bool setStartPoint() {
    if (!selectionInRange()) return false;
    const TrackPoint& point = track_.paths[selection_.pathIndex].points[selection_.pointIndex];
    if (point.kind != PointKind::Position) return false;
    const int posIndex = rawIndexToPositionIndex(track_.paths[selection_.pathIndex], selection_.pointIndex);
    if (track_.start.path == selection_.pathIndex && track_.start.point == posIndex) return false;
    history_.push(track_);
    track_.start.path = selection_.pathIndex;
    track_.start.point = posIndex;
    return true;
  }

  // Whether (pathIndex, pointIndex) -- a raw Path::points index, matching SelectedPoint -- is the
  // current start point. Used by the properties panel to disable "Set as start point" once it
  // already is, mirroring editor.html's isStart/#startBtn.
  bool isStartPoint(int pathIndex, int pointIndex) const {
    if (pathIndex < 0 || pathIndex >= static_cast<int>(track_.paths.size())) return false;
    return track_.start.path == pathIndex && track_.start.point == rawIndexToPositionIndex(track_.paths[pathIndex], pointIndex);
  }

  // Handling panel (EDITOR_PARITY_FIXES.md gap 7), mirrors #handlingPanel's field-change handler:
  // clamps each field to the same ranges TrackCore.normalizeHandling/EditorTrackDefinition's
  // fromJson use (so a panel edit and a hand-edited JSON file converge on the same value), then
  // commits one undo step. Always pushes -- unlike setTrackName/setSelectedPositionFields there's
  // no cheap "did anything actually change" check worth doing across four fields, and JS's own
  // 'change' handler pushes unconditionally too.
  void setHandling(double maxSpeed, double accel, double turnSpeed, double weight) {
    history_.push(track_);
    track_.handling.maxSpeed = std::clamp(maxSpeed, 10.0, 1000.0);
    track_.handling.accel = std::clamp(accel, 5.0, 1000.0);
    track_.handling.turnSpeed = std::clamp(turnSpeed, 10.0, 720.0);
    track_.handling.weight = std::clamp(weight, 50.0, 100000.0);
  }

  // Mirrors #handlingResetBtn: restores TrackCore.DEFAULT_HANDLING (Handling{}'s own defaults).
  void resetHandling() {
    history_.push(track_);
    track_.handling = Handling{};
  }

  // Create mode: click adds a point to the in-progress draft, unless the click lands on the
  // draft's first point (closes as a closed path) or last point (finishes as open) -- mirrors
  // createModeClick/finishCreateDraft. Returns true if the draft was just finished into a new
  // path (the caller may want to switch back to Edit mode, matching setEditMode('edit') in JS).
  // `worldX`/`worldZ` is used for hit-testing against the draft's own closing/finishing points
  // (unsnapped, matching the raw click position); `snappedX`/`snappedZ` is what a genuinely new
  // point gets, letting the caller apply grid-snap (EDITOR_PARITY_FIXES.md gap 9) without it
  // fighting the pick tolerance on an already-placed draft point. Defaults to the raw click when
  // the caller has no snapping to apply.
  bool createModeClick(double worldX, double worldZ, double pickRadiusWorld, double snappedX, double snappedZ) {
    if (!createDraft_.empty()) {
      if (withinPick(createDraft_.front(), worldX, worldZ, pickRadiusWorld)) return finishCreateDraft(true);
      if (createDraft_.size() > 1 && withinPick(createDraft_.back(), worldX, worldZ, pickRadiusWorld)) return finishCreateDraft(false);
    }
    createDraft_.emplace_back(std::round(snappedX * 10.0) / 10.0, 0.0, std::round(snappedZ * 10.0) / 10.0);
    return false;
  }
  bool createModeClick(double worldX, double worldZ, double pickRadiusWorld) {
    return createModeClick(worldX, worldZ, pickRadiusWorld, worldX, worldZ);
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
    selectedZoneId_.reset();
    selectedTriggerId_.reset();
    explicitCurrentPathIndex_ = 0;
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

  // "z" prefix mirrors js/editor.js's newId('z') for zones.
  std::string newZoneId() const {
    std::set<std::string> used;
    for (const auto& zone : track_.zones) used.insert(zone.id);
    return firstUnusedId("z", used);
  }

  // "tr" prefix mirrors js/editor.js's newId('tr') for triggers.
  std::string newTriggerId() const {
    std::set<std::string> used;
    for (const auto& trigger : track_.triggers) used.insert(trigger.id);
    return firstUnusedId("tr", used);
  }

  // Shared id space for junctions ("j" prefix) and disjoint seams ("seam" prefix), mirroring
  // js/editor.js's newId('j')/newId('seam') -- each still scans only its own collection, since the
  // two record kinds never share an id namespace in JS either.
  std::string newConnectionId(const std::string& prefix) const {
    std::set<std::string> used;
    for (const auto& c : prefix == "j" ? track_.junctions : track_.disjointSeams) used.insert(c.id);
    return firstUnusedId(prefix, used);
  }

  static TrackPoint* firstPositionMutable(Path& path) {
    for (auto& p : path.points)
      if (p.kind == PointKind::Position) return &p;
    return nullptr;
  }

  static TrackPoint* lastPositionMutable(Path& path) {
    for (auto it = path.points.rbegin(); it != path.points.rend(); ++it)
      if (it->kind == PointKind::Position) return &*it;
    return nullptr;
  }

  static const TrackPoint* firstPosition(const Path& path) {
    for (const auto& p : path.points)
      if (p.kind == PointKind::Position) return &p;
    return nullptr;
  }

  static const TrackPoint* lastPosition(const Path& path) {
    for (auto it = path.points.rbegin(); it != path.points.rend(); ++it)
      if (it->kind == PointKind::Position) return &*it;
    return nullptr;
  }

  // Raw Path::points index -> position-only index, the inverse of positionIndexToRaw. -1 if
  // out of range or the point at that raw index isn't a Position point.
  static int rawIndexToPositionIndex(const Path& path, int rawIndex) {
    if (rawIndex < 0 || rawIndex >= static_cast<int>(path.points.size())) return -1;
    if (path.points[rawIndex].kind != PointKind::Position) return -1;
    int count = -1;
    for (int i = 0; i <= rawIndex; ++i)
      if (path.points[i].kind == PointKind::Position) ++count;
    return count;
  }

  bool hasDisjointSeamOnPath(const std::string& pathId) const {
    for (const auto& s : track_.disjointSeams)
      if (s.pathId == pathId || s.leftPathId == pathId || s.rightPathId == pathId) return true;
    return false;
  }

  const Connection* seamForPointId(const std::string& pointId) const {
    for (const auto& s : track_.disjointSeams)
      if (s.pointId == pointId) return &s;
    return nullptr;
  }

  void eraseDisjointSeamById(const std::string& seamId) {
    const auto it =
        std::find_if(track_.disjointSeams.begin(), track_.disjointSeams.end(), [&](const Connection& s) { return s.id == seamId; });
    if (it != track_.disjointSeams.end()) track_.disjointSeams.erase(it);
  }

  // Mirrors removeStaleSeams(): prunes every disjoint seam/junction/self-intersection-override/
  // zone/trigger whose referenced point or path/mesh no longer exists, then re-runs the
  // one-Finish invariant in case the checkpoint that used to be Finish was just pruned (mirrors
  // TrackCore.normalizeTriggers's call inside removeStaleSeams()). Called after any structural
  // edit that can orphan these (deleteCurrentPath, makeDisjoint, reconnectDisjoint).
  void pruneStaleReferences() {
    std::set<std::string> positionIds;
    for (const auto& path : track_.paths)
      for (const auto& point : path.points)
        if (point.kind == PointKind::Position && !point.id.empty()) positionIds.insert(point.id);

    auto seamValid = [&](const Connection& seam) {
      if (!positionIds.count(seam.pointId)) return false;
      if (seam.kind == "opened-closed") {
        const auto it = std::find_if(track_.paths.begin(), track_.paths.end(), [&](const Path& p) { return p.id == seam.pathId; });
        if (it == track_.paths.end()) return false;
        const int positionCount =
            static_cast<int>(std::count_if(it->points.begin(), it->points.end(), [](const TrackPoint& p) { return p.kind == PointKind::Position; }));
        const TrackPoint* first = firstPosition(*it);
        const TrackPoint* last = lastPosition(*it);
        return positionCount >= 2 && first != nullptr && last != nullptr && first->id == seam.pointId && last->id == seam.pointId;
      }
      if (seam.kind == "split-open") {
        const auto leftIt = std::find_if(track_.paths.begin(), track_.paths.end(), [&](const Path& p) { return p.id == seam.leftPathId; });
        const auto rightIt =
            std::find_if(track_.paths.begin(), track_.paths.end(), [&](const Path& p) { return p.id == seam.rightPathId; });
        if (leftIt == track_.paths.end() || rightIt == track_.paths.end()) return false;
        const TrackPoint* leftLast = lastPosition(*leftIt);
        const TrackPoint* rightFirst = firstPosition(*rightIt);
        return leftLast != nullptr && rightFirst != nullptr && leftLast->id == seam.pointId && rightFirst->id == seam.pointId;
      }
      return false;
    };
    track_.disjointSeams.erase(std::remove_if(track_.disjointSeams.begin(), track_.disjointSeams.end(),
                                              [&](const Connection& s) { return !seamValid(s); }),
                               track_.disjointSeams.end());

    track_.junctions.erase(
        std::remove_if(track_.junctions.begin(), track_.junctions.end(), [&](const Connection& j) { return !positionIds.count(j.pointId); }),
        track_.junctions.end());

    track_.selfIntersectionOverrides.erase(
        std::remove_if(track_.selfIntersectionOverrides.begin(), track_.selfIntersectionOverrides.end(),
                       [&](const SelfIntersectionOverride& o) { return !positionIds.count(o.a) || !positionIds.count(o.b); }),
        track_.selfIntersectionOverrides.end());

    std::set<std::string> pathIds, meshIds;
    for (const auto& p : track_.paths) pathIds.insert(p.id);
    for (const auto& m : track_.meshes) meshIds.insert(m.id);
    track_.zones.erase(std::remove_if(track_.zones.begin(), track_.zones.end(),
                                      [&](const Zone& z) {
                                        return z.host.kind == "mesh" ? !meshIds.count(z.host.meshId) : !pathIds.count(z.host.pathId);
                                      }),
                       track_.zones.end());
    track_.triggers.erase(std::remove_if(track_.triggers.begin(), track_.triggers.end(),
                                         [&](const Trigger& t) {
                                           return t.host.kind == "mesh" ? !meshIds.count(t.host.meshId) : !pathIds.count(t.host.pathId);
                                         }),
                          track_.triggers.end());
    if (!track_.triggers.empty() &&
        std::none_of(track_.triggers.begin(), track_.triggers.end(),
                     [](const Trigger& t) { return t.type == "checkpoint" && t.role == "finish"; })) {
      for (auto& t : track_.triggers)
        if (t.type == "checkpoint") {
          t.role = "finish";
          break;
        }
    }
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
  std::optional<std::string> selectedZoneId_;
  std::optional<std::string> selectedTriggerId_;
  int explicitCurrentPathIndex_{0};
};

}  // namespace editor
