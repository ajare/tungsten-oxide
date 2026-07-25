#include "ElevationView.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

#include "imgui.h"

namespace editor {
namespace {

constexpr float kPadX = 30.0f;
constexpr float kPadY = 20.0f;
constexpr float kPointRadius = 4.0f;
constexpr float kPickRadiusPx = 10.0f;
const ImU32 kBackgroundColor = IM_COL32(8, 20, 29, 255);
const ImU32 kAxisColor = IM_COL32(255, 255, 255, 60);
const ImU32 kProfileColor = IM_COL32(120, 170, 220, 200);
const ImU32 kPointColor = IM_COL32(240, 200, 60, 255);
const ImU32 kSelectedPointColor = IM_COL32(255, 90, 90, 255);

struct Layout {
  float w{1.0f}, h{1.0f};
  double minY{-1.0}, maxY{1.0};
  float yScale{1.0f};
  bool closed{true};
  int positionCount{0};
};

float screenY(const Layout& layout, double y) { return (layout.h - kPadY) - static_cast<float>((y - layout.minY)) * layout.yScale; }

float screenX(const Layout& layout, int orderIndex) {
  const int denom = layout.closed ? std::max(1, layout.positionCount) : std::max(1, layout.positionCount - 1);
  const float frac = static_cast<float>(orderIndex) / static_cast<float>(denom);
  return kPadX + frac * (layout.w - 2.0f * kPadX);
}

double worldYAt(const Layout& layout, float screenYPx) { return layout.minY + ((layout.h - kPadY) - screenYPx) / layout.yScale; }

// Collects (rawPointIndex, y) for every Position-kind point in the path, in authored order --
// see ElevationView.hpp's header comment on why order stands in for true arc-length placement.
std::vector<std::pair<int, double>> collectPositionPoints(const Path& path) {
  std::vector<std::pair<int, double>> points;
  for (int i = 0; i < static_cast<int>(path.points.size()); ++i)
    if (path.points[i].kind == PointKind::Position) points.emplace_back(i, path.points[i].pos.y);
  return points;
}

Layout computeLayout(const std::vector<std::pair<int, double>>& points, bool closed, float w, float h) {
  Layout layout;
  layout.w = w;
  layout.h = h;
  layout.closed = closed;
  layout.positionCount = static_cast<int>(points.size());
  double minY = 1e300, maxY = -1e300;
  for (const auto& [index, y] : points) {
    minY = std::min(minY, y);
    maxY = std::max(maxY, y);
  }
  if (points.empty() || minY > maxY) {
    minY = -1.0;
    maxY = 1.0;
  }
  if (maxY - minY < 1.0) {
    // Flat or near-flat profiles still need visible headroom, matching editor.js padding a
    // degenerate [minY, maxY] out to a sane span rather than dividing by ~0.
    const double mid = (minY + maxY) / 2.0;
    minY = mid - 5.0;
    maxY = mid + 5.0;
  }
  layout.minY = minY;
  layout.maxY = maxY;
  layout.yScale = static_cast<float>((h - 2.0f * kPadY) / (maxY - minY));
  return layout;
}

void drawAxis(ImDrawList* drawList, const ImVec2& canvasOrigin, const Layout& layout) {
  drawList->AddLine(ImVec2(canvasOrigin.x + kPadX, canvasOrigin.y), ImVec2(canvasOrigin.x + kPadX, canvasOrigin.y + layout.h), kAxisColor);
  char label[32];
  std::snprintf(label, sizeof(label), "%.1f", layout.maxY);
  drawList->AddText(ImVec2(canvasOrigin.x + 2.0f, canvasOrigin.y + kPadY - 6.0f), kAxisColor, label);
  std::snprintf(label, sizeof(label), "%.1f", layout.minY);
  drawList->AddText(ImVec2(canvasOrigin.x + 2.0f, canvasOrigin.y + layout.h - kPadY - 6.0f), kAxisColor, label);
}

void drawBakedProfile(ImDrawList* drawList, const ImVec2& canvasOrigin, const Layout& layout, const tox::Path& bakedPath) {
  const std::size_t n = bakedPath.centerline.size();
  if (n < 2) return;
  std::vector<ImVec2> screen;
  screen.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    const float x = kPadX + (static_cast<float>(i) / static_cast<float>(n - 1)) * (layout.w - 2.0f * kPadX);
    const float y = screenY(layout, bakedPath.centerline[i].pos.y);
    screen.push_back(ImVec2(canvasOrigin.x + x, canvasOrigin.y + y));
  }
  drawList->AddPolyline(screen.data(), static_cast<int>(screen.size()), kProfileColor, ImDrawFlags_None, 2.0f);
}

}  // namespace

