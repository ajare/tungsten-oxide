// EditorState.hpp â€” mode/selection/drag/create-draft state for point editing: editMode/
// selectedPointId/dragging/createDraft plus setEditMode/nodeAtTop/deleteSelected/createModeClick.
// M4/M5 added mesh region placement (select/drag/rotate/delete) and Rails mode (rail-edge
// toggling on a shared MeshAsset) -- both removed along with MeshRegion/MeshAsset/MeshPlacement
// (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 2); selection is a 3-way exclusive point/zone/trigger
// concept now, not 4-way. Milestone 3/5 add a new drivable-mesh-object-placement selection concept
// from scratch rather than reviving this one.
//
// M7b adds texture asset registration/deletion/tile-sizing and per-path texture assignment:
// addTextureAsset/deleteTextureAsset/clampTextureTileSize/clearInvalidTextureAssignments/
// assignCurrentCurveTexture/clearCurrentCurveTexture. Image
// decoding and GL upload live in TextureCache.hpp/.cpp instead -- EditorState only ever holds the
// schema-level TextureAsset record (name/path/dimensions), same separation TopDownCanvas.cpp
// keeps between authored data and its own rendering.
//
// DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 1 adds ProjectionMode (TopDown/Front/Side), a
// canvas-wide state orthogonal to EditMode: it picks which plane screen drags project into, not
// what a click/drag does semantically. TopDown is today's only behavior; Front/Side exist for
// drivable mesh object placement but apply to every entity's editing.
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
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
                      Create };

// Canvas-wide, orthogonal to EditMode: which plane screen drags project into. TopDown (X/Z, view
// dir Y = -1) is today's only behavior; Front (X/Y, view dir Z = -1) and Side (Y/Z, view dir X = 1)
// are added for drivable mesh object placement (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 1) but
// apply to every entity's editing, not just placements.
enum class ProjectionMode { TopDown,
                            Front,
                            Side };

struct SelectedPoint {
  int pathIndex{-1};
  int pointIndex{-1};
  bool valid() const { return pathIndex >= 0 && pointIndex >= 0; }
};

class EditorState {
public:
  // backfillPointIds() is called after every track
  // construction/replacement (initial load, New, Random, Import). Without it here, a track
  // built in memory rather than loaded from JSON (main.cpp's buildStarterTrack(), New,
  // generateRandomTrack()) has no point ids at all, which silently defeats both the id-collision
  // fix (ids are minted by scanning for a gap -- irrelevant if nothing has an id yet) and the
  // start-point-preservation fix (which matches by id) the moment this constructor is skipped.
  explicit EditorState(TrackDefinition initial) : track_(std::move(initial)) { backfillPointIds(track_); }

  const TrackDefinition& track() const { return track_; }
  EditMode mode() const { return mode_; }
  SelectedPoint selection() const { return selection_; }
  const std::vector<tox::Vec3>& createDraft() const { return createDraft_; }
  ProjectionMode projectionMode() const { return projectionMode_; }

  // Switching planes mid-gesture would silently change what a drag/rotate's axes mean partway
  // through, so this drops any in-flight drag/rotate the same way setMode() does on an EditMode
  // switch -- a dangling half-mutation across a plane change is worse than the gesture just ending.
  void setProjectionMode(ProjectionMode mode) {
    projectionMode_ = mode;
    dragging_ = false;
    dragMutated_ = false;
    rotateGestureActive_ = false;
  }
  bool dragging() const { return dragging_; }
  const std::optional<std::string>& selectedZoneId() const { return selectedZoneId_; }
  const std::optional<std::string>& selectedTriggerId() const { return selectedTriggerId_; }
  const std::optional<std::string>& selectedReservationId() const { return selectedReservationId_; }
  const std::optional<std::string>& selectedMeshObjectId() const { return selectedMeshObjectId_; }

  // ---- Curve management ----
  //
  // "Current curve": the curve-selector dropdown sets
  // it directly and a control-point click overrides (selectPositionAt/selectPoint always win while a
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

  const ModelPlacement* findMeshObjectPlacement(const std::string& id) const {
    for (const auto& placement : track_.meshObjects)
      if (placement.id == id) return &placement;
    return nullptr;
  }

  // Non-const: PropertiesPanel.cpp edits an embedded Model's per-mesh Type/Visible metadata
  // in place (TRACK_MODEL_LIST_PLAN.md Milestone 6.2) -- callers push undo themselves via
  // editEmbeddedModel below rather than mutating this pointer directly.
  modelxml::ModelXmlDefinition* findModel(const std::string& id) {
    for (auto& model : track_.models)
      if (model.id == id) return &model;
    return nullptr;
  }
  const modelxml::ModelXmlDefinition* findModel(const std::string& id) const {
    for (const auto& model : track_.models)
      if (model.id == id) return &model;
    return nullptr;
  }

  // Mutates the embedded Model by id via `mutate`, pushing one undo step first -- mirrors
  // editMeshObjectPlacement's own pattern, for Milestone 6.2's per-mesh Type/Visible editor.
  template <typename Mutate>
  bool editEmbeddedModel(const std::string& id, Mutate&& mutate) {
    modelxml::ModelXmlDefinition* model = findModel(id);
    if (model == nullptr) return false;
    history_.push(track_);
    mutate(*model);
    return true;
  }

  // Unlike findZone/findTrigger, reservations are stored per-path, so this scans every path.
  const Reservation* findReservation(const std::string& id) const {
    for (const auto& path : track_.paths)
      for (const auto& reservation : path.reservations)
        if (reservation.id == id) return &reservation;
    return nullptr;
  }

  History& history() { return history_; }

  // Push the current state onto the opposite stack, restore
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

  // Clears the abandoned create draft, drops drag state, so switching
  // modes mid-gesture can never leave a dangling half-mutation.
  void setMode(EditMode mode) {
    mode_ = mode;
    createDraft_.clear();
    dragging_ = false;
    dragMutated_ = false;
    rotateGestureActive_ = false;
  }

  // Returns true if a position point was hit within `pickRadiusWorld` of (planeU, planeV) -- the
  // active ProjectionMode's plane coordinates (see planeCoords), not necessarily world (x, z).
  // Selects it (Edit mode's plain click) but does not start a drag -- call beginDrag separately
  // once the caller knows the mouse is actually moving.
  bool selectPositionAt(double planeU, double planeV, double pickRadiusWorld) {
    const auto hit = hitTestPosition(planeU, planeV, pickRadiusWorld);
    if (!hit) return false;
    selection_ = *hit;
    // Points/mesh objects/zones/triggers share one selection (props panel).
    selectedZoneId_.reset();
    selectedTriggerId_.reset();
    selectedMeshObjectId_.reset();
    return true;
  }

  void clearSelection() { selection_ = {}; }

  // Deselects whichever of the four mutually-exclusive selection kinds (point/mesh-region/zone/
  // trigger) is currently active -- the 'D' hotkey / Edit menu's "Deselect".
  // "Curve" (currentPathIndex()) isn't one of these: unlike the other four it's
  // never "nothing" -- it always resolves to some path (explicitCurrentPathIndex_, defaulting to
  // 0) -- so there's nothing to clear there; it simply stops being driven by a point selection
  // once one no longer exists, per currentPathIndex()'s own "selection wins while a point is
  // selected" precedence.
  void deselectAll() {
    selection_ = {};
    selectedZoneId_.reset();
    selectedTriggerId_.reset();
    selectedMeshObjectId_.reset();
  }

  // Read-only counterpart to selectPositionAt: the nearest Position point within
  // `pickRadiusWorld` of (planeU, planeV) -- the active ProjectionMode's plane coordinates -- or
  // nullopt, without mutating selection_ -- used to render a hover highlight distinct from the
  // actual click-driven selection (see TopDownCanvas.cpp's hover-highlight rendering).
  std::optional<SelectedPoint> hoverTestPosition(double planeU, double planeV, double pickRadiusWorld) const {
    return hitTestPosition(planeU, planeV, pickRadiusWorld);
  }

  // Whether the current selection is in range AND a Position point specifically -- used to guard
  // on-canvas X/Z dragging (TopDownCanvas.cpp) from also firing when a roll/width/cross-section
  // handle is selected (dragSelectedTo() itself has no kind guard, since every OTHER caller
  // already only ever selects a Position point before dragging).
  bool selectionIsPosition() const {
    return selectionInRange() && track_.paths[selection_.pathIndex].points[selection_.pointIndex].kind == PointKind::Position;
  }

  // Width-drag counterpart to selectionIsPosition() -- gates dragSelectedWidthTo()'s on-canvas
  // drag path.
  bool selectionIsWidth() const {
    return selectionInRange() && track_.paths[selection_.pathIndex].points[selection_.pointIndex].kind == PointKind::Width;
  }

  // Roll-drag counterpart to selectionIsPosition()/selectionIsWidth() -- gates
  // dragSelectedRollTo()'s on-canvas drag path.
  bool selectionIsRoll() const {
    return selectionInRange() && track_.paths[selection_.pathIndex].points[selection_.pointIndex].kind == PointKind::Roll;
  }

  // Cross-section-drag counterpart -- gates dragSelectedCurvatureTo()'s on-canvas drag path.
  bool selectionIsCrossSection() const {
    return selectionInRange() && track_.paths[selection_.pathIndex].points[selection_.pointIndex].kind == PointKind::CrossSection;
  }

