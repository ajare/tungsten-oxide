// EditorState.hpp — mode/selection/drag/create-draft state for point editing
// (EDITOR_CPP_PORT_PLAN.md M3), mirroring js/editor.js's editMode/selectedPointId/dragging/
// createDraft globals and setEditMode/nodeAtTop/deleteSelected/createModeClick functions.
//
// Scope: position points only, one path at a time -- roll/width/cross-section handles, segment
// deletion/splitting, shared/disjoint point identity, and mesh/zone/trigger picking are all still
// editor.js-only (later milestones or out of scope). Rails mode is wired for mode-switching parity
// but is a no-op until mesh regions exist.
#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
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

class EditorState {
 public:
  explicit EditorState(TrackDefinition initial) : track_(std::move(initial)) {}

  const TrackDefinition& track() const { return track_; }
  EditMode mode() const { return mode_; }
  SelectedPoint selection() const { return selection_; }
  const std::vector<tox::Vec3>& createDraft() const { return createDraft_; }
  bool dragging() const { return dragging_; }

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
  }

  // Returns true if a position point was hit within `pickRadiusWorld` of (worldX, worldZ).
  // Selects it (Edit mode's plain click) but does not start a drag -- call beginDrag separately
  // once the caller knows the mouse is actually moving.
  bool selectPositionAt(double worldX, double worldZ, double pickRadiusWorld) {
    const auto hit = hitTestPosition(worldX, worldZ, pickRadiusWorld);
    if (!hit) return false;
    selection_ = *hit;
    return true;
  }

  void clearSelection() { selection_ = {}; }

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
    if (!dragging_ || !selection_.valid()) return;
    if (!dragMutated_) {
      history_.push(track_);
      dragMutated_ = true;
    }
    TrackPoint& point = track_.paths[selection_.pathIndex].points[selection_.pointIndex];
    point.pos.x = std::round(worldX * 10.0) / 10.0;
    point.pos.z = std::round(worldZ * 10.0) / 10.0;
  }

  void endDrag() {
    dragging_ = false;
    dragMutated_ = false;
  }

  // Mirrors deleteSelected(): refuses to drop a path below 4 position points (a track path needs
  // that many to bake). No shared/disjoint-id guard -- this editor doesn't alias points by id yet
  // (see EditorTrackDefinition.hpp).
  bool deleteSelectedPoint() {
    if (!selection_.valid()) return false;
    Path& path = track_.paths[selection_.pathIndex];
    const auto positionCount =
        std::count_if(path.points.begin(), path.points.end(), [](const TrackPoint& p) { return p.kind == PointKind::Position; });
    if (positionCount <= 4) return false;
    history_.push(track_);
    path.points.erase(path.points.begin() + selection_.pointIndex);
    selection_ = {};
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
    selection_ = {};
    dragging_ = false;
    dragMutated_ = false;
    createDraft_.clear();
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
    path.id = "path" + std::to_string(nextId_++);
    path.closed = closed;
    for (const auto& pos : createDraft_) {
      TrackPoint point;
      point.kind = PointKind::Position;
      point.id = "p" + std::to_string(nextId_++);
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
  int nextId_{1};
};

}  // namespace editor