bool DrawElevationView(EditorState& state, const tox::Track* baked, int pathIndex) {
  bool mutated = false;

  ImGui::BeginChild("ElevationView", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
  ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  canvasSize.x = std::max(1.0f, canvasSize.x);
  canvasSize.y = std::max(1.0f, canvasSize.y);

  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(canvasOrigin, ImVec2(canvasOrigin.x + canvasSize.x, canvasOrigin.y + canvasSize.y), kBackgroundColor);

  if (pathIndex < 0 || pathIndex >= static_cast<int>(state.track().paths.size())) {
    ImGui::TextUnformatted("No path to show -- create one first.");
    ImGui::EndChild();
    return false;
  }

  const Path& path = state.track().paths[pathIndex];
  const std::vector<std::pair<int, double>> points = collectPositionPoints(path);
  const Layout layout = computeLayout(points, path.closed, canvasSize.x, canvasSize.y);

  ImGui::InvisibleButton("elevationViewInput", canvasSize, ImGuiButtonFlags_MouseButtonLeft);
  const bool hovered = ImGui::IsItemHovered();
  const ImVec2 mouseLocal = ImVec2(ImGui::GetIO().MousePos.x - canvasOrigin.x, ImGui::GetIO().MousePos.y - canvasOrigin.y);

  // The current selection only drives dragging here if it's a position point on THIS path --
  // otherwise a point selected in the top-down view on another path would spuriously start
  // moving the instant the mouse is dragged over the elevation panel.
  const SelectedPoint selection = state.selection();
  const bool selectionOnThisPath = selection.valid() && selection.pathIndex == pathIndex && path.points[selection.pointIndex].kind == PointKind::Position;

  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    int hitIndex = -1;
    float bestDistSq = kPickRadiusPx * kPickRadiusPx;
    for (int order = 0; order < static_cast<int>(points.size()); ++order) {
      const float px = screenX(layout, order), py = screenY(layout, points[order].second);
      const float dx = mouseLocal.x - px, dy = mouseLocal.y - py;
      const float distSq = dx * dx + dy * dy;
      if (distSq <= bestDistSq) {
        bestDistSq = distSq;
        hitIndex = points[order].first;
      }
    }
    if (hitIndex >= 0) state.selectPoint(pathIndex, hitIndex);
  }

  if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) && selectionOnThisPath) {
    if (!state.dragging()) state.beginDrag();
    state.dragSelectedElevationTo(worldYAt(layout, mouseLocal.y));
    mutated = true;
  } else if (state.dragging() && selectionOnThisPath && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    state.endDrag();
  }

  drawAxis(drawList, canvasOrigin, layout);
  if (baked != nullptr && pathIndex < static_cast<int>(baked->paths.size())) drawBakedProfile(drawList, canvasOrigin, layout, baked->paths[pathIndex]);
  for (int order = 0; order < static_cast<int>(points.size()); ++order) {
    const bool isSelected = selectionOnThisPath && selection.pointIndex == points[order].first;
    const ImVec2 screen(canvasOrigin.x + screenX(layout, order), canvasOrigin.y + screenY(layout, points[order].second));
    drawList->AddCircleFilled(screen, isSelected ? kPointRadius + 2.0f : kPointRadius, isSelected ? kSelectedPointColor : kPointColor);
  }

  ImGui::EndChild();
  return mutated;
}

}  // namespace editor