  // ---- Drivable mesh object placements (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 5) ----
  //
  // Selection/on-canvas drag+rotate mirrors position points and the old (Milestone 2-removed) mesh
  // region: click-to-select, plain drag moves (using the shared dragging_/dragMutated_/beginDrag/
  // endDrag lifecycle, same as dragSelectedTo), shift+drag rotates (using the entity-agnostic
  // rotateGestureActive_/beginRotateGesture/dragRotateGestureTo/endRotateGesture plumbing Milestone
  // 1.3 built ahead of time for exactly this). Which plane coordinate a plain drag writes, and
  // which rotation axis a shift+drag writes, both follow the active ProjectionMode (TopDown moves
  // X/Z and yaws, Front moves X/Y and pitches, Side moves Y/Z and rolls) -- TopDownCanvas.cpp picks
  // the field, this class only stores whatever it's told.
  //
  // No asset library/thumbnail/bounding-box preview exists here: the editor never loads a
  // `.mppmodel` (see DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 3's "`.mppmodel` loading is host-only"
  // architecture note), so `modelId` is authored as a plain relative-path string (mirroring
  // `TextureAsset::path`), picked via a native file-open dialog in main.cpp, not browsed from a
  // pre-scanned list of known-valid ids.

  void selectMeshObject(const std::string& id) {
    selectedMeshObjectId_ = id;
    selection_ = {};
    selectedZoneId_.reset();
    selectedTriggerId_.reset();
  }

  void clearMeshObjectSelection() { selectedMeshObjectId_.reset(); }

  // Places a new instance of `modelId` at world (x, 0, z), facing/scaled at schema defaults.
  // Selects the new placement and returns its id.
  std::string addMeshObjectPlacement(const std::string& modelId, double x, double z) {
    history_.push(track_);
    ModelPlacement placement;
    placement.id = newMeshObjectId();
    placement.modelId = modelId;
    placement.position = tox::Vec3(x, 0.0, z);
    track_.meshObjects.push_back(std::move(placement));
    const std::string id = track_.meshObjects.back().id;
    selectMeshObject(id);
    return id;
  }

  // "Load Model" (TRACK_MODEL_LIST_PLAN.md Milestone 6, revised so loading no longer implies
  // placing): embeds `parsed` into `track_.models` -- reusing an existing entry whose ModelFile
  // already matches `modelFileReference` (dedup, per the locked-in decision) rather than duplicating
  // it, or appending a fresh entry with a freshly generated id otherwise. Creates no placement --
  // that's placeModelInstance's job, invoked separately (the canvas's right-click "Place Model"
  // submenu). Returns the embedded (or reused) Model's own id.
  std::string embedModel(modelxml::ModelXmlDefinition parsed, const std::string& modelFileReference) {
    const auto existing = std::find_if(track_.models.begin(), track_.models.end(),
                                       [&](const modelxml::ModelXmlDefinition& m) { return m.modelFile == modelFileReference; });
    if (existing != track_.models.end()) return existing->id.value_or(std::string());
    history_.push(track_);
    const std::string modelId = newModelId();
    parsed.id = modelId;
    parsed.modelFile = modelFileReference;
    track_.models.push_back(std::move(parsed));
    return modelId;
  }

  // Places a new instance of an already-embedded `modelId` (see embedModel above) at whichever
  // world position the active ProjectionMode's plane maps (planeU, planeV) to -- same
  // planeCoords/setPlaneCoords convention as every other canvas-placed entity (e.g. the "Add control
  // point" context-menu items), rather than always writing world X/Z regardless of mode. Selects the
  // new placement and returns its id.
  std::string placeModelInstance(const std::string& modelId, double planeU, double planeV) {
    history_.push(track_);
    ModelPlacement placement;
    placement.id = newMeshObjectId();
    placement.modelId = modelId;
    setPlaneCoords(projectionMode_, placement.position, planeU, planeV);
    track_.meshObjects.push_back(std::move(placement));
    const std::string placementId = track_.meshObjects.back().id;
    selectMeshObject(placementId);
    return placementId;
  }

  // Mutates the placement by id via `mutate`, pushing one undo step first. Also re-clamps `scale`
  // away from zero/negative (a zero or negative scale axis would collapse or mirror the referenced
  // model in a way nothing downstream expects), mirroring editZone/editReservation's own
  // re-clamp-after-mutate pattern.
  template <typename Mutate>
  bool editMeshObjectPlacement(const std::string& id, Mutate&& mutate) {
    const auto it = std::find_if(track_.meshObjects.begin(), track_.meshObjects.end(),
                                 [&](const ModelPlacement& p) { return p.id == id; });
    if (it == track_.meshObjects.end()) return false;
    history_.push(track_);
    mutate(*it);
    it->scale.x = std::max(1e-3, it->scale.x);
    it->scale.y = std::max(1e-3, it->scale.y);
    it->scale.z = std::max(1e-3, it->scale.z);
    return true;
  }

  bool deleteSelectedMeshObjectPlacement() {
    if (!selectedMeshObjectId_.has_value()) return false;
    const auto it = std::find_if(track_.meshObjects.begin(), track_.meshObjects.end(),
                                 [&](const ModelPlacement& p) { return p.id == *selectedMeshObjectId_; });
    if (it == track_.meshObjects.end()) return false;
    history_.push(track_);
    track_.meshObjects.erase(it);
    selectedMeshObjectId_.reset();
    pruneStaleReferences();
    return true;
  }

  // On-canvas plain-drag counterpart to addMeshObjectPlacement's initial position -- (planeU,
  // planeV) are already projected into the active ProjectionMode's plane (see planeCoords/
  // setPlaneCoords), same convention dragSelectedTo uses for position points. No-op (returns
  // false) if nothing is selected or the id no longer resolves (stale selection).
  bool dragSelectedMeshObjectTo(double planeU, double planeV) {
    if (!selectedMeshObjectId_.has_value()) return false;
    const auto it = std::find_if(track_.meshObjects.begin(), track_.meshObjects.end(),
                                 [&](const ModelPlacement& p) { return p.id == *selectedMeshObjectId_; });
    if (it == track_.meshObjects.end()) return false;
    if (!dragMutated_) {
      history_.push(track_);
      dragMutated_ = true;
    }
    setPlaneCoords(projectionMode_, it->position, planeU, planeV);
    return true;
  }

  // Shift-drag rotate counterpart to dragSelectedMeshObjectTo above -- writes into whichever of
  // rotation.x/y/z (yaw/pitch/roll) the active ProjectionMode implies (TopDown=yaw, Front=pitch,
  // Side=roll), pushing history once per gesture via the SAME dragMutated_ flag
  // dragSelectedMeshObjectTo uses. Safe to share: a plain drag and a shift-drag rotate are
  // mutually exclusive per frame (the caller picks one based on whether shift is held, never
  // both), so there's never a chance of one gesture's flag state leaking into the other's.
  // Deliberately NOT built on editMeshObjectPlacement -- that pushes history unconditionally on
  // every call, which called once per dragged frame would push a new undo step every single frame
  // of the rotate instead of one per gesture.
  bool dragSelectedMeshObjectRotationTo(double degrees) {
    if (!selectedMeshObjectId_.has_value()) return false;
    const auto it = std::find_if(track_.meshObjects.begin(), track_.meshObjects.end(),
                                 [&](const ModelPlacement& p) { return p.id == *selectedMeshObjectId_; });
    if (it == track_.meshObjects.end()) return false;
    if (!dragMutated_) {
      history_.push(track_);
      dragMutated_ = true;
    }
    switch (projectionMode_) {
      case ProjectionMode::Front: it->rotation.y = degrees; break;
      case ProjectionMode::Side: it->rotation.z = degrees; break;
      case ProjectionMode::TopDown:
      default: it->rotation.x = degrees; break;
    }
    return true;
  }

  // ---- Zones ----
  //
  // Full add/edit-fields/delete via a dedicated panel (ZonesPanel.hpp/.cpp),
  // plus on-canvas rendering and click-to-select (reusing core's own baked tox::Zone records --
  // see TopDownCanvas.cpp's zoneOutlineWorld/zoneAtWorld), but NOT an on-canvas drag that would
  // continuously re-project the mouse onto the nearest path -- there is no spline evaluator
  // exposed to cpp/editor for that (core
  // keeps its own Evaluator private to TrackBake.cpp), so reproducing that exactly would mean
  // porting or exposing one. Every zone is path-hosted now (mesh-hosted zones were removed along
  // with MeshRegion, DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 2).

  void selectZone(const std::string& id) {
    selectedZoneId_ = id;
    selection_ = {};
    selectedTriggerId_.reset();
    selectedMeshObjectId_.reset();
  }

  void clearZoneSelection() { selectedZoneId_.reset(); }

