#include "ElevationView.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
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
const ImU32 kProfileColor = IM_COL32(120, 170, 220, 200);
const ImU32 kSelectedPointColor = IM_COL32(255, 90, 90, 255);
// Mirrors TopDownCanvas.cpp's kSelectedOutlineColor: a crisp white "handle" border right at the
// (already-larger) fill's edge -- unmistakable as selected regardless of hover state.
const ImU32 kSelectedOutlineColor = IM_COL32(255, 255, 255, 255);
// Mirrors TopDownCanvas.cpp's kHoverRingColor: a separate, softer ring further out from the fill,
// so it never gets confused with the tighter selection border even when both apply.
const ImU32 kHoverRingColor = IM_COL32(255, 255, 255, 140);
// Disjoint-seam ring: a thicker amber stroke, not a cross (that's a separate drawTop-only styling
// in TopDownCanvas.cpp).
const ImU32 kDisjointColor = IM_COL32(255, 204, 68, 255);
const ImU32 kIndexLabelColor = IM_COL32(127, 184, 216, 255);
// Selected mesh region's elevation line: idle / dragging, 2px idle / 3px dragging.
const ImU32 kMeshElevLineColor = IM_COL32(185, 140, 255, 255);
const ImU32 kMeshElevLineDraggingColor = IM_COL32(240, 228, 255, 255);
constexpr float kMeshElevPickPx = 6.0f;

struct Layout {
  float w{1.0f}, h{1.0f};
  double minY{-1.0}, maxY{1.0};
  float yScale{1.0f};
  bool closed{true};
  int positionCount{0};
};

float screenY(const Layout& layout, double y) { return (layout.h - kPadY) - static_cast<float>((y - layout.minY)) * layout.yScale; }

double worldYAt(const Layout& layout, float screenYPx) { return layout.minY + ((layout.h - kPadY) - screenYPx) / layout.yScale; }

// Cumulative-arc-length profile of the baked centerline, replacing an authored-ORDER x-axis with
// true arc length so the profile lines up exactly with the control-point handles plotted on it.
// `frameCumulative[i]` is the arc length from frame 0 up to baked frame i; for a CLOSED path
// there's one extra trailing entry (index == centerline size) holding the full lap length,
// representing the wrap back to frame 0 -- this both lets interpolation cross the seam cleanly and
// doubles as the arc length of the closed-loop "echo" slot (see xFracForPositionIndex). `valid` is
// false (and every lookup falls back to plain order-based spacing) when there's no baked
// centerline yet to measure -- mirrors this file's existing "baked may be null mid-edit" tolerance.
struct ArcProfile {
  bool valid{false};
  bool closed{false};
  std::vector<double> frameCumulative;
  double totalArc{1.0};
};

ArcProfile buildArcProfile(const tox::Path* bakedPath, bool authoredClosed) {
  ArcProfile profile;
  profile.closed = authoredClosed;
  if (bakedPath == nullptr || bakedPath->centerline.size() < 2) return profile;
  const std::size_t n = bakedPath->centerline.size();
  profile.frameCumulative.resize(profile.closed ? n + 1 : n);
  profile.frameCumulative[0] = 0.0;
  double cum = 0.0;
  for (std::size_t i = 1; i < n; ++i) {
    const tox::Vec3& a = bakedPath->centerline[i - 1].pos;
    const tox::Vec3& b = bakedPath->centerline[i].pos;
    cum += std::hypot(b.x - a.x, b.z - a.z);
    profile.frameCumulative[i] = cum;
  }
  if (profile.closed) {
    const tox::Vec3& a = bakedPath->centerline[n - 1].pos;
    const tox::Vec3& b = bakedPath->centerline[0].pos;
    cum += std::hypot(b.x - a.x, b.z - a.z);
    profile.frameCumulative[n] = cum;
  }
  profile.totalArc = cum > 0.0 ? cum : 1.0;
  profile.valid = true;
  return profile;
}

