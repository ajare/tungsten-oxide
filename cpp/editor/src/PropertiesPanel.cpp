#include "PropertiesPanel.hpp"

#include <algorithm>
#include <cstdio>

#include "imgui.h"

namespace editor {
namespace {

constexpr ImGuiInputTextFlags kCommitOnEnter = ImGuiInputTextFlags_EnterReturnsTrue;

void drawPositionFields(EditorState& state, const SelectedPoint& sel, const TrackPoint& point, bool& mutated) {
  double x = point.pos.x, y = point.pos.y, z = point.pos.z, weight = point.weight;
  bool changed = false;
  ImGui::TextUnformatted("Position Point");
  ImGui::SetNextItemWidth(120);
  changed |= ImGui::InputDouble("X", &x, 0.0, 0.0, "%.1f", kCommitOnEnter);
  ImGui::SetNextItemWidth(120);
  changed |= ImGui::InputDouble("Y (elevation)", &y, 0.0, 0.0, "%.1f", kCommitOnEnter);
  ImGui::SetNextItemWidth(120);
  changed |= ImGui::InputDouble("Z", &z, 0.0, 0.0, "%.1f", kCommitOnEnter);
  ImGui::SetNextItemWidth(120);
  changed |= ImGui::InputDouble("Weight", &weight, 0.0, 0.0, "%.2f", kCommitOnEnter);
  if (changed && state.setSelectedPositionFields(x, y, z, weight)) mutated = true;

  // "Set as start point" (EDITOR_PARITY_FIXES.md gap 6), mirrors editor.html's #startBtn: disabled
  // once this already is the start point, same as JS's `${isStart ? 'disabled' : ''}`.
  const bool isStart = state.isStartPoint(sel.pathIndex, sel.pointIndex);
  ImGui::BeginDisabled(isStart);
  if (ImGui::Button(isStart ? "This is the start point" : "Set as start point")) {
    if (state.setStartPoint()) mutated = true;
  }
  ImGui::EndDisabled();

  // Disjoint (EDITOR_PARITY_FIXES.md gap 5): mirrors editor.html's #disjointChk. Checking splits
  // this point into a hard, unsmoothed seam (EditorState::makeDisjoint); unchecking merges it back
  // (reconnectDisjoint). Silently no-ops when disallowed (open endpoint, or fewer than 4 position
  // points on either side) rather than JS's alert() -- there's no modal dialog plumbing here yet.
  const auto seamIt = std::find_if(state.disjointSeams().begin(), state.disjointSeams().end(),
                                   [&](const Connection& s) { return s.pointId == point.id; });
  const bool isDisjoint = seamIt != state.disjointSeams().end();
  bool checked = isDisjoint;
  if (ImGui::Checkbox("Disjoint (hard seam)", &checked)) {
    if (checked && !isDisjoint) {
      if (state.makeDisjoint(sel.pathIndex, sel.pointIndex)) mutated = true;
    } else if (!checked && isDisjoint) {
      if (state.reconnectDisjoint(seamIt->id)) mutated = true;
    }
  }

  // Delete outgoing/incoming segment (EDITOR_PARITY_FIXES.md gap 11), mirrors editor.html's
  // #delSegmentBtn/#delPrevSegmentBtn: rendered only when that direction's segment actually
  // exists (an open path's last/first point has no outgoing/incoming segment), same as JS's
  // `if (outgoingSeg)`/`if (incomingSeg)` guards around the button markup.
  const auto outgoingSeg = state.selectedOutgoingSegment();
  const auto incomingSeg = state.selectedIncomingSegment();
  if (outgoingSeg.has_value() && ImGui::Button("Delete Outgoing Segment")) {
    if (state.deleteSelectedSegment(true)) mutated = true;
  }
  if (incomingSeg.has_value() && ImGui::Button("Delete Incoming Segment")) {
    if (state.deleteSelectedSegment(false)) mutated = true;
  }
}

void drawRollFields(EditorState& state, const SelectedPoint& sel, const TrackPoint& point, bool& mutated) {
  double tPercent = point.t * 100.0, rollDeg = point.roll;
  bool changed = false;
  ImGui::TextUnformatted("Roll Point");
  ImGui::SetNextItemWidth(120);
  changed |= ImGui::InputDouble("T (%)", &tPercent, 0.0, 0.0, "%.1f", kCommitOnEnter);
  ImGui::SetNextItemWidth(120);
  changed |= ImGui::InputDouble("Roll (deg)", &rollDeg, 0.0, 0.0, "%.1f", kCommitOnEnter);
  if (changed)
    mutated |= state.editAuxPoint(sel.pathIndex, sel.pointIndex, [&](TrackPoint& p) {
      p.t = tPercent / 100.0;
      p.roll = rollDeg;
    });
}

void drawWidthFields(EditorState& state, const SelectedPoint& sel, const TrackPoint& point, bool& mutated) {
  double tPercent = point.t * 100.0, width = point.width;
  bool changed = false;
  ImGui::TextUnformatted("Width Point");
  ImGui::SetNextItemWidth(120);
  changed |= ImGui::InputDouble("T (%)", &tPercent, 0.0, 0.0, "%.1f", kCommitOnEnter);
  ImGui::SetNextItemWidth(120);
  changed |= ImGui::InputDouble("Width", &width, 0.0, 0.0, "%.1f", kCommitOnEnter);
  if (changed)
    mutated |= state.editAuxPoint(sel.pathIndex, sel.pointIndex, [&](TrackPoint& p) {
      p.t = tPercent / 100.0;
      p.width = width;
    });
}

void drawCrossSectionFields(EditorState& state, const SelectedPoint& sel, const TrackPoint& point, bool& mutated) {
  double tPercent = point.t * 100.0, curvature = point.curvature, tightness = point.tightness, thickness = point.thickness;
  bool changed = false;
  ImGui::TextUnformatted("Cross-Section Point");
  ImGui::SetNextItemWidth(120);
  changed |= ImGui::InputDouble("T (%)", &tPercent, 0.0, 0.0, "%.1f", kCommitOnEnter);
  ImGui::SetNextItemWidth(120);
  changed |= ImGui::InputDouble("Curvature", &curvature, 0.0, 0.0, "%.2f", kCommitOnEnter);
  ImGui::SetNextItemWidth(120);
  changed |= ImGui::InputDouble("Tightness", &tightness, 0.0, 0.0, "%.2f", kCommitOnEnter);
  ImGui::SetNextItemWidth(120);
  changed |= ImGui::InputDouble("Thickness", &thickness, 0.0, 0.0, "%.2f", kCommitOnEnter);
  if (changed)
    mutated |= state.editAuxPoint(sel.pathIndex, sel.pointIndex, [&](TrackPoint& p) {
      p.t = tPercent / 100.0;
      p.curvature = curvature;
      p.tightness = tightness;
      p.thickness = thickness;
    });
}

}  // namespace

bool DrawPropertiesPanel(EditorState& state, int currentPathIndex) {
  bool mutated = false;
  const SelectedPoint sel = state.selection();
  const bool selectionValid = sel.valid() && sel.pathIndex < static_cast<int>(state.track().paths.size()) &&
                              sel.pointIndex < static_cast<int>(state.track().paths[sel.pathIndex].points.size());

  if (selectionValid) {
    const TrackPoint& point = state.track().paths[sel.pathIndex].points[sel.pointIndex];
    switch (point.kind) {
      case PointKind::Position:
        drawPositionFields(state, sel, point, mutated);
        break;
      case PointKind::Roll:
        drawRollFields(state, sel, point, mutated);
        break;
      case PointKind::Width:
        drawWidthFields(state, sel, point, mutated);
        break;
      case PointKind::CrossSection:
        drawCrossSectionFields(state, sel, point, mutated);
        break;
    }
    if (ImGui::Button("Delete Point")) {
      if (state.deleteSelectedPoint()) mutated = true;
    }
  } else {
    ImGui::TextUnformatted("No point selected.");
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Add roll / width / cross-section point:");
  const bool hasPath = currentPathIndex >= 0 && currentPathIndex < static_cast<int>(state.track().paths.size());
  static float addTPercent = 50.0f;
  ImGui::SetNextItemWidth(160);
  ImGui::SliderFloat("t (%)##addAuxT", &addTPercent, 0.0f, 100.0f);
  ImGui::BeginDisabled(!hasPath);
  if (ImGui::Button("Add Roll")) {
    if (state.addAuxPoint(currentPathIndex, PointKind::Roll, addTPercent / 100.0).has_value()) mutated = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Add Width")) {
    if (state.addAuxPoint(currentPathIndex, PointKind::Width, addTPercent / 100.0).has_value()) mutated = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Add Cross-Section")) {
    if (state.addAuxPoint(currentPathIndex, PointKind::CrossSection, addTPercent / 100.0).has_value()) mutated = true;
  }
  ImGui::EndDisabled();
  if (!hasPath) ImGui::TextUnformatted("(select or create a path first)");

  // There's no on-canvas handle to click for these (see this file's header comment), so once a
  // roll/width/crossSection point is deselected -- by clicking a position point, say -- there'd
  // otherwise be no way back to it at all. A flat clickable list is the minimum viable substitute.
  if (hasPath) {
    ImGui::Separator();
    ImGui::TextUnformatted("Existing roll / width / cross-section points on this path:");
    const auto& points = state.track().paths[currentPathIndex].points;
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
      const TrackPoint& p = points[i];
      if (p.kind == PointKind::Position) continue;
      const bool isSelected = selectionValid && sel.pathIndex == currentPathIndex && sel.pointIndex == i;
      char label[64];
      if (p.kind == PointKind::Roll)
        std::snprintf(label, sizeof(label), "Roll  t=%.1f%%  roll=%.1f##aux%d", p.t * 100.0, p.roll, i);
      else if (p.kind == PointKind::Width)
        std::snprintf(label, sizeof(label), "Width t=%.1f%%  width=%.1f##aux%d", p.t * 100.0, p.width, i);
      else
        std::snprintf(label, sizeof(label), "XSec  t=%.1f%%  curv=%.2f##aux%d", p.t * 100.0, p.curvature, i);
      if (ImGui::Selectable(label, isSelected)) state.selectPoint(currentPathIndex, i);
    }
  }

  return mutated;
}

}  // namespace editor