  // Adds a path-hosted zone at parameter `t` along `pathIndex` with schema defaults (width 24,
  // length 40, and factor 1.5/duration 2 if boost -- TrackDefinition.hpp's own field defaults,
  // which already match TrackCore.DEFAULT_ZONE_WIDTH/LENGTH/DEFAULT_BOOST_FACTOR/DURATION exactly).
  // `effect` may be "velocityChange" (boost), "jump", or "startGrid"; anything else is treated
  // as boost, matching loader normalization.
  // Selects the new zone and returns its id, or nullopt if pathIndex is invalid.
  std::optional<std::string> addPathZone(int pathIndex, const std::string& effect, double t, double lateral) {
    if (pathIndex < 0 || pathIndex >= static_cast<int>(track_.paths.size())) return std::nullopt;
    history_.push(track_);
    Zone zone;
    zone.id = newZoneId();
    zone.effect = effect == "startGrid" ? "startGrid" : effect == "jump" ? "jump" : "velocityChange";
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

  // ---- Triggers ----
  //
  // Same scope reduction as zones: full add/edit-fields/delete via a dedicated panel
  // (TriggersPanel.hpp/.cpp) plus on-canvas rendering and click-to-select, reusing core's own
  // baked tox::Trigger records. Unlike zones, core already bakes a trigger's complete world-space
  // gate frame (center/right/up/fwd/halfWidth/height) directly -- no centerline-interpolation
  // approximation is needed here at all (see TopDownCanvas.cpp's drawTriggers/triggerAtWorld).
  // NOT implemented: on-canvas drag, for the same reason zone drag
  // isn't (it would continuously re-project onto the nearest path via a live spline evaluator that
  // isn't exposed to cpp/editor). Only path-hosted trigger creation is wired up in the panel;
  // mesh-hosted triggers can still be loaded, viewed, selected and edited.

  void selectTrigger(const std::string& id) {
    selectedTriggerId_ = id;
    selection_ = {};
    selectedZoneId_.reset();
    selectedMeshObjectId_.reset();
  }

  void clearTriggerSelection() { selectedTriggerId_.reset(); }

  // Adds a path-hosted trigger at parameter `t` along `pathIndex` with schema defaults (width 40,
  // height 12 -- TrackDefinition.hpp's own field defaults, matching TrackCore.DEFAULT_TRIGGER_WIDTH/
  // HEIGHT). `type` must be "dummy" or "checkpoint"; anything else is treated as dummy. A fresh
  // checkpoint starts as role "intermediate". Selects the new trigger and returns its
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
    if (trigger.type == "checkpoint") {
      trigger.role = "intermediate";
      // A checkpoint gates the FULL track width dead-center, unrotated -- unlike a dummy trigger's
      // free-form rotation/lateral/width fields. Explicit rather than relying on already-zero-valued defaults, so
      // this stays correct if those defaults ever change.
      trigger.rotation = 0.0;
      trigger.host.lateral = 0.0;
      trigger.autoWidth = true;
    }
    track_.triggers.push_back(std::move(trigger));
    const std::string triggerId = track_.triggers.back().id;
    selectTrigger(triggerId);
    return triggerId;
  }