// Arc length up to path-parameter g in [0, gMax], linearly interpolated between the two
// bracketing baked frames -- follows the exact same g/gMax/closed convention as
// TopDownCanvas.cpp's sampleCenterlineAtG (this file's sampleCenterlinePosAtG below mirrors that
// convention too, so a control point's own `g` value works unmodified in either function).
double arcLengthAtG(const ArcProfile& profile, std::size_t centerlineCount, double g, double gMax) {
  if (!profile.valid || centerlineCount < 2) return 0.0;
  const double frac = gMax > 0.0 ? std::clamp(g, 0.0, gMax) / gMax : 0.0;
  const double indexF = frac * static_cast<double>(profile.closed ? centerlineCount : centerlineCount - 1);
  const auto index0 = static_cast<std::size_t>(std::floor(indexF));
  const double t = indexF - static_cast<double>(index0);
  const std::size_t maxIndex = profile.frameCumulative.size() - 1;
  const std::size_t clampedIndex0 = std::min(index0, maxIndex);
  const std::size_t index1 = std::min(clampedIndex0 + 1, maxIndex);
  return profile.frameCumulative[clampedIndex0] + (profile.frameCumulative[index1] - profile.frameCumulative[clampedIndex0]) * t;
}

// Fraction across the plotted x-axis [0, 1] for a control-point "slot" at position-space index
// `slotIndex` (g = slotIndex): a normal control point for slotIndex in [0, positionCount), or --
// for a CLOSED path only -- the echo slot at slotIndex == positionCount, which resolves to g ==
// gMax and therefore arc length == totalArc, i.e. the far right edge. Falls back to plain
// order-based spacing when no arc profile is available.
double xFracForPositionIndex(const ArcProfile& arc, std::size_t centerlineCount, int slotIndex, bool closed, int positionCount) {
  if (arc.valid) {
    const double gMax = closed ? positionCount : positionCount - 1;
    return arc.totalArc > 0.0 ? arcLengthAtG(arc, centerlineCount, static_cast<double>(slotIndex), gMax) / arc.totalArc : 0.0;
  }
  const int denom = closed ? std::max(1, positionCount) : std::max(1, positionCount - 1);
  return static_cast<double>(slotIndex) / static_cast<double>(denom);
}

float screenX(const Layout& layout, double xFrac) { return kPadX + static_cast<float>(xFrac) * (layout.w - 2.0f * kPadX); }

// Inverse of arcLengthAtG: the path parameter g in [0, gMax] whose arc length equals
// `arcFrac * totalArc`. Used for right-click-to-insert, mapping a clicked screen x back to a curve
// parameter. Falls back to plain `arcFrac * gMax` (matching xFracForPositionIndex's own fallback)
// when no arc profile is available. Linear scan over frameCumulative is fine here: this only runs
// once per right-click, not per frame.
double gAtArcFraction(const ArcProfile& arc, std::size_t centerlineCount, double arcFrac, double gMax) {
  if (!arc.valid || arc.frameCumulative.size() < 2) return std::clamp(arcFrac, 0.0, 1.0) * gMax;
  const double targetArc = std::clamp(arcFrac, 0.0, 1.0) * arc.totalArc;
  const std::size_t last = arc.frameCumulative.size() - 1;
  std::size_t hi = 1;
  while (hi < last && arc.frameCumulative[hi] < targetArc) ++hi;
  const std::size_t lo = hi - 1;
  const double segLen = arc.frameCumulative[hi] - arc.frameCumulative[lo];
  const double t = segLen > 0.0 ? (targetArc - arc.frameCumulative[lo]) / segLen : 0.0;
  const double indexF = static_cast<double>(lo) + t;
  const double frac = indexF / static_cast<double>(arc.closed ? centerlineCount : centerlineCount - 1);
  return std::clamp(frac, 0.0, 1.0) * gMax;
}

// Interpolated centerline position at path parameter `g` in [0, gMax] -- the position-only
// counterpart of TopDownCanvas.cpp's sampleCenterlineAtG (duplicated rather than shared: each
// file's sampler is a private implementation detail of its own approximate-but-good-enough-at-
// editor-zoom conventions, per this codebase's existing pattern of small per-file mini-evaluators).
tox::Vec3 sampleCenterlinePosAtG(const std::vector<tox::Frame>& centerline, bool closed, double g, double gMax) {
  const std::size_t n = centerline.size();
  if (n == 0) return tox::Vec3(0.0, 0.0, 0.0);
  if (n == 1) return centerline[0].pos;
  const double frac = gMax > 0.0 ? std::clamp(g, 0.0, gMax) / gMax : 0.0;
  const double indexF = frac * static_cast<double>(closed ? n : n - 1);
  auto index0 = static_cast<std::size_t>(std::floor(indexF));
  const double t = indexF - static_cast<double>(index0);
  std::size_t index1;
  if (closed) {
    index0 %= n;
    index1 = (index0 + 1) % n;
  } else {
    index0 = std::min(index0, n - 1);
    index1 = std::min(index0 + 1, n - 1);
  }
  const tox::Vec3& a = centerline[index0].pos;
  const tox::Vec3& b = centerline[index1].pos;
  return tox::Vec3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}

