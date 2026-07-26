#include "PropertiesPanel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include "TrackCore.hpp"
#include "imgui.h"

namespace editor {
namespace {

constexpr ImGuiInputTextFlags kCommitOnEnter = ImGuiInputTextFlags_EnterReturnsTrue;
const ImU32 kCrossSectionStrokeColor = IM_COL32(213, 140, 255, 255);
const ImU32 kCrossSectionFillPositive = IM_COL32(213, 140, 255, 64);
const ImU32 kCrossSectionFillNegative = IM_COL32(255, 140, 213, 64);
const ImU32 kCrossSectionSlabFill = IM_COL32(120, 152, 184, 71);
const ImU32 kCrossSectionSlabStroke = IM_COL32(164, 192, 220, 140);
const ImU32 kCrossSectionChordColor = IM_COL32(111, 147, 168, 115);
const ImU32 kCrossSectionEdgeColor = IM_COL32(213, 140, 255, 255);
const ImU32 kCrossSectionTextColor = IM_COL32(205, 238, 255, 255);
const ImU32 kCrossSectionLabelColor = IM_COL32(111, 147, 168, 255);

// Road-surface width at a point's t, sampled (linearly interpolated) from the baked centerline --
// mirrors editor.js's TrackCore.evalWidth() closely enough for a preview (exact spline evaluation
// would need the authored width-point list, which PropertiesPanel doesn't otherwise touch).
double widthAtT(const tox::Track* baked, int pathIndex, bool closed, double t) {
  if (baked == nullptr || pathIndex < 0 || pathIndex >= static_cast<int>(baked->paths.size())) return tox::TrackCore::DEFAULT_WIDTH;
  const auto& centerline = baked->paths[pathIndex].centerline;
  const std::size_t n = centerline.size();
  if (n == 0) return tox::TrackCore::DEFAULT_WIDTH;
  if (n == 1) return centerline[0].width;
  const double frac = std::clamp(t, 0.0, 1.0);
  const double indexF = frac * static_cast<double>(closed ? n : n - 1);
  auto index0 = static_cast<std::size_t>(std::floor(indexF));
  const double fracIdx = indexF - static_cast<double>(index0);
  std::size_t index1;
  if (closed) {
    index0 %= n;
    index1 = (index0 + 1) % n;
  } else {
    index0 = std::min(index0, n - 1);
    index1 = std::min(index0 + 1, n - 1);
  }
  return centerline[index0].width + (centerline[index1].width - centerline[index0].width) * fracIdx;
}

// On-canvas cross-section preview (editor.html's #crossSectionPreview / drawCrossSectionPreview,
// js/editor.js:2032-2125): the same profile the game's ribbon mesh and USD exporter build, so this
// always shows the surface that will actually be baked, not just an abstract curvature/tightness
// readout. tox::TrackCore::crossSectionHeight is the exact function TrackBake.cpp uses.
void drawCrossSectionPreview(double curvature, double tightness, double thickness, double width) {
  constexpr float kPreviewHeight = 170.0f;
  constexpr float kPad = 18.0f;
  constexpr int kSteps = 48;

  ImGui::BeginChild("CrossSectionPreview", ImVec2(0, kPreviewHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImVec2 size = ImGui::GetContentRegionAvail();
  size.x = std::max(1.0f, size.x);
  size.y = std::max(1.0f, size.y);
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(7, 16, 25, 255));

  const double c = std::clamp(curvature, -1.0, 1.0);
  const double k = std::clamp(tightness, 0.2, 4.0);
  const double thick = std::max(0.0, thickness);
  const double w = std::max(width, 0.0);
  const auto heightAt = [&](double v) { return tox::TrackCore::crossSectionHeight(c, k, v, w); };

  double hiY = 0.0, loY = 0.0;
  for (int i = 0; i <= kSteps; ++i) {
    const double y = heightAt(static_cast<double>(i) / kSteps);
    hiY = std::max(hiY, y);
    loY = std::min(loY, y);
  }
  loY -= thick;
  const double midX = size.x / 2.0, midY = size.y / 2.0;
  const double scale = std::min((size.x - 2.0 * kPad) / std::max(w, 1.0), (size.y - 2.0 * kPad) / std::max(hiY - loY, 1.0));
  const double centreY = (hiY + loY) / 2.0;
  const auto px = [&](double x) { return static_cast<float>(midX + x * scale); };
  const auto py = [&](double y) { return static_cast<float>(midY - (y - centreY) * scale); };
  const auto vx = [&](double v) { return px(-w / 2.0 + w * v); };
  const auto surfacePoint = [&](int i, double offset) {
    const double v = static_cast<double>(i) / kSteps;
    return ImVec2(origin.x + vx(v), origin.y + py(heightAt(v) + offset));
  };

  // flat chord reference
  const float chordY = origin.y + py(0.0);
  drawList->AddLine(ImVec2(origin.x + kPad, chordY), ImVec2(origin.x + size.x - kPad, chordY), kCrossSectionChordColor, 1.0f);
  drawList->AddText(ImVec2(origin.x + kPad, chordY - 14.0f), kCrossSectionLabelColor, "flat");

  if (thick > 0.0) {
    // Extruded slab: road surface on top, the same profile offset `thick` below it.
    std::vector<ImVec2> poly;
    poly.reserve(2 * (kSteps + 1));
    for (int i = 0; i <= kSteps; ++i) poly.push_back(surfacePoint(i, 0.0));
    for (int i = kSteps; i >= 0; --i) poly.push_back(surfacePoint(i, -thick));
    drawList->AddConvexPolyFilled(poly.data(), static_cast<int>(poly.size()), kCrossSectionSlabFill);
    drawList->AddPolyline(poly.data(), static_cast<int>(poly.size()), kCrossSectionSlabStroke, ImDrawFlags_Closed, 1.0f);
  } else {
    // Zero thickness is still a legal sheet -- show curvature as the area between the flat chord
    // and the road surface.
    std::vector<ImVec2> poly;
    poly.reserve(kSteps + 3);
    poly.push_back(ImVec2(origin.x + vx(0.0), chordY));
    for (int i = 0; i <= kSteps; ++i) poly.push_back(surfacePoint(i, 0.0));
    poly.push_back(ImVec2(origin.x + vx(1.0), chordY));
    drawList->AddConvexPolyFilled(poly.data(), static_cast<int>(poly.size()), c >= 0.0 ? kCrossSectionFillPositive : kCrossSectionFillNegative);
  }

  // road surface curve, drawn last so it reads as the driving surface
  std::vector<ImVec2> surface;
  surface.reserve(kSteps + 1);
  for (int i = 0; i <= kSteps; ++i) surface.push_back(surfacePoint(i, 0.0));
  drawList->AddPolyline(surface.data(), static_cast<int>(surface.size()), kCrossSectionStrokeColor, ImDrawFlags_None, 2.5f);

  // edge markers and centre marker
  for (const double v : {0.0, 0.5, 1.0}) {
    const ImVec2 p(origin.x + vx(v), origin.y + py(heightAt(v)));
    drawList->AddCircleFilled(p, v == 0.5 ? 4.0f : 3.0f, v == 0.5 ? IM_COL32(255, 255, 255, 255) : kCrossSectionEdgeColor);
  }

  char left[64], right[64];
  std::snprintf(left, sizeof(left), "curvature %.2f * tightness %.2f", c, k);
  std::snprintf(right, sizeof(right), "width %.1f * thick %.1f", w, thick);
  drawList->AddText(ImVec2(origin.x + kPad, origin.y + size.y - 22.0f), kCrossSectionTextColor, left);
  const ImVec2 rightSize = ImGui::CalcTextSize(right);
  drawList->AddText(ImVec2(origin.x + size.x - kPad - rightSize.x, origin.y + size.y - 22.0f), kCrossSectionTextColor, right);

  ImGui::EndChild();
}

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

void drawCrossSectionFields(EditorState& state, const SelectedPoint& sel, const TrackPoint& point, const tox::Track* baked, bool pathClosed,
                             bool& mutated) {
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

  const double width = widthAtT(baked, sel.pathIndex, pathClosed, tPercent / 100.0);
  drawCrossSectionPreview(curvature, tightness, thickness, width);
}

// Read-only physics-sample info (EDITOR_PARITY_FIXES.md gap 10), mirroring renderProps()'s
// `if (physicsSel)` branch exactly: these are baked frames, not authored state, so there's
// nothing here to edit -- just the exact values physics consumes. Returns true if a selection was
// shown (caller should skip the normal point-fields body), matching JS's early-return precedence.
bool drawPhysicsSampleInfo(const TopDownView& view, const tox::Track* baked) {
  const auto& sel = view.physicsSelection();
  if (!sel.has_value() || baked == nullptr || sel->pathIndex < 0 || sel->pathIndex >= static_cast<int>(baked->paths.size())) return false;
  const tox::Path& path = baked->paths[sel->pathIndex];
  if (sel->frameIndex < 0 || sel->frameIndex >= static_cast<int>(path.centerline.size())) return false;
  const tox::Frame& frame = path.centerline[sel->frameIndex];
  const int n = static_cast<int>(path.centerline.size());
  const double t = path.closed ? static_cast<double>(sel->frameIndex) / n : (n > 1 ? static_cast<double>(sel->frameIndex) / (n - 1) : 0.0);

  char header[64];
  std::snprintf(header, sizeof(header), "Physics sample %d.%d", sel->pathIndex, sel->frameIndex);
  ImGui::TextColored(ImVec4(1.0f, 0.61f, 0.24f, 1.0f), "%s", header);
  ImGui::TextDisabled("baked frame %d of %d - %s path", sel->frameIndex + 1, n, path.closed ? "closed" : "open");

  const auto v3Row = [](const char* label, const tox::Vec3& v) {
    ImGui::Text("%s: %.3f, %.3f, %.3f", label, v.x, v.y, v.z);
  };
  ImGui::Text("t (param): %.4f", t);
  v3Row("Position", frame.pos);
  v3Row("Tangent", frame.tangent);
  ImGui::Text("Roll deg: %.2f", frame.roll * 180.0 / std::numbers::pi);
  ImGui::Text("Width: %.3f", frame.width);
  ImGui::Text("Half width: %.3f", frame.halfW);
  v3Row("Edge right", frame.edgeRight);
  v3Row("Normal", frame.normal);
  const tox::Vec3 left = frame.pos.clone().addScaledVector(frame.edgeRight, -frame.halfW);
  const tox::Vec3 right = frame.pos.clone().addScaledVector(frame.edgeRight, frame.halfW);
  v3Row("Left edge", left);
  v3Row("Right edge", right);
  return true;
}

}  // namespace

bool DrawPropertiesPanel(EditorState& state, int currentPathIndex, const TopDownView& view, const tox::Track* baked) {
  if (drawPhysicsSampleInfo(view, baked)) return false;

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
        drawCrossSectionFields(state, sel, point, baked, state.track().paths[sel.pathIndex].closed, mutated);
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