  // Mutates the trigger by id via `mutate`, pushing one undo step first and re-clamping afterward.
  // Also enforces a finish-uniqueness invariant: if `mutate`
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
    if (it->host.kind == "path") {
      it->host.t = std::clamp(it->host.t, 0.0, 1.0);
    } else {
      it->autoWidth = false;   // meaningless without a host path/t to sample a road width from
      it->host.lateral = 0.0;  // meaningless without a host path centerline to offset from
    }
    return true;
  }

  // On-canvas trigger center-handle drag (new functionality -- previously host.t was panel-edited
  // only, via editTrigger's "T (%)" field): same drag lifecycle (beginDrag/endDrag/one-push-per-
  // gesture, sharing dragging_/dragMutated_ with the aux-point drag mutators above) but for a
  // path-hosted Trigger's host.t. Keeps the trigger on its CURRENT host path via
  // tangent-projection (like dragSelectedAuxTTo; the caller computes the new t the same way) rather
  // than re-hosting onto whatever path is nearest the cursor -- there's no live spline
  // evaluator here to do that search from an arbitrary world point. No-op (returns false) for a
  // mesh-hosted trigger, since host.t is meaningless there.
  bool dragSelectedTriggerTTo(double t) {
    if (!dragging_ || !selectedTriggerId_.has_value()) return false;
    const auto it = std::find_if(track_.triggers.begin(), track_.triggers.end(),
                                 [&](const Trigger& trigger) { return trigger.id == *selectedTriggerId_; });
    if (it == track_.triggers.end() || it->host.kind != "path") return false;
    if (!dragMutated_) {
      history_.push(track_);
      dragMutated_ = true;
    }
    it->host.t = std::clamp(t, 0.0, 1.0);
    return true;
  }

  // A checkpoint marked "finish" can't be
  // deleted until another is promoted first, since a track needs exactly one finish trigger for
  // lap detection. Returns false (no-op) when blocked.
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

  // ---- Central reservations (CENTRAL_RESERVATION_PLAN.md) ----
  //
  // A path-hosted void carved out of the road between t0 and t1 (native C++ only).
  // Panel-only authoring (numeric t0/t1/width fields via
  // ReservationsPanel.hpp/.cpp), matching the current state of roll/width/cross-section points:
  // no on-canvas click-to-place or drag. Stored per-path but selected/deleted through one flat id
  // namespace like zones/triggers, via findReservation below.

  void selectReservation(const std::string& id) {
    selectedReservationId_ = id;
    selection_ = {};
    selectedZoneId_.reset();
    selectedTriggerId_.reset();
    selectedMeshObjectId_.reset();
  }

  void clearReservationSelection() { selectedReservationId_.reset(); }

  // Locates (pathIndex, reservationIndex) for `id`, or nullopt if no path has it -- unlike
  // findReservation (a plain pointer, matching findZone/findTrigger's convention for read-only
  // display), this is what editReservation/deleteSelectedReservation need to mutate/erase in place.
  std::optional<std::pair<int, int>> locateReservation(const std::string& id) const {
    for (int pi = 0; pi < static_cast<int>(track_.paths.size()); ++pi) {
      const auto& reservations = track_.paths[pi].reservations;
      for (int ri = 0; ri < static_cast<int>(reservations.size()); ++ri)
        if (reservations[ri].id == id) return std::make_pair(pi, ri);
    }
    return std::nullopt;
  }

  // Adds a reservation on `pathIndex` at [t0,t1] with `width`, clamped/pushed clear of any other
  // reservation already on that path (see clampReservation below -- auto-clamp, no reachable
  // invalid state, CENTRAL_RESERVATION_PLAN.md). Selects the new reservation and returns its id, or
  // nullopt if pathIndex is invalid.
  std::optional<std::string> addReservation(int pathIndex, double t0, double t1, double width) {
    if (pathIndex < 0 || pathIndex >= static_cast<int>(track_.paths.size())) return std::nullopt;
    history_.push(track_);
    Reservation reservation;
    reservation.id = newReservationId();
    reservation.t0 = t0;
    reservation.t1 = t1;
    reservation.width = width;
    auto& reservations = track_.paths[pathIndex].reservations;
    reservations.push_back(std::move(reservation));
    clampReservation(reservations, static_cast<int>(reservations.size()) - 1);
    const std::string id = reservations.back().id;
    selectReservation(id);
    return id;
  }

  // Mutates the reservation by id via `mutate`, pushing one undo step first and re-clamping
  // afterward (same pattern as editZone/editTrigger).
  template <typename Mutate>
  bool editReservation(const std::string& id, Mutate&& mutate) {
    const auto found = locateReservation(id);
    if (!found) return false;
    history_.push(track_);
    auto& reservations = track_.paths[found->first].reservations;
    mutate(reservations[found->second]);
    clampReservation(reservations, found->second);
    return true;
  }

  bool deleteSelectedReservation() {
    if (!selectedReservationId_.has_value()) return false;
    const auto found = locateReservation(*selectedReservationId_);
    if (!found) return false;
    history_.push(track_);
    track_.paths[found->first].reservations.erase(track_.paths[found->first].reservations.begin() + found->second);
    selectedReservationId_.reset();
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
    selectedMeshObjectId_.reset();
    explicitCurrentPathIndex_ = std::clamp(deleteIndex, 0, static_cast<int>(track_.paths.size()) - 1);
    clampStart();
    return true;
  }

  // ---- Connect/join ----
  //
  // Endpoint-to-endpoint only: same-path closes the
  // loop; different-path shares the target endpoint's identity and records a junction. Joining onto
  // an INTERIOR point of an open path (which would need splitting the target path there first) is
  // out of scope here; connecting to an existing curve's middle isn't offered by this panel.
  // `pathA`/`pathB` must each be an OPEN path;
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
    *sourceSlot = targetCopy;  // shares identity by copying the whole point (id included)
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

  // ---- Drag-to-weld an open path's endpoint onto another open endpoint (distinct from the
  // shift+drag rubber-band gesture below, which is a separate, non-moving gesture; this
  // is a plain-drag variant that also relocates the point). Both helpers below feed the top-down
  // canvas's per-frame drag handling: selectedOpenEndpointEnd() answers "is the point currently
  // being dragged actually an open path's start/end", hitTestOpenEndpoint() finds a weld target
  // near it, and joinPathEndpoints() (above) performs the actual weld on release. ----

  struct OpenEndpointRef {
    int pathIndex;
    bool atEnd;  // false = path's first Position point, true = last -- same convention as joinPathEndpoints
  };

  // nullopt unless the current selection is a Position point sitting at the first or last
  // Position index of an OPEN path (a closed path or an interior point can't be welded).
  std::optional<bool> selectedOpenEndpointEnd() const {
    if (!selectionInRange()) return std::nullopt;
    const Path& path = track_.paths[selection_.pathIndex];
    if (path.closed) return std::nullopt;
    if (path.points[selection_.pointIndex].kind != PointKind::Position) return std::nullopt;
    const int posIndex = rawIndexToPositionIndex(path, selection_.pointIndex);
    if (posIndex < 0) return std::nullopt;
    if (posIndex == 0) return false;
    if (posIndex == positionCount(path) - 1) return true;
    return std::nullopt;
  }

  // Nearest-wins search over every OTHER open path's two endpoints (excluding the specific
  // (excludePathIndex, excludeAtEnd) endpoint being dragged -- but NOT excluding that path's other
  // endpoint, since dragging one end onto the other end of the SAME open path is exactly how a
  // curve closes itself) within pickRadiusWorld of a world point. Pure proximity, independent of
  // pickRadiusWorld's own screen-derived convention elsewhere in this file (kPickRadiusPx / scale).
  std::optional<OpenEndpointRef> hitTestOpenEndpoint(double worldX, double worldZ, double pickRadiusWorld, int excludePathIndex,
                                                     bool excludeAtEnd) const {
    std::optional<OpenEndpointRef> best;
    double bestDistSq = pickRadiusWorld * pickRadiusWorld;
    for (int p = 0; p < static_cast<int>(track_.paths.size()); ++p) {
      const Path& path = track_.paths[p];
      if (path.closed) continue;
      for (const bool atEnd : {false, true}) {
        if (p == excludePathIndex && atEnd == excludeAtEnd) continue;
        const TrackPoint* point = atEnd ? lastPosition(path) : firstPosition(path);
        if (point == nullptr) continue;
        const auto [pu, pv] = planeCoords(projectionMode_, point->pos);
        const double dx = pu - worldX, dz = pv - worldZ;
        const double distSq = dx * dx + dz * dz;
        if (distSq <= bestDistSq) {
          bestDistSq = distSq;
          best = OpenEndpointRef{p, atEnd};
        }
      }
    }
    return best;
  }

  // Shift-drag-into-empty-space release action: extends an open
  // path by appending (atEnd=true) or prepending (atEnd=false) a new position point at
  // (worldX, worldZ) -- the active ProjectionMode's plane coordinates -- inheriting the dragged
  // endpoint's own current value on the third axis, outside that plane (there's no on-curve sample
  // to inherit from beyond the curve's own end). Delegates to insertPositionOnSegment, which
  // already pushes undo and selects the new point.
  std::optional<int> extendOpenPathFromEndpoint(int pathIndex, bool atEnd, double worldX, double worldZ) {
    if (pathIndex < 0 || pathIndex >= static_cast<int>(track_.paths.size())) return std::nullopt;
    const Path& path = track_.paths[pathIndex];
    if (path.closed) return std::nullopt;
    const TrackPoint* fromPoint = atEnd ? lastPosition(path) : firstPosition(path);
    if (fromPoint == nullptr) return std::nullopt;
    const int insertAt = atEnd ? positionCount(path) : 0;
    tox::Vec3 newPos = fromPoint->pos;
    setPlaneCoords(projectionMode_, newPos, worldX, worldZ);
    return insertPositionOnSegment(pathIndex, insertAt, newPos.x, newPos.y, newPos.z);
  }

  // ---- Disjoint / reconnect ----
  //
  // "Disjoint" splits a shared/smooth control point into a hard, unsmoothed seam. The point ID
  // itself stays shared (this is a smoothing annotation, not an
  // identity split): core's baker reads disjointSeams to skip tangent/roll continuity there
  // (TrackBake.cpp), so both sides remain physically coincident. Guarded like disjointDisabledReason:
  // refuses an already-disjoint open endpoint, and refuses an open-path split that would leave
  // fewer than 4 position points on either side.
  //
  // A path rebuilt by makeDisjoint/reconnectDisjoint here does NOT
  // proportionally redistribute its roll/width/cross-section points from the original curve --
  // they're reset to schema defaults (appendDefaultAuxPoints, the same helper finishCreateDraft
  // uses). Banking/width authored before a split/reconnect is lost on the rebuilt path(s) and must
  // be re-entered via the Point Properties panel; this is the same "authoring capability over
  // pixel-perfect parity" scope reduction the zones/triggers panels already document.
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

  // ---- Split Point ----
  //
  // Structurally the same rotate-and-duplicate (closed path) / erase-and-insert-two-paths (open
  // path) operation as makeDisjoint() above, but a PERMANENT split rather than a smoothing seam:
  // no Connection is recorded in track_.disjointSeams (there's nothing for reconnectDisjoint() to
  // undo), and the newly-created coincident point gets a fresh newPointId() rather than sharing
  // the original point's id -- the two ends are independent points from the moment of the split,
  // not two ends of one annotated seam. Refuses (returns false, no history push) at an open path's
  // first/last position (nothing to split off there); no minimum-points-per-side floor otherwise,
  // since (unlike makeDisjoint) there's no smoothing continuity to preserve at the cut.
  bool splitSelectedPoint() {
    if (!selectionInRange()) return false;
    const Path& path = track_.paths[selection_.pathIndex];
    if (path.points[selection_.pointIndex].kind != PointKind::Position) return false;
    const int positionIndex = rawIndexToPositionIndex(path, selection_.pointIndex);
    if (positionIndex < 0) return false;
    const int count = positionCount(path);
    if (!path.closed && (positionIndex == 0 || positionIndex == count - 1)) return false;

    const int pathIndex = selection_.pathIndex;
    const std::string startPointId = currentStartPointId();
    history_.push(track_);

    Path& mutablePath = track_.paths[pathIndex];
    std::vector<TrackPoint> positions;
    for (const auto& p : mutablePath.points)
      if (p.kind == PointKind::Position) positions.push_back(p);

    if (mutablePath.closed) {
      std::rotate(positions.begin(), positions.begin() + positionIndex, positions.end());
      TrackPoint duplicate = positions.front();
      duplicate.id = newPointId();
      positions.push_back(duplicate);
      Path rebuilt;
      rebuilt.id = mutablePath.id;
      rebuilt.closed = false;
      rebuilt.points = std::move(positions);
      rebuilt.texture = mutablePath.texture;
      appendDefaultAuxPoints(rebuilt);
      mutablePath = std::move(rebuilt);
      selection_ = {pathIndex, 0};
    } else {
      std::set<std::string> usedPathIds;
      for (const auto& p : track_.paths) usedPathIds.insert(p.id);
      const std::string leftId = firstUnusedId("path", usedPathIds);
      usedPathIds.insert(leftId);
      const std::string rightId = firstUnusedId("path", usedPathIds);

      Path leftPath;
      leftPath.id = leftId;
      leftPath.closed = false;
      leftPath.points.assign(positions.begin(), positions.begin() + positionIndex + 1);
      leftPath.texture = mutablePath.texture;
      appendDefaultAuxPoints(leftPath);

      Path rightPath;
      rightPath.id = rightId;
      rightPath.closed = false;
      TrackPoint duplicate = positions[positionIndex];
      duplicate.id = newPointId();
      rightPath.points.push_back(duplicate);
      rightPath.points.insert(rightPath.points.end(), positions.begin() + positionIndex + 1, positions.end());
      rightPath.texture = mutablePath.texture;
      appendDefaultAuxPoints(rightPath);

      track_.paths.erase(track_.paths.begin() + pathIndex);
      track_.paths.insert(track_.paths.begin() + pathIndex, {leftPath, rightPath});
      selection_ = {pathIndex + 1, 0};
    }

    preserveStartPoint(startPointId);
    pruneStaleReferences();
    return true;
  }

  // ---- Generic shift+drag-to-rotate gesture plumbing (DRIVABLE_MESH_OBJECTS_PLAN.md
  // Milestone 1.3) ----
  //
  // Entity-agnostic angle bookkeeping for the canvas's shift+drag rotate gesture: TopDown yields
  // yaw, Front pitch, Side roll (see TopDownCanvas.cpp's rotateAngleDeg, which produces the
  // (origin-relative) angles fed in here). Tracks only the accumulated angle delta, since no
  // entity exists yet to own a yaw/pitch/roll field (drivable mesh object placements land in
  // Milestone 3; Milestone 5 wires this gesture to one). The gap between the drag's start angle
  // and the entity's rotation at mousedown is preserved for the whole gesture, so the shape
  // doesn't jump to face the cursor.
  bool rotateGestureActive() const { return rotateGestureActive_; }

  void beginRotateGesture(double originRotationDeg, double startAngleDeg) {
    rotateGestureOriginDeg_ = originRotationDeg;
    rotateGestureStartAngleDeg_ = startAngleDeg;
    rotateGestureActive_ = true;
  }

  // Returns the rotation to apply -- origin rotation plus the angle delta since gesture start.
  // Milestone 5's caller writes this into whichever yaw/pitch/roll field the active
  // ProjectionMode corresponds to; this class doesn't know about that entity yet.
  double dragRotateGestureTo(double currentAngleDeg) const {
    return rotateGestureActive_ ? rotateGestureOriginDeg_ + (currentAngleDeg - rotateGestureStartAngleDeg_) : rotateGestureOriginDeg_;
  }

  void endRotateGesture() { rotateGestureActive_ = false; }

  // ---- Texture assets (M7b) ----

  // Registers a newly loaded image as a texture asset: the id is
  // derived from `name` (mirrors TrackMesh.uniqueAssetId -- sanitized, deduped against existing
  // ids), and the tile size starts at the full image (one tile).
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

  // Also clears any path currently bound to it.
  bool deleteTextureAsset(const std::string& assetId) {
    if (!track_.textureAssets.count(assetId)) return false;
    history_.push(track_);
    track_.textureAssets.erase(assetId);
    for (auto& path : track_.paths)
      if (path.texture && path.texture->assetId == assetId) path.texture.reset();
    return true;
  }

  // Shrinking a tile size below
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

  // No-op (no history push) if already assigned exactly this asset/tile.
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

  // ---- TrackMaterials (Materials panel) ----

  const std::vector<std::string>& availableMaterials() const { return availableMaterials_; }

  // Alphabetically-first available material, or "" if none are known yet (main.cpp calls
  // setAvailableMaterials once at startup, right after MaterialCatalog loads -- before that, and
  // in every main.cpp self-test that never calls it at all, this stays empty and every path's
  // material stays "" too, which is harmless since nothing there exercises mppmodel export).
  std::string defaultMaterial() const { return availableMaterials_.empty() ? std::string() : availableMaterials_.front(); }

  // Called once at startup once MaterialCatalog has loaded (main.cpp), and whenever a track's
  // material set could otherwise go stale. Stores the sorted qualified-name list and immediately
  // backfills every current path's material (see backfillMaterials()).
  void setAvailableMaterials(std::vector<std::string> qualifiedNames) {
    availableMaterials_ = std::move(qualifiedNames);
    std::sort(availableMaterials_.begin(), availableMaterials_.end());
    backfillMaterials();
  }

  // Mirrors assignPathTexture's no-op-if-unchanged guard.
  bool assignPathMaterial(int pathIndex, const std::string& qualifiedName) {
    if (pathIndex < 0 || pathIndex >= static_cast<int>(track_.paths.size())) return false;
    if (!std::binary_search(availableMaterials_.begin(), availableMaterials_.end(), qualifiedName)) return false;
    Path& path = track_.paths[pathIndex];
    if (path.material == qualifiedName) return false;
    history_.push(track_);
    path.material = qualifiedName;
    return true;
  }

  // Wholesale replacement, e.g. loading a file: clears interaction state that no longer refers to
  // anything meaningful in the new track (the same resets setMode does). Does NOT touch
  // history -- callers that want the old state to remain undoable should push() it first.
  void replaceTrack(TrackDefinition replacement) { replaceTrackKeepHistory(std::move(replacement)); }

  // One undo push per drag gesture, on the first actual mutation.
  void beginDrag() {
    dragging_ = true;
    dragMutated_ = false;
  }

  // (planeU, planeV) are screen-drag coordinates already projected into the active
  // ProjectionMode's plane (see planeCoords/setPlaneCoords) -- TopDown's (x, z), Front's (x, y), or
  // Side's (y, z). The third axis, outside that plane, is left as-is.
  void dragSelectedTo(double planeU, double planeV) {
    if (!dragging_ || !selectionInRange()) return;
    if (!dragMutated_) {
      history_.push(track_);
      dragMutated_ = true;
    }
    TrackPoint& point = track_.paths[selection_.pathIndex].points[selection_.pointIndex];
    setPlaneCoords(projectionMode_, point.pos, std::round(planeU * 10.0) / 10.0, std::round(planeV * 10.0) / 10.0);
  }

  // On-canvas width-handle drag: same drag lifecycle (beginDrag/endDrag/one-push-per-gesture,
  // sharing dragging_/dragMutated_ with dragSelectedTo) but for a Width
  // point's `width` field. The caller (TopDownCanvas.cpp) computes the new width value itself:
  // it needs the baked frame's position/h axis at the point's `t`, which EditorState -- deliberately
  // THE-free -- has no access to (see EditorTrackDefinition.hpp's own header comment on why).
  void dragSelectedWidthTo(double width) {
    if (!dragging_ || !selectionInRange()) return;
    TrackPoint& point = track_.paths[selection_.pathIndex].points[selection_.pointIndex];
    if (point.kind != PointKind::Width) return;
    if (!dragMutated_) {
      history_.push(track_);
      dragMutated_ = true;
    }
    point.width = std::max(1.0, width);
  }

  // Properties-panel slider counterpart to dragSelectedWidthTo(). The shared drag lifecycle
  // makes a continuous center-offset slider gesture a single undoable edit.
  void dragSelectedWidthCenterOffsetTo(double centerOffsetPercent) {
    if (!dragging_ || !selectionInRange()) return;
    TrackPoint& point = track_.paths[selection_.pathIndex].points[selection_.pointIndex];
    if (point.kind != PointKind::Width) return;
    if (!dragMutated_) {
      history_.push(track_);
      dragMutated_ = true;
    }
    point.centerOffsetPercent = std::clamp(centerOffsetPercent, -50.0, 50.0);
  }

  // On-canvas roll-handle drag: same drag lifecycle as dragSelectedWidthTo(), for a Roll point's
  // `roll` field. The caller computes the roll value itself from the baked frame (position/h
  // axis/width at the point's `t`), same THE-free split as dragSelectedWidthTo().
  void dragSelectedRollTo(double rollDegrees) {
    if (!dragging_ || !selectionInRange()) return;
    TrackPoint& point = track_.paths[selection_.pathIndex].points[selection_.pointIndex];
    if (point.kind != PointKind::Roll) return;
    if (!dragMutated_) {
      history_.push(track_);
      dragMutated_ = true;
    }
    point.roll = std::clamp(rollDegrees, -180.0, 180.0);
  }

  // Cross-section-handle drag counterpart to dragSelectedWidthTo()/dragSelectedRollTo(): same
  // drag lifecycle, for a CrossSection point's `curvature` field. Previously click-to-select
  // only (panel-edited); this adds the same on-canvas value drag the other two aux kinds already
  // had.
  void dragSelectedCurvatureTo(double curvature) {
    if (!dragging_ || !selectionInRange()) return;
    TrackPoint& point = track_.paths[selection_.pathIndex].points[selection_.pointIndex];
    if (point.kind != PointKind::CrossSection) return;
    if (!dragMutated_) {
      history_.push(track_);
      dragMutated_ = true;
    }
    point.curvature = std::clamp(curvature, -1.0, 1.0);
  }

  // t-axis counterpart to dragSelectedWidthTo/dragSelectedRollTo/dragSelectedCurvatureTo: lets an
  // on-canvas drag of a Roll/Width/CrossSection point ALSO move it along the curve, not just
  // change its value (see TopDownCanvas.cpp's tangentAtG for how the along-curve component
  // is derived). Shares the same beginDrag/endDrag/one-push-per-gesture lifecycle (dragMutated_)
  // as the value-drag methods above, so a single drag gesture that moves both perpendicular
  // (value) and tangential (t) still pushes exactly one undo step, whichever of the two mutators
  // happens to fire first each gesture.
  void dragSelectedAuxTTo(double t) {
    if (!dragging_ || !selectionInRange()) return;
    TrackPoint& point = track_.paths[selection_.pathIndex].points[selection_.pointIndex];
    if (point.kind == PointKind::Position) return;
    if (!dragMutated_) {
      history_.push(track_);
      dragMutated_ = true;
    }
    point.t = std::clamp(t, 0.0, 1.0);
  }

  void endDrag() {
    dragging_ = false;
    dragMutated_ = false;
  }

  // Direct index-based selection, for callers (the elevation view) that already know exactly
  // which point they hit rather than needing a world-space radius search.
  void selectPoint(int pathIndex, int pointIndex) {
    selection_ = {pathIndex, pointIndex};
    // Mirrors selectPositionAt's own clearing of the other three selection kinds -- a point,
    // mesh object, zone, and trigger share one "the selected object" slot (only one of the four
    // is ever selected at a time), so picking one clears the other three. This one had been
    // missing the zone/trigger reset (only mesh was cleared), so clicking a roll/width/cross-
    // section handle while a zone or trigger was selected left both "selected" simultaneously.
    selectedZoneId_.reset();
    selectedTriggerId_.reset();
    selectedMeshObjectId_.reset();
  }

  // Deletes the currently selected point. For a position point, refuses to drop a path below 4,
  // since a track path needs that many to bake. For a roll/width/crossSection point there's no
  // minimum-count guard at all -- a path can have zero roll/width/crossSection points; core's
  // baker treats that as flat/default-width/uncurved. No shared/disjoint-id guard -- this editor
  // doesn't alias points by id yet (see EditorTrackDefinition.hpp). Preserves track_.start across
  // the deletion (preserveStartPoint) -- harmless no-op when the deleted point isn't a/the position
  // point start refers to.
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

  // ---- Segment selection/deletion/splitting; insert-point-on-segment ----
  //
  // `i` throughout is a POSITION-space index (0-based, counting only Position points on the
  // path) identifying the segment running from position i to position i+1 (wrapping for a closed
  // path), never a raw Path::points index.
  //
  // Deliberately does NOT support click-to-select-a-segment directly -- the shipped UI only ever
  // derives a segment from the *currently selected control
  // point* via selectedOutgoingSegment/selectedIncomingSegment below, so that's the only path
  // supported here.
  struct SegmentRef {
    int pathIndex, i;
  };

  static int positionCount(const Path& path) {
    return static_cast<int>(std::count_if(path.points.begin(), path.points.end(), [](const TrackPoint& p) { return p.kind == PointKind::Position; }));
  }

  // The segment leaving the currently selected control point, or nullopt if nothing satisfies it
  // (no selection, an aux point selected, or an open path's last point). Returns null while a
  // roll/width/crossSection point is selected: this port keeps all point kinds in one `selection_`,
  // so that guard becomes "the selected point must be a Position point".
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

  // The segment arriving at the currently selected control point (same guards as
  // selectedOutgoingSegment above, mirrored for the other direction).
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
  // `pathIndex`:
  //  - closed path: opens it by rotating the point array so the cut becomes the new start/end
  //    (no point removed -- opening a loop needs no duplicate endpoint, unlike makeDisjoint's
  //    opened-closed case, which marks a *smoothing seam* at a point that stays shared).
  //  - open path, first or last segment: shrinks by dropping that one endpoint (refused below the
  //    4-point floor).
  //  - open path, an interior segment: splits into two new open paths at the cut, with fresh
  //    default roll/width/crossSection points (refused if either half would drop below 4 points)
  //    -- same "authoring capability over pixel-perfect parity" tradeoff as makeDisjoint's split
  //    (no proportional roll/width redistribution).
  // Refuses (returns false, no history push) if the path carries a disjoint seam: reconnect it
  // first.
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
  // (clamped to [0, positionCount]) -- used by the "Position" context-menu item. Selects the new
  // point and returns its raw Path::points index, or nullopt if pathIndex is invalid.
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

  // ---- Roll/width/cross-section point editing ----
  //
  // Scoped to add/edit-fields/delete via a properties panel (PropertiesPanel.hpp/.cpp), NOT a
  // draggable-handle-on-canvas interaction across the top-down and elevation views -- that would
  // need deriving a screen-space handle position from an arbitrary t along the baked centerline,
  // which is materially more work than the schema-authoring capability itself. This still makes
  // banking/width/cross-section fully authorable; on-canvas dragging remains a gap.

  TrackPoint* mutablePointAt(int pathIndex, int pointIndex) {
    if (pathIndex < 0 || pathIndex >= static_cast<int>(track_.paths.size())) return nullptr;
    auto& points = track_.paths[pathIndex].points;
    if (pointIndex < 0 || pointIndex >= static_cast<int>(points.size())) return nullptr;
    return &points[pointIndex];
  }

  // Appends a new roll/width/crossSection point to `pathIndex` at parameter `t`, using the schema
  // defaults every TrackPoint already carries (roll 0, width 36, curvature 0/tightness 1/thickness
  // 4 -- rather than interpolating the curve's *current* value at that t, which would keep a fresh
  // point from visibly kinking the curve; a newly added point here may need its value adjusted
  // immediately).
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
    if (point->kind == PointKind::Width) {
      point->width = std::max(1.0, point->width);
      point->centerOffsetPercent = std::clamp(point->centerOffsetPercent, -50.0, 50.0);
    }
    if (point->kind == PointKind::CrossSection) {
      point->curvature = std::clamp(point->curvature, -1.0, 1.0);
      point->tightness = std::clamp(point->tightness, 0.2, 4.0);
      point->thickness = std::max(0.0, point->thickness);
    }
    return true;
  }

  // Numeric-field counterpart to dragSelectedTo for a Position point, for
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

  // ---- Point type conversion ----

  // Reason a Type-combo conversion of the selected point to `newKind` would be refused, or nullptr
  // if it's allowed -- returned as data, for a disabled combo entry + tooltip (this
  // editor's established modal substitute, e.g. MeshPanel.cpp's rail-height tooltip).
  const char* convertBlockedReason(PointKind newKind) const {
    if (!selectionInRange()) return "No point selected.";
    const Path& path = track_.paths[selection_.pathIndex];
    const TrackPoint& point = path.points[selection_.pointIndex];
    if (point.kind == newKind) return "Already this type.";
    if (point.kind == PointKind::Position) {
      if (sharedPositionOccurrences(point.id) > 1) return "Reconnect this shared/disjoint point before converting it.";
      if (positionCount(path) <= 4) return "A track path needs at least 4 position control points.";
      return nullptr;
    }
    const int count =
        static_cast<int>(std::count_if(path.points.begin(), path.points.end(), [&](const TrackPoint& p) { return p.kind == point.kind; }));
    if (count <= 2) {
      switch (point.kind) {
        case PointKind::Roll: return "A path needs at least 2 roll points.";
        case PointKind::Width: return "A path needs at least 2 width points.";
        case PointKind::CrossSection: return "A path needs at least 2 cross-section points.";
        default: break;
      }
    }
    return nullptr;
  }

  // Converts the selected point to `newKind`: removes the current point, then seeds the freshly
  // created point of the new kind by evaluating the *existing* points of that kind at the same t
  // (the current point's own t, or its position-space index/N for a Position point). curKind !=
  // newKind always (guarded by convertBlockedReason above), so removing curObj never changes the
  // list being evaluated -- there is no "recompute remaining points first" step needed here.
  //
  // `positionXYZ` supplies the world position for a conversion TO Position -- ignored for every
  // other target kind. The caller (PropertiesPanel.cpp, which has the baked tox::Track this class
  // deliberately keeps out of EditorState/EditorTrackDefinition.hpp) computes it by sampling the
  // baked centerline at g = t * gMax, the same approximation TopDownCanvas.cpp's own "Position"
  // context-menu item already uses -- exact here, not
  // just approximate, since only Roll/Width/CrossSection points can convert TO Position, and none
  // of those affect the baked centerline's X/Y/Z, only the ribbon's cross-section.
  //
  // Refuses (returns false, no history push) exactly when convertBlockedReason(newKind) is non-null.
  bool convertSelectedPoint(PointKind newKind, const tox::Vec3& positionXYZ) {
    if (convertBlockedReason(newKind) != nullptr) return false;
    Path& path = track_.paths[selection_.pathIndex];
    const TrackPoint curObj = path.points[selection_.pointIndex];  // copy: erase() below invalidates any reference

    double t;
    if (curObj.kind == PointKind::Position) {
      const int n = positionCount(path);
      const int idx = rawIndexToPositionIndex(path, selection_.pointIndex);
      t = path.closed ? static_cast<double>(idx) / n : static_cast<double>(idx) / static_cast<double>(std::max(1, n - 1));
    } else {
      t = curObj.t;
    }

    history_.push(track_);
    path.points.erase(path.points.begin() + selection_.pointIndex);

    TrackPoint created;
    created.kind = newKind;
    int newRawIndex;
    if (newKind == PointKind::Position) {
      created.id = newPointId();
      created.pos = positionXYZ;
      created.weight = 1.0;
      const int n = positionCount(path);
      const int insertIdx = std::clamp(static_cast<int>(std::lround(t * static_cast<double>(path.closed ? n : std::max(1, n - 1)))), 0, n);
      const int rawAt = insertIdx < n ? positionIndexToRaw(path, insertIdx) : static_cast<int>(path.points.size());
      path.points.insert(path.points.begin() + rawAt, created);
      newRawIndex = rawAt;
    } else {
      created.t = t;
      if (newKind == PointKind::Roll) {
        std::vector<std::pair<double, double>> samples;
        for (const auto& p : path.points)
          if (p.kind == PointKind::Roll) samples.emplace_back(p.t, p.roll);
        created.roll = samples.empty() ? TrackPoint{}.roll : std::round(evalScalarSpline(samples, path.closed, t) * 10.0) / 10.0;
      } else if (newKind == PointKind::Width) {
        std::vector<std::pair<double, double>> samples;
        for (const auto& p : path.points)
          if (p.kind == PointKind::Width) samples.emplace_back(p.t, p.width);
        const double width = samples.empty() ? TrackPoint{}.width : std::max(1.0, evalScalarSpline(samples, path.closed, t));
        created.width = std::round(width * 10.0) / 10.0;
      } else {  // CrossSection
        std::vector<std::pair<double, double>> curvSamples, tightSamples, thickSamples;
        for (const auto& p : path.points) {
          if (p.kind != PointKind::CrossSection) continue;
          curvSamples.emplace_back(p.t, p.curvature);
          tightSamples.emplace_back(p.t, p.tightness);
          thickSamples.emplace_back(p.t, p.thickness);
        }
        const double curvature = curvSamples.empty() ? TrackPoint{}.curvature : std::clamp(evalScalarSpline(curvSamples, path.closed, t), -1.0, 1.0);
        const double tightness = tightSamples.empty() ? TrackPoint{}.tightness : std::clamp(evalScalarSpline(tightSamples, path.closed, t), 0.2, 4.0);
        const double thickness = thickSamples.empty() ? TrackPoint{}.thickness : std::max(0.0, evalScalarSpline(thickSamples, path.closed, t));
        created.curvature = std::round(curvature * 100.0) / 100.0;
        created.tightness = std::round(tightness * 10.0) / 10.0;
        created.thickness = std::round(thickness * 10.0) / 10.0;
      }
      path.points.push_back(created);
      newRawIndex = static_cast<int>(path.points.size()) - 1;
    }
    selectPoint(selection_.pathIndex, newRawIndex);
    return true;
  }

  // Track name field: commits once when the caller's text field is deactivated
  // after an edit (ImGui::IsItemDeactivatedAfterEdit) -- a whole typing session collapses into one
  // undo step without needing focus/blur state threaded in from the caller -- consistent with every
  // other typed-field setter in this file (setSelectedPositionFields, editAuxPoint), which all
  // commit once rather than living-sync every keystroke. An empty name is accepted here; only
  // serialization falls back to "Untitled Track" (toJson).
  bool setTrackName(const std::string& name) {
    if (name == track_.name) return false;
    history_.push(track_);
    track_.name = name;
    return true;
  }

  // Cycles a self-intersection crossing's override: none -> keep -> collapse -> none, including
  // the order-insensitive (a,b) == (b,a) match on both sides. Always pushes undo unconditionally --
  // there's no guard here, every call legitimately changes something.
  void cycleCrossingOverride(const std::string& side, const std::string& a, const std::string& b) {
    history_.push(track_);
    const auto it = std::find_if(track_.selfIntersectionOverrides.begin(), track_.selfIntersectionOverrides.end(),
                                 [&](const SelfIntersectionOverride& o) {
                                   return o.side == side && ((o.a == a && o.b == b) || (o.a == b && o.b == a));
                                 });
    if (it == track_.selfIntersectionOverrides.end()) {
      SelfIntersectionOverride created;
      created.side = side;
      created.a = a;
      created.b = b;
      created.action = "keep";
      track_.selfIntersectionOverrides.push_back(std::move(created));
    } else if (it->action == "keep") {
      it->action = "collapse";
    } else {
      track_.selfIntersectionOverrides.erase(it);
    }
  }

  // Direction toggle: clampStart() first (start may be stale from a prior structural edit), then
  // flip the flag.
  void toggleStartReverse() {
    clampStart();
    history_.push(track_);
    track_.start.reverse = !track_.start.reverse;
  }

  // "Set as start point": repoints track_.start at the current selection, keeping the existing
  // reverse flag. No-ops when the selection isn't a Position point or is already the start point
  // (the panel disables the button in that state).
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
  // already is.
  bool isStartPoint(int pathIndex, int pointIndex) const {
    if (pathIndex < 0 || pathIndex >= static_cast<int>(track_.paths.size())) return false;
    return track_.start.path == pathIndex && track_.start.point == rawIndexToPositionIndex(track_.paths[pathIndex], pointIndex);
  }

  // Handling panel field-change handler: clamps each field to the same ranges
  // EditorTrackDefinition's fromJson uses (so a panel edit and a hand-edited JSON file converge on
  // the same value), then commits one undo step. Always pushes -- unlike
  // setTrackName/setSelectedPositionFields there's no cheap "did anything actually change" check
  // worth doing across four fields.
  void setHandling(double maxSpeed, double accel, double turnSpeed, double weight) {
    history_.push(track_);
    track_.handling.maxSpeed = std::clamp(maxSpeed, 10.0, 1000.0);
    track_.handling.accel = std::clamp(accel, 5.0, 1000.0);
    track_.handling.turnSpeed = std::clamp(turnSpeed, 10.0, 720.0);
    track_.handling.weight = std::clamp(weight, 50.0, 100000.0);
  }

  // Restores Handling{}'s own schema defaults.
  void resetHandling() {
    history_.push(track_);
    track_.handling = Handling{};
  }

  // Create mode: click adds a point to the in-progress draft, unless the click lands on the
  // draft's first point (closes as a closed path) or last point (finishes as open). Returns true if
  // the draft was just finished into a new path (the caller may want to switch back to Edit mode).
  // `worldX`/`worldZ` is used for hit-testing against the draft's own closing/finishing points
  // (unsnapped, matching the raw click position); `snappedX`/`snappedZ` is what a genuinely new
  // point gets, letting the caller apply grid-snap without it
  // fighting the pick tolerance on an already-placed draft point. Defaults to the raw click when
  // the caller has no snapping to apply.
  bool createModeClick(double worldX, double worldZ, double pickRadiusWorld, double snappedX, double snappedZ) {
    if (!createDraft_.empty()) {
      if (withinPick(createDraft_.front(), worldX, worldZ, pickRadiusWorld)) return finishCreateDraft(true);
      if (createDraft_.size() > 1 && withinPick(createDraft_.back(), worldX, worldZ, pickRadiusWorld)) return finishCreateDraft(false);
    }
    // The third axis outside the active plane defaults to 0 -- matching TopDown's own
    // longstanding behavior of starting every fresh draft point at y=0 regardless of anything else.
    tox::Vec3 pos(0.0, 0.0, 0.0);
    setPlaneCoords(projectionMode_, pos, std::round(snappedX * 10.0) / 10.0, std::round(snappedZ * 10.0) / 10.0);
    createDraft_.push_back(pos);
    return false;
  }
  bool createModeClick(double worldX, double worldZ, double pickRadiusWorld) {
    return createModeClick(worldX, worldZ, pickRadiusWorld, worldX, worldZ);
  }

  void cancelCreateDraft() { createDraft_.clear(); }