// Collects (rawPointIndex, y) for every Position-kind point in the path, in authored order.
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
// padding twice. `extraY`, when present, is folded into the range too -- used to keep the selected
// mesh region's elevation line on-panel even when it sits well above or below this curve.
YRange rawYRange(const std::vector<std::pair<int, double>>& points, std::optional<double> extraY = std::nullopt) {
  double minY = 1e300, maxY = -1e300;
  for (const auto& [index, y] : points) {
    minY = std::min(minY, y);
    maxY = std::max(maxY, y);
  }
  if (extraY.has_value()) {
    minY = std::min(minY, *extraY);
    maxY = std::max(maxY, *extraY);
  }
  if (minY > maxY) return {-1.0, 1.0};
  return {minY, maxY};
}

Layout layoutFromRange(double minY, double maxY, bool closed, int positionCount, float w, float h) {
  Layout layout;
  layout.w = w;
  layout.h = h;
  layout.closed = closed;
  layout.positionCount = positionCount;
  if (maxY - minY < 1.0) {
    // Flat or near-flat profiles still need visible headroom -- pad a
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

Layout computeLayout(const std::vector<std::pair<int, double>>& points, bool closed, float w, float h, std::optional<double> extraY = std::nullopt) {
  const YRange range = rawYRange(points, extraY);
  return layoutFromRange(range.minY, range.maxY, closed, static_cast<int>(points.size()), w, h);
}

// blue (low) -> teal -> warm (high).
ImU32 heightColor(double y) {
  const double t = std::clamp(y / 8.0, -1.0, 1.0);
  const int r = static_cast<int>(std::lround(60.0 + 150.0 * std::max(0.0, t)));
  const int g = static_cast<int>(std::lround(150.0 + 60.0 * (1.0 - std::abs(t))));
  const int b = static_cast<int>(std::lround(180.0 - 120.0 * std::max(0.0, t) + 40.0 * std::max(0.0, -t)));
  return IM_COL32(r, g, b, 255);
}

// Rounds a raw tick spacing down to a "nice" 1/2/5-times-a-power-of-ten step.
double niceAxisStep(double range, double targetTicks) {
  const double raw = range / std::max(1.0, targetTicks);
  if (!(raw > 0.0) || !std::isfinite(raw)) return 1.0;
  const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
  const double normalized = raw / magnitude;
  const double step = normalized <= 1.0 ? 1.0 : normalized <= 2.0 ? 2.0 : normalized <= 5.0 ? 5.0 : 10.0;
  return step * magnitude;
}

// A few short dashes rather than one native ImGui doesn't support -- used only for the zero line,
// which is explicitly "dashed", unlike this file's other solid lines, which already accept a
// solid-for-dashed simplification elsewhere (e.g. drawCreateDraft in TopDownCanvas.cpp).
void addDashedHLine(ImDrawList* drawList, float x0, float x1, float y, ImU32 color, float thickness) {
  constexpr float kDash = 4.0f, kGap = 4.0f;
  for (float x = x0; x < x1; x += kDash + kGap) drawList->AddLine(ImVec2(x, y), ImVec2(std::min(x + kDash, x1), y), color, thickness);
}

// Full-width horizontal gridline per "nice" Y-axis tick, each labelled in the left gutter, plus a
// distinctly-emphasized dashed zero line. The tick landing on zero is skipped -- it would collide
// with the zero line's own label.
void drawYAxis(ImDrawList* drawList, const ImVec2& canvasOrigin, const Layout& layout) {
  const double range = layout.maxY - layout.minY;
  if (range > 0.0 && std::isfinite(range)) {
    const double step = niceAxisStep(range, std::max(2.0, std::round((layout.h - 2.0f * kPadY) / 34.0)));
    const int decimals = std::clamp(static_cast<int>(-std::floor(std::log10(step) + 1e-9)), 0, 3);
    char fmt[8];
    std::snprintf(fmt, sizeof(fmt), "%%.%df", decimals);
    for (double v = std::ceil(layout.minY / step) * step; v <= layout.maxY + step * 1e-6; v += step) {
      const float y = canvasOrigin.y + screenY(layout, v);
      if (y < canvasOrigin.y + kPadY - 0.5f || y > canvasOrigin.y + layout.h - kPadY + 0.5f) continue;
      if (std::abs(v) < step * 1e-6) continue;  // the zero line labels itself, drawn below
      drawList->AddLine(ImVec2(canvasOrigin.x + kPadX, y), ImVec2(canvasOrigin.x + layout.w - kPadX, y), IM_COL32(60, 95, 125, 87));
      char label[32];
      std::snprintf(label, sizeof(label), fmt, v == 0.0 ? 0.0 : v);  // strips a stray "-0"
      const ImVec2 size = ImGui::CalcTextSize(label);
      drawList->AddText(ImVec2(canvasOrigin.x + kPadX - 6.0f - size.x, y - size.y * 0.5f), IM_COL32(92, 127, 149, 255), label);
    }
  }
  const float zeroY = canvasOrigin.y + screenY(layout, 0.0);
  addDashedHLine(drawList, canvasOrigin.x + kPadX, canvasOrigin.x + layout.w - kPadX, zeroY, IM_COL32(70, 110, 140, 128), 1.0f);
  const ImVec2 zeroLabelSize = ImGui::CalcTextSize("0");
  drawList->AddText(ImVec2(canvasOrigin.x + kPadX - 6.0f - zeroLabelSize.x, zeroY - zeroLabelSize.y * 0.5f), IM_COL32(143, 180, 200, 255), "0");
}

// Nearest position-point "slot" (see xFracForPositionIndex) to `mouseLocal`, within kPickRadiusPx,
// returned as the raw Path::points index it resolves to -- a hit on the closed-loop echo slot
// resolves to point 0 (`idx = i % n`). Shared by click-to-select and hover-highlight so they always
// agree on what's "under the cursor". Empty (-1) when `showPositionPoints` is false.
int nearestPointIndex(const std::vector<std::pair<int, double>>& points, const ArcProfile& arc, std::size_t centerlineCount, const Layout& layout,
                     const ImVec2& mouseLocal, bool showPositionPoints) {
  if (!showPositionPoints || points.empty()) return -1;
  const int n = static_cast<int>(points.size());
  const int slots = layout.closed ? n + 1 : n;
  int hitIndex = -1;
  float bestDistSq = kPickRadiusPx * kPickRadiusPx;
  for (int slot = 0; slot < slots; ++slot) {
    const int pointsIndex = slot % n;
    const float px = screenX(layout, xFracForPositionIndex(arc, centerlineCount, slot, layout.closed, n));
    const float py = screenY(layout, points[pointsIndex].second);
    const float dx = mouseLocal.x - px, dy = mouseLocal.y - py;
    const float distSq = dx * dx + dy * dy;
    if (distSq <= bestDistSq) {
      bestDistSq = distSq;
      hitIndex = points[pointsIndex].first;
    }
  }
  return hitIndex;
}

void drawBakedProfile(ImDrawList* drawList, const ImVec2& canvasOrigin, const Layout& layout, const tox::Path& bakedPath, const ArcProfile& arc) {
  const std::size_t n = bakedPath.centerline.size();
  if (n < 2 || !arc.valid) return;
  std::vector<ImVec2> screen;
  screen.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    const float x = screenX(layout, arc.totalArc > 0.0 ? arc.frameCumulative[i] / arc.totalArc : 0.0);
    const float y = screenY(layout, bakedPath.centerline[i].pos.y);
    screen.push_back(ImVec2(canvasOrigin.x + x, canvasOrigin.y + y));
  }
  drawList->AddPolyline(screen.data(), static_cast<int>(screen.size()), kProfileColor, ImDrawFlags_None, 2.0f);
}

// Full-width horizontal line at the selected mesh region's elevation, with an "<asset>  y <elev>"
// label. A mesh placement has no path parameter, so unlike a position point's marker this always
// spans the panel's full width rather than sitting at one x position.
void drawMeshElevationLine(ImDrawList* drawList, const ImVec2& canvasOrigin, const Layout& layout, const MeshPlacement& placement, bool dragging) {
  const float y = canvasOrigin.y + screenY(layout, placement.elevation);
  const ImU32 color = dragging ? kMeshElevLineDraggingColor : kMeshElevLineColor;
  drawList->AddLine(ImVec2(canvasOrigin.x + kPadX, y), ImVec2(canvasOrigin.x + layout.w - kPadX, y), color, dragging ? 3.0f : 2.0f);
  char label[80];
  std::snprintf(label, sizeof(label), "%s  y %.1f", placement.assetId.c_str(), placement.elevation);
  drawList->AddText(ImVec2(canvasOrigin.x + kPadX + 4.0f, y - 14.0f), color, label);
}

}  // namespace

