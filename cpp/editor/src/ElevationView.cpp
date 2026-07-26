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
// Mirrors TopDownCanvas.cpp's kSelectedOutlineColor: a crisp white "handle" border right at the
// (already-larger) fill's edge -- unmistakable as selected regardless of hover state.
const ImU32 kSelectedOutlineColor = IM_COL32(255, 255, 255, 255);
// Mirrors TopDownCanvas.cpp's kHoverRingColor: a separate, softer ring further out from the fill,
// so it never gets confused with the tighter selection border even when both apply.
const ImU32 kHoverRingColor = IM_COL32(255, 255, 255, 140);

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

struct YRange {
  double minY, maxY;
};

// Raw (unpadded) extent of the profile's points, degenerating to [-1, 1] when there's nothing to
// show. Split out from computeLayout() so a drag gesture can combine this frame's raw extent with
// the extent frozen at drag-start (see DrawElevationView) without re-deriving the flat-profile
// padding twice.
YRange rawYRange(const std::vector<std::pair<int, double>>& points) {
  double minY = 1e300, maxY = -1e300;
  for (const auto& [index, y] : points) {
    minY = std::min(minY, y);
    maxY = std::max(maxY, y);
  }
  if (points.empty() || minY > maxY) return {-1.0, 1.0};
  return {minY, maxY};
}