private:
  // Mirrors countPointOccurrences(point) > 1: a Position point shared across paths (a junction) or
  // aliased by a disjoint seam has the same id appear more than once. Used only by
  // convertBlockedReason's shared/disjoint guard.
  int sharedPositionOccurrences(const std::string& pointId) const {
    int count = 0;
    for (const auto& p : track_.paths)
      for (const auto& pt : p.points)
        if (pt.kind == PointKind::Position && pt.id == pointId) ++count;
    return count;
  }

  // Non-uniform Catmull-Rom/Hermite interpolation over a (t, value) point set, circular for closed
  // paths and clamped at the ends for open ones -- the same per-attribute spline TrackCore.evalRoll/
  // evalWidth/evalCrossSection* wrap, independent of the rational position spline core's own baker
  // uses. Used only by convertSelectedPoint to seed a freshly converted
  // roll/width/crossSection point from its neighbours. `points` must be non-empty (every caller
  // checks first) and sorted by t (matches every caller, which builds it directly from path.points
  // in authored order -- schema load already enforces aux points sorted by t).
  static double evalScalarSpline(const std::vector<std::pair<double, double>>& points, bool closed, double tQuery) {
    const int m = static_cast<int>(points.size());
    if (m == 1) return points[0].second;
    double t = tQuery;
    if (closed) {
      t = std::fmod(std::fmod(t, 1.0) + 1.0, 1.0);
    } else {
      t = std::clamp(t, points.front().first, points.back().first);
    }

    const auto idxT = [&](int i) -> std::pair<double, double> {
      if (closed) {
        const int k = ((i % m) + m) % m;
        const int cyc = (i - k) / m;
        return {points[k].first + cyc, points[k].second};
      }
      const int k = std::clamp(i, 0, m - 1);
      return {points[k].first, points[k].second};
    };

    int i = closed ? m - 1 : m - 2;
    for (int k = 0; k < m - 1; ++k) {
      if (t >= points[k].first && t < points[k + 1].first) {
        i = k;
        break;
      }
    }

    const auto p1 = idxT(i), p2 = idxT(i + 1);
    double tt = t;
    if (tt < p1.first) tt += 1.0;
    const double dt = (p2.first - p1.first) != 0.0 ? (p2.first - p1.first) : 1e-6;
    const double u = (tt - p1.first) / dt;

    const auto p0 = idxT(i - 1), p3 = idxT(i + 2);
    const double m1 = ((p2.second - p0.second) / ((p2.first - p0.first) != 0.0 ? (p2.first - p0.first) : 1e-6)) * dt;
    const double m2 = ((p3.second - p1.second) / ((p3.first - p1.first) != 0.0 ? (p3.first - p1.first) : 1e-6)) * dt;

    const double u2 = u * u, u3 = u2 * u;
    const double h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
    return h00 * p1.second + h10 * m1 + h01 * p2.second + h11 * m2;
  }

  // Empty assignments (new/legacy tracks) receive the current default. Non-empty unknown names
  // are preserved: a Resources XML may be edited while this process is running, and
  // "Refresh materials from XML" can make that assignment valid later. Save validation blocks
  // unresolved names rather than silently rewriting authored data.
  void backfillMaterials() {
    if (availableMaterials_.empty()) return;
    for (auto& path : track_.paths) {
      if (path.material.empty()) path.material = defaultMaterial();
    }
  }

  void replaceTrackKeepHistory(TrackDefinition replacement) {
    track_ = std::move(replacement);
    backfillPointIds(track_);  // see the constructor's comment on why this must never be skipped
    backfillMaterials();
    selection_ = {};
    dragging_ = false;
    dragMutated_ = false;
    createDraft_.clear();
    selectedZoneId_.reset();
    selectedTriggerId_.reset();
    selectedMeshObjectId_.reset();
    explicitCurrentPathIndex_ = 0;
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

  // SelectedPoint::valid() only checks that both indices are non-negative, not that they're
  // in-range for the CURRENT track -- every structural mutation today happens to clear the
  // selection first, so this was unreachable, but it was a latent out-of-bounds write one edit
  // away. Mutating methods that index by selection_ should use
  // this instead of selection_.valid() directly.
  bool selectionInRange() const {
    return selection_.pathIndex >= 0 && selection_.pathIndex < static_cast<int>(track_.paths.size()) && selection_.pointIndex >= 0 &&
           selection_.pointIndex < static_cast<int>(track_.paths[selection_.pathIndex].points.size());
  }

  // track_.start.point is a POSITION-only index (matches core's TrackDefinition.hpp/StartGrid.cpp:
  // clamped against positionCount, not points.size()) -- unlike SelectedPoint::pointIndex, which is
  // a raw index into Path::points (mixed position/roll/width/crossSection). These two convert
  // between them.
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

  // Re-finds the point that used to be at track_.start by id (first
  // match, scanning paths in authored order -- same as findPointOccurrence) so a structural edit
  // doesn't leave start silently pointing at a different physical point. Falls back to clampStart()
  // when the point is gone.
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

  // Keeps track_.start's indices in range after paths/points are added or
  // removed. Does not try to track "the same" point through a restructure that has no id match.
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

  // Clamps `list[justEdited]`'s t0/t1 order and range, floors its width, and pushes it clear of
  // every other reservation on the same path -- CENTRAL_RESERVATION_PLAN.md's auto-clamp decision
  // (no reachable invalid state, no warning UI). Finds the free [lo,hi] gap (bounded by whichever
  // other entries are nearest on each side of this entry's own midpoint) and clamps t0/t1 into it,
  // which guarantees non-overlap without ever having to touch another entry.
  static void clampReservation(std::vector<Reservation>& list, int justEdited) {
    Reservation& r = list[justEdited];
    // `width` is metres (Fixed) or a 0-100 percentage of the road's own width (Percent) -- see
    // tox::ReservationWidthMode. The old floor of 1.0 only made sense as a minimum metres value;
    // Percent just needs a valid percentage.
    r.width = r.widthMode == ReservationWidthMode::Percent ? std::clamp(r.width, 0.0, 100.0) : std::max(1.0, r.width);
    // End-cap width never widens past the reservation's own midpoint width (a Mitred/Rounded end
    // narrower than the reservation makes a taper; wider would make it flare out instead). Only
    // enforceable here in Fixed mode, where `r.width` is metres -- in Percent mode it's a 0-100
    // number, not comparable, so the bake clamps the cap against the actual local peak width
    // instead (TrackBake.cpp's reservationHalfGapAt).
    // `noseLength` needs no upper clamp -- the bake maxes the dome against the base taper, so an
    // over-long nose is simply swallowed by it rather than producing anything invalid.
    if (r.widthMode == ReservationWidthMode::Fixed) {
      r.endCap0.width = std::clamp(r.endCap0.width, 0.0, r.width);
      r.endCap1.width = std::clamp(r.endCap1.width, 0.0, r.width);
    } else {
      r.endCap0.width = std::max(0.0, r.endCap0.width);
      r.endCap1.width = std::max(0.0, r.endCap1.width);
    }
    r.endCap0.noseLength = std::max(0.0, r.endCap0.noseLength);
    r.endCap1.noseLength = std::max(0.0, r.endCap1.noseLength);
    r.t0 = std::clamp(r.t0, 0.0, 1.0);
    r.t1 = std::clamp(r.t1, 0.0, 1.0);
    if (r.t0 > r.t1) std::swap(r.t0, r.t1);

    const double mid = (r.t0 + r.t1) / 2;
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
      if (i == justEdited) continue;
      const Reservation& other = list[i];
      if (other.t1 <= mid) lo = std::max(lo, other.t1);
      if (other.t0 >= mid) hi = std::min(hi, other.t0);
    }
    r.t0 = std::clamp(r.t0, lo, hi);
    r.t1 = std::clamp(r.t1, lo, hi);
    constexpr double kMinSpan = 0.01;
    if (r.t1 - r.t0 < kMinSpan) {
      r.t0 = std::max(lo, std::min(r.t0, hi - kMinSpan));
      r.t1 = std::min(hi, r.t0 + kMinSpan);
    }
  }

  // Scans for the first unused "<prefix><N>" id starting at N=1, collision-proof by construction
  // (as opposed to an ever-incrementing-but-never-seeded counter, which would collide with ids
  // already present in a loaded track). `reserved` lets a caller mint several ids in one call before any of
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

  // "z" prefix for zones.
  std::string newZoneId() const {
    std::set<std::string> used;
    for (const auto& zone : track_.zones) used.insert(zone.id);
    return firstUnusedId("z", used);
  }

  // "tr" prefix for triggers.
  std::string newTriggerId() const {
    std::set<std::string> used;
    for (const auto& trigger : track_.triggers) used.insert(trigger.id);
    return firstUnusedId("tr", used);
  }

  // "res" prefix. Reservations are stored per-path
  // (PathDefinition::reservations) but share one id namespace across every path, same as zones/
  // triggers' own single flat namespaces (CENTRAL_RESERVATION_PLAN.md).
  std::string newReservationId() const {
    std::set<std::string> used;
    for (const auto& path : track_.paths)
      for (const auto& reservation : path.reservations) used.insert(reservation.id);
    return firstUnusedId("res", used);
  }

  // "mo" prefix, for drivable mesh object placements (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 5).
  std::string newMeshObjectId() const {
    std::set<std::string> used;
    for (const auto& placement : track_.meshObjects) used.insert(placement.id);
    return firstUnusedId("mo", used);
  }

  // "model" prefix, for embedded <Model> entries (TRACK_MODEL_LIST_PLAN.md Milestone 6).
  std::string newModelId() const {
    std::set<std::string> used;
    for (const auto& model : track_.models)
      if (model.id.has_value()) used.insert(*model.id);
    return firstUnusedId("model", used);
  }

  // Shared id space for junctions ("j" prefix) and disjoint seams ("seam" prefix) -- each still
  // scans only its own collection, since the two record kinds never share an id namespace.
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

    std::set<std::string> pathIds;
    for (const auto& p : track_.paths) pathIds.insert(p.id);
    std::set<std::string> meshObjectIds;
    for (const auto& p : track_.meshObjects) meshObjectIds.insert(p.id);
    // A zone/trigger is path-hosted or drivable-mesh-object-hosted (DRIVABLE_MESH_OBJECTS_PLAN.md
    // Milestone 3.5/5) -- only the host kind actually in use needs its own reference to still
    // resolve; the other host's now-empty field (pathId for a meshObject host, meshObjectId for a
    // path host) is never checked.
    auto zoneHostValid = [&](const Zone& z) {
      return z.host.kind == "meshObject" ? meshObjectIds.count(z.host.meshObjectId) > 0 : pathIds.count(z.host.pathId) > 0;
    };
    auto triggerHostValid = [&](const Trigger& t) {
      return t.host.kind == "meshObject" ? meshObjectIds.count(t.host.meshObjectId) > 0 : pathIds.count(t.host.pathId) > 0;
    };
    track_.zones.erase(std::remove_if(track_.zones.begin(), track_.zones.end(), [&](const Zone& z) { return !zoneHostValid(z); }),
                       track_.zones.end());
    track_.triggers.erase(
        std::remove_if(track_.triggers.begin(), track_.triggers.end(), [&](const Trigger& t) { return !triggerHostValid(t); }),
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

  // Extracts/writes the two axes ProjectionMode's drag/render plane covers: TopDown -> (x, z)
  // (today's only behavior, an existing convention left alone), Front -> (x, -y), Side -> (z, -y).
  // Shared by every hit-test/drag helper below so a screen-space (u, v) from any canvas projection
  // mode maps onto the right pair of world axes, leaving the third (the one that mode doesn't
  // expose) untouched. Y is negated in both Front and Side: the second slot here feeds
  // TopDownCanvas.cpp's worldToScreen as its screen-Y-bound argument, which increases the pixel
  // coordinate DOWNWARD -- without the negation, moving up in world Y would draw lower on screen.
  // Side's first slot is Z, not Y: looking along Side's view direction (X = 1) at the YZ plane, Z
  // is the "along the track" axis that belongs on the horizontal screen axis, matching X's role for
  // Front and TopDown. Mirrors TopDownCanvas.cpp's own free planeCoords/setPlaneCoords.
  static std::pair<double, double> planeCoords(ProjectionMode mode, const tox::Vec3& p) {
    switch (mode) {
      case ProjectionMode::Front: return {p.x, -p.y};
      case ProjectionMode::Side: return {p.z, -p.y};
      case ProjectionMode::TopDown:
      default: return {p.x, p.z};
    }
  }

  static void setPlaneCoords(ProjectionMode mode, tox::Vec3& p, double u, double v) {
    switch (mode) {
      case ProjectionMode::Front: p.x = u; p.y = -v; break;
      case ProjectionMode::Side: p.z = u; p.y = -v; break;
      case ProjectionMode::TopDown:
      default: p.x = u; p.z = v; break;
    }
  }

  bool withinPick(const tox::Vec3& p, double planeU, double planeV, double pickRadiusWorld) const {
    const auto [pu, pv] = planeCoords(projectionMode_, p);
    const double du = pu - planeU, dv = pv - planeV;
    return (du * du + dv * dv) <= pickRadiusWorld * pickRadiusWorld;
  }

  std::optional<SelectedPoint> hitTestPosition(double planeU, double planeV, double pickRadiusWorld) const {
    for (int pi = 0; pi < static_cast<int>(track_.paths.size()); ++pi) {
      const auto& points = track_.paths[pi].points;
      for (int i = 0; i < static_cast<int>(points.size()); ++i) {
        if (points[i].kind != PointKind::Position) continue;
        if (withinPick(points[i].pos, planeU, planeV, pickRadiusWorld)) return SelectedPoint{pi, i};
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
      createDraft_.clear();  // an editor-only guard, not a schema rule
      return false;
    }
    history_.push(track_);
    Path path;
    path.id = newPathId();
    path.closed = closed;
    path.material = defaultMaterial();
    // Points minted in this same loop aren't in track_ yet, so newPointId's scan can't see them --
    // `reserved` tracks ids minted so far this call so two points in one draft can never collide
    // with each other, only with what's already on the track.
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
  std::vector<std::string> availableMaterials_;  // sorted qualified names; see setAvailableMaterials
  EditMode mode_{EditMode::Edit};
  ProjectionMode projectionMode_{ProjectionMode::TopDown};
  SelectedPoint selection_;
  bool dragging_{false};
  bool dragMutated_{false};
  std::vector<tox::Vec3> createDraft_;

  // Generic shift+drag-to-rotate gesture state (see rotateGestureActive() above); unused by any
  // entity until Milestone 5.
  bool rotateGestureActive_{false};
  double rotateGestureOriginDeg_{0.0}, rotateGestureStartAngleDeg_{0.0};

  std::optional<std::string> selectedZoneId_;
  std::optional<std::string> selectedTriggerId_;
  std::optional<std::string> selectedReservationId_;
  std::optional<std::string> selectedMeshObjectId_;
  int explicitCurrentPathIndex_{0};
};

}  // namespace editor