bool DrawElevationView(EditorState& state, const tox::Track* baked, int pathIndex, bool showPositionPoints) {
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
  const tox::Path* bakedPath = (baked != nullptr && pathIndex < static_cast<int>(baked->paths.size())) ? &baked->paths[pathIndex] : nullptr;
  // Arc-length profile: rebuilt fresh every call, NOT frozen like the
  // Y layout below -- a Y-only drag (position elevation or mesh elevation) never changes any path's
  // X/Z, so the arc length along this curve is unaffected by it, even though main.cpp does rebake
  // on every dragging frame.
  const ArcProfile arc = buildArcProfile(bakedPath, path.closed);
  const std::size_t centerlineCount = bakedPath != nullptr ? bakedPath->centerline.size() : 0;
  // Selected mesh region, if any -- its elevation line is drawn/
  // draggable in this panel regardless of which path is currently shown (a mesh has no path
  // parameter of its own).
  const MeshPlacement* meshPlacement = state.selectedMeshId().has_value() ? state.findMeshPlacement(*state.selectedMeshId()) : nullptr;
  std::optional<double> meshElevationForRange;
  if (meshPlacement != nullptr) meshElevationForRange = meshPlacement->elevation;
  const Layout liveLayout = computeLayout(points, path.closed, canvasSize.x, canvasSize.y, meshElevationForRange);
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

  ImGui::InvisibleButton("elevationViewInput", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
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
  const int hoveredRawIndex = hovered ? nearestPointIndex(points, arc, centerlineCount, layout, mouseLocal, showPositionPoints) : -1;

  // Selected mesh region's elevation line: grabbed on vertical proximity alone -- no x-range
  // restriction, since the line spans the panel's full width.
  const bool meshElevLineHovered =
      hovered && meshPlacement != nullptr && std::abs(mouseLocal.y - screenY(layout, meshPlacement->elevation)) <= kMeshElevPickPx;
  // Persists across frames for the life of one drag gesture, mirroring frozenLayout's own
  // static-local lifetime just above. Set on the mousedown that starts a gesture, checking
  // mesh-elevation proximity FIRST, before roll/position hit tests, so a click landing on the line
  // always grabs it rather than falling through to point selection.
  static bool meshElevDragArmed = false;

  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    if (meshElevLineHovered) {
      meshElevDragArmed = true;
    } else {
      meshElevDragArmed = false;
      const int hitIndex = nearestPointIndex(points, arc, centerlineCount, layout, mouseLocal, showPositionPoints);
      if (hitIndex >= 0) state.selectPoint(pathIndex, hitIndex);
    }
  }

  // Right-click to insert a new position control point: X/Z come from the baked
  // centerline at the clicked arc position, Y comes from the click's height. Gated on
  // showPositionPoints and the click landing inside the plot gutter.
  if (showPositionPoints && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && mouseLocal.x >= kPadX &&
      mouseLocal.x <= layout.w - kPadX && bakedPath != nullptr && !bakedPath->centerline.empty()) {
    const int n = EditorState::positionCount(path);
    const double gMax = path.closed ? n : n - 1;
    const double arcFrac = std::clamp(static_cast<double>((mouseLocal.x - kPadX) / (layout.w - 2.0f * kPadX)), 0.0, 1.0);
    const double g = gAtArcFraction(arc, centerlineCount, arcFrac, gMax);
    const tox::Vec3 pos = sampleCenterlinePosAtG(bakedPath->centerline, path.closed, g, gMax);
    const int insertAt = path.closed ? (static_cast<int>(std::floor(g)) + 1) % (n + 1) : std::min(n, static_cast<int>(std::floor(g)) + 1);
    const double y = worldYAt(layout, mouseLocal.y);
    if (state.insertPositionOnSegment(pathIndex, insertAt, pos.x, y, pos.z).has_value()) mutated = true;
  }

  // Gated on itemActive (this canvas's own InvisibleButton captured the mouse-down), not just a
  // global drag gesture -- ImGui::IsMouseDragging() alone is true regardless of which window's
  // widget is actually being dragged, so without this gate, dragging a point in the top-down
  // view's own canvas (a separate InvisibleButton) would ALSO be seen as a drag here on every
  // frame both windows draw, spuriously overwriting the selected point's Y with wherever the
  // mouse happens to be over THIS view. Mirrors TopDownCanvas.cpp's same fix for the reverse
  // direction (elevation drags spuriously moving X/Z in the top-down view).
  const bool meshElevDragging = itemActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) && meshElevDragArmed && meshPlacement != nullptr;
  if (meshElevDragging) {
    if (!state.dragging()) {
      state.beginDrag();
      frozenLayout = liveLayout;
    }
    state.dragSelectedMeshElevationTo(worldYAt(layout, mouseLocal.y));
    mutated = true;
  } else if (itemActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) && selectionOnThisPath) {
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
    meshElevDragArmed = false;
  }

  drawYAxis(drawList, canvasOrigin, layout);
  if (bakedPath != nullptr) drawBakedProfile(drawList, canvasOrigin, layout, *bakedPath, arc);
  if (meshPlacement != nullptr) drawMeshElevationLine(drawList, canvasOrigin, layout, *meshPlacement, meshElevDragging);

  // Height handles: circles colored by heightColor(y), a closed path
  // getting one extra faded "echo" slot of point 0 at the far right.
  // Skipped entirely when showPositionPoints is false.
  if (showPositionPoints && !points.empty()) {
    const int n = static_cast<int>(points.size());
    const int slots = path.closed ? n + 1 : n;
    for (int slot = 0; slot < slots; ++slot) {
      const int pointsIndex = slot % n;
      const int rawIndex = points[pointsIndex].first;
      const bool echo = path.closed && slot == n;
      // NOT gated on !echo: when point 0 is selected/hovered, the echo slot draws with the same
      // selected/hovered styling too, just faded like every other echo attribute.
      const bool isSelected = selectionOnThisPath && selection.pointIndex == rawIndex;
      const bool isHovered = rawIndex == hoveredRawIndex;
      const bool isDisjoint =
          std::find_if(state.disjointSeams().begin(), state.disjointSeams().end(),
                       [&](const Connection& s) { return s.pointId == path.points[rawIndex].id; }) != state.disjointSeams().end();
      const ImVec2 screen(canvasOrigin.x + screenX(layout, xFracForPositionIndex(arc, centerlineCount, slot, path.closed, n)),
                          canvasOrigin.y + screenY(layout, points[pointsIndex].second));
      const float radius = isSelected ? kPointRadius + 2.0f : kPointRadius;
      const ImU32 alphaMask = echo ? 0x73000000u : 0xFF000000u;  // ~45% alpha for the echo
      drawList->AddCircleFilled(screen, radius, ((isSelected ? kSelectedPointColor : heightColor(points[pointsIndex].second)) & 0x00FFFFFFu) | alphaMask);
      // Ring only, no cross: unlike the top-down view's own disjoint-node styling (drawTop's amber
      // X, TopDownCanvas.cpp's kDisjointColor use), this view's disjoint indicator is just a
      // thicker amber stroke -- there is no cross in this view.
      const ImU32 strokeColor = isSelected ? kSelectedOutlineColor : (isDisjoint ? kDisjointColor : IM_COL32(0, 0, 0, 153));
      drawList->AddCircle(screen, radius, (strokeColor & 0x00FFFFFFu) | alphaMask, 0, isSelected || isDisjoint ? 3.0f : 1.5f);
      if (isHovered) drawList->AddCircle(screen, radius + 3.0f, kHoverRingColor, 0, 2.0f);
      char label[8];
      std::snprintf(label, sizeof(label), echo ? "0\xE2\x86\xBA" : "%d", pointsIndex);  // "0\xE2\x86\xBA" = "0↺" (UTF-8)
      drawList->AddText(ImVec2(screen.x - 4.0f, canvasOrigin.y + layout.h - kPadY + 14.0f), (kIndexLabelColor & 0x00FFFFFFu) | alphaMask, label);
    }
  }

  ImGui::EndChild();
  return mutated;
}

}  // namespace editor