Layout layoutFromRange(double minY, double maxY, bool closed, int positionCount, float w, float h) {
  Layout layout;
  layout.w = w;
  layout.h = h;
  layout.closed = closed;
  layout.positionCount = positionCount;
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

Layout computeLayout(const std::vector<std::pair<int, double>>& points, bool closed, float w, float h) {
  const YRange range = rawYRange(points);
  return layoutFromRange(range.minY, range.maxY, closed, static_cast<int>(points.size()), w, h);
}

void drawAxis(ImDrawList* drawList, const ImVec2& canvasOrigin, const Layout& layout) {
  drawList->AddLine(ImVec2(canvasOrigin.x + kPadX, canvasOrigin.y), ImVec2(canvasOrigin.x + kPadX, canvasOrigin.y + layout.h), kAxisColor);
  char label[32];
  std::snprintf(label, sizeof(label), "%.1f", layout.maxY);
  drawList->AddText(ImVec2(canvasOrigin.x + 2.0f, canvasOrigin.y + kPadY - 6.0f), kAxisColor, label);
  std::snprintf(label, sizeof(label), "%.1f", layout.minY);
  drawList->AddText(ImVec2(canvasOrigin.x + 2.0f, canvasOrigin.y + layout.h - kPadY - 6.0f), kAxisColor, label);
}

// Nearest position-point's raw Path::points index to `mouseLocal`, within kPickRadiusPx, or -1.
// Shared by click-to-select and hover-highlight so they always agree on what's "under the cursor".
int nearestPointIndex(const std::vector<std::pair<int, double>>& points, const Layout& layout, const ImVec2& mouseLocal) {
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
  return hitIndex;
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
  const Layout liveLayout = computeLayout(points, path.closed, canvasSize.x, canvasSize.y);
  // Mirrors TopDownView::freezeBounds: the dragged point's own y-value feeds back into the range
  // (and therefore yScale) every frame, so recomputing the layout from scratch each frame runs
  // away -- growing the y-span SHRINKS yScale, which INCREASES world-units-per-pixel, so the
  // further you drag, the faster it goes. An expand-only range (grow to keep the dragged point
  // framed, never shrink) was tried and rejected: it still grows the span as the point moves
  // further out, so it reproduces the exact same runaway. Freezing the whole layout at drag-start
  // is the only fix that keeps sensitivity constant for the gesture; the tradeoff is a point
  // dragged far enough draws outside the visible canvas until release, at which point the layout
  // is recomputed fresh (see below) and the view snaps to fit it.
  static std::optional<Layout> frozenLayout;
  const Layout& layout = frozenLayout.has_value() ? *frozenLayout : liveLayout;

  ImGui::InvisibleButton("elevationViewInput", canvasSize, ImGuiButtonFlags_MouseButtonLeft);
  const bool hovered = ImGui::IsItemHovered();
  const bool itemActive = ImGui::IsItemActive();
  const ImVec2 mouseLocal = ImVec2(ImGui::GetIO().MousePos.x - canvasOrigin.x, ImGui::GetIO().MousePos.y - canvasOrigin.y);

  // The current selection only drives dragging here if it's a position point on THIS path --
  // otherwise a point selected in the top-down view on another path would spuriously start
  // moving the instant the mouse is dragged over the elevation panel.
  const SelectedPoint selection = state.selection();
  const bool selectionOnThisPath = selection.valid() && selection.pathIndex == pathIndex && path.points[selection.pointIndex].kind == PointKind::Position;

  // Hover highlight (distinct from click-driven selection): mirrors TopDownCanvas.cpp's, and
  // shares the same nearest-point search the click handler below uses, so hovering and clicking
  // always agree on which point is "under the cursor".
  const int hoveredRawIndex = hovered ? nearestPointIndex(points, layout, mouseLocal) : -1;

  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    const int hitIndex = nearestPointIndex(points, layout, mouseLocal);
    if (hitIndex >= 0) state.selectPoint(pathIndex, hitIndex);
  }

  // Gated on itemActive (this canvas's own InvisibleButton captured the mouse-down), not just a
  // global drag gesture -- ImGui::IsMouseDragging() alone is true regardless of which window's
  // widget is actually being dragged, so without this gate, dragging a point in the top-down
  // view's own canvas (a separate InvisibleButton) would ALSO be seen as a drag here on every
  // frame both windows draw, spuriously overwriting the selected point's Y with wherever the
  // mouse happens to be over THIS view. Mirrors TopDownCanvas.cpp's same fix for the reverse
  // direction (elevation drags spuriously moving X/Z in the top-down view).
  if (itemActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) && selectionOnThisPath) {
    if (!state.dragging()) {
      state.beginDrag();
      frozenLayout = liveLayout;
    }
    state.dragSelectedElevationTo(worldYAt(layout, mouseLocal.y));
    mutated = true;
  } else if (frozenLayout.has_value() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    // Gated on frozenLayout (this view's OWN gesture state), not state.dragging(): dragging_ is
    // one flag shared by every drag kind in EditorState, and TopDownCanvas.cpp -- drawn earlier
    // this frame -- has its own generic "state.dragging() && released" handler that unconditionally
    // calls state.endDrag() on mouse-up regardless of which view/point started the drag. That runs
    // first and clears dragging_ before this check ever sees it, so gating the freeze release on
    // dragging() being *still* true left it stuck forever after the first drag. state.endDrag() is
    // idempotent (just clears two flags), so calling it again here even if TopDownCanvas already
    // did is harmless.
    if (state.dragging()) state.endDrag();
    frozenLayout.reset();
  }

  drawAxis(drawList, canvasOrigin, layout);
  if (baked != nullptr && pathIndex < static_cast<int>(baked->paths.size())) drawBakedProfile(drawList, canvasOrigin, layout, baked->paths[pathIndex]);
  for (int order = 0; order < static_cast<int>(points.size()); ++order) {
    const bool isSelected = selectionOnThisPath && selection.pointIndex == points[order].first;
    const bool isHovered = points[order].first == hoveredRawIndex;
    const ImVec2 screen(canvasOrigin.x + screenX(layout, order), canvasOrigin.y + screenY(layout, points[order].second));
    const float radius = isSelected ? kPointRadius + 2.0f : kPointRadius;
    drawList->AddCircleFilled(screen, radius, isSelected ? kSelectedPointColor : kPointColor);
    if (isSelected) drawList->AddCircle(screen, radius, kSelectedOutlineColor, 0, 1.5f);
    if (isHovered) drawList->AddCircle(screen, radius + 3.0f, kHoverRingColor, 0, 2.0f);
  }

  ImGui::EndChild();
  return mutated;
}

}  // namespace editor
