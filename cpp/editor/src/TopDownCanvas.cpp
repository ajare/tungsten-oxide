#include "TopDownCanvas.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "imgui.h"

#include "Clipboard.hpp"

namespace editor {
namespace {

constexpr float kPointRadius = 4.0f;
constexpr float kPickRadiusPx = 10.0f;                    // matches editor.js's nodeAtTop hit radius
const ImU32 kBackgroundColor = IM_COL32(8, 20, 29, 255);  // matches editor.html's #canvasWrap
const ImU32 kGridColor = IM_COL32(255, 255, 255, 18);
const ImU32 kRoadColor = IM_COL32(60, 70, 82, 255);
const ImU32 kCenterlineColor = IM_COL32(120, 170, 220, 200);
const ImU32 kPositionPointColor = IM_COL32(240, 200, 60, 255);
const ImU32 kSelectedPointColor = IM_COL32(255, 90, 90, 255);
// Selected points get a crisp white "handle" border immediately at the fill edge, on top of the
// larger red fill -- unmistakable at a glance and independent of hover, unlike relying on color/
// size alone (easy to miss against the also-bright hover ring below).
const ImU32 kSelectedOutlineColor = IM_COL32(255, 255, 255, 255);
// Hover is drawn as a separate, larger, softer ring further out from the fill -- on top of
// whichever fill (normal/selected) already applies, rather than its own fill color, so "hovered"
// and "hovered + selected" both read clearly without a fourth distinct fill color to track, and
// without being confusable with the tighter selection border above.
const ImU32 kHoverRingColor = IM_COL32(255, 255, 255, 140);
// Matches editor.js's WIDTH_COLOR (#b6ff3c) / CROSS_SECTION_COLOR (#d58cff); roll's own stroke
// colour is computed per-point by rollTint() below, matching editor.js's rollTint(), not a fixed
// constant. All three handles fill white (matches editor.js's roll/width/cross-section handles,
// which are always '#ffffff' regardless of selection -- only stroke width/handle size change).
const ImU32 kWidthColor = IM_COL32(182, 255, 60, 255);
const ImU32 kCrossSectionColor = IM_COL32(213, 140, 255, 255);
const ImU32 kAuxHandleFillColor = IM_COL32(255, 255, 255, 255);
const ImU32 kCreateDraftColor = IM_COL32(120, 230, 140, 255);
const ImU32 kMeshFillColor = IM_COL32(90, 110, 70, 200);
const ImU32 kMeshOutlineColor = IM_COL32(150, 190, 110, 255);
const ImU32 kMeshSelectedOutlineColor = IM_COL32(255, 90, 90, 255);
const ImU32 kRailEdgeColor = IM_COL32(255, 170, 40, 255);
const ImU32 kRailEdgeSelectedColor = IM_COL32(255, 90, 90, 255);

// Matches editor.js's physics-sample dot colors (js/editor.js:1095: '#ff9c3c' idle, '#ff5ea8'
// selected).
const ImU32 kPhysicsPointColor = IM_COL32(255, 156, 60, 255);
const ImU32 kPhysicsSelectedColor = IM_COL32(255, 94, 168, 255);
constexpr float kMeshEdgePickPx = 8.0f;  // matches editor.js's MESH_EDGE_PICK_PX

// Matches editor.js's ZONE_FILL/ZONE_STROKE (js/editor.js:4209-4210) minus the startGrid checker
// pattern, which is cosmetic only -- a flat fill reads fine at editor zoom levels.
const ImU32 kZoneBoostFillColor = IM_COL32(255, 165, 32, 107);         // rgba(255,165,32,0.42)
const ImU32 kZoneBoostStrokeColor = IM_COL32(255, 176, 32, 255);       // #ffb020
const ImU32 kZoneStartGridFillColor = IM_COL32(207, 214, 221, 97);     // rgba(207,214,221,0.38)
const ImU32 kZoneStartGridStrokeColor = IM_COL32(207, 214, 221, 255);  // #cfd6dd
const ImU32 kZoneSelectedStrokeColor = IM_COL32(255, 90, 90, 255);

// Matches editor.js's TRIGGER_COLOR / triggerColor (js/editor.js:4347-4350). Selection is shown
// via line/point weight only (matching drawTriggers), not a separate highlight color.
const ImU32 kTriggerDummyColor = IM_COL32(255, 94, 168, 255);        // #ff5ea8
const ImU32 kTriggerCheckpointColor = IM_COL32(127, 231, 255, 255);  // #7fe7ff
const ImU32 kTriggerFinishColor = IM_COL32(255, 211, 79, 255);       // #ffd34f

// Mirrors editor.js's computeTrackBounds: authored control points plus every mesh region's baked
// bounds, so a placed mesh always fits in the auto-fit view even off to one side of the track.
TrackBounds2D computeViewBounds(const TrackDefinition& track, const tox::Track* baked) {
  TrackBounds2D bounds{1e300, -1e300, 1e300, -1e300};
  for (const auto& path : track.paths) {
    for (const auto& point : path.points) {
      if (point.kind != PointKind::Position) continue;
      bounds.minX = std::min(bounds.minX, point.pos.x);
      bounds.maxX = std::max(bounds.maxX, point.pos.x);
      bounds.minZ = std::min(bounds.minZ, point.pos.z);
      bounds.maxZ = std::max(bounds.maxZ, point.pos.z);
    }
  }
  if (baked != nullptr) {
    for (const auto& region : baked->meshRegions) {
      bounds.minX = std::min(bounds.minX, region.bounds.minX);
      bounds.maxX = std::max(bounds.maxX, region.bounds.maxX);
      bounds.minZ = std::min(bounds.minZ, region.bounds.minZ);
      bounds.maxZ = std::max(bounds.maxZ, region.bounds.maxZ);
    }
  }
  if (bounds.minX > bounds.maxX) return TrackBounds2D{-1.0, 1.0, -1.0, 1.0};
  return bounds;
}

ImVec2 toAbsolute(const ImVec2& canvasOrigin, const ScreenPoint2D& local) {
  return ImVec2(canvasOrigin.x + static_cast<float>(local.x), canvasOrigin.y + static_cast<float>(local.y));
}
ImVec2 toAbsolute(const ImVec2& canvasOrigin, const ImVec2& local) { return ImVec2(canvasOrigin.x + local.x, canvasOrigin.y + local.y); }

// Road-fill color formulas for Flat/Elevation render modes (EDITOR_PARITY_FIXES.md gap 10),
// ported 1:1 from js/editor.js's rollColor/elevationColor (js/editor.js:774-787).
ImU32 rollFillColor(double rollDeg) {
  const double t = std::clamp(std::abs(rollDeg) / 180.0, 0.0, 1.0);
  const int r = static_cast<int>(std::lround(40.0 + (210.0 - 40.0) * t));
  const int g = static_cast<int>(std::lround(190.0 + (50.0 - 190.0) * t));
  return IM_COL32(r, g, 55, 255);
}
ImU32 elevationFillColor(double y, double minY, double maxY) {
  const double span = (maxY - minY) != 0.0 ? (maxY - minY) : 1.0;
  const double t = std::clamp((y - minY) / span, 0.0, 1.0);
  const int r = static_cast<int>(std::lround(40.0 + (220.0 - 40.0) * t));
  const int g = static_cast<int>(std::lround(210.0 + (55.0 - 210.0) * t));
  return IM_COL32(r, g, 55, 255);
}

// Configurable top-down reference grid (EDITOR_PARITY_FIXES.md gap 9), mirroring editor.js's
// drawTop() grid block: gated on view.showGrid(), spaced at view.gridSize() world units, skipped
// once screen spacing drops below drawTop's own `step > 6` threshold rather than smearing into a
// solid fill.
void drawGrid(ImDrawList* drawList, const ImVec2& canvasOrigin, const ImVec2& canvasSize, const TopDownView& view) {
  if (!view.showGrid()) return;
  const double gridSize = view.gridSize();
  const double step = gridSize * view.scale();
  if (step <= 6.0) return;
  const WorldPoint2D topLeft = view.screenToWorld(0.0, 0.0);
  const WorldPoint2D bottomRight = view.screenToWorld(canvasSize.x, canvasSize.y);
  const double startX = std::floor(topLeft.x / gridSize) * gridSize;
  for (double x = startX; x <= bottomRight.x; x += gridSize) {
    const ScreenPoint2D top = view.worldToScreen(x, topLeft.z);
    const ScreenPoint2D bottom = view.worldToScreen(x, bottomRight.z);
    drawList->AddLine(toAbsolute(canvasOrigin, top), toAbsolute(canvasOrigin, bottom), kGridColor);
  }
  const double startZ = std::floor(topLeft.z / gridSize) * gridSize;
  for (double z = startZ; z <= bottomRight.z; z += gridSize) {
    const ScreenPoint2D left = view.worldToScreen(topLeft.x, z);
    const ScreenPoint2D right = view.worldToScreen(bottomRight.x, z);
    drawList->AddLine(toAbsolute(canvasOrigin, left), toAbsolute(canvasOrigin, right), kGridColor);
  }
}

// `mode` mirrors editor.js's renderMode (EDITOR_PARITY_FIXES.md gap 10): Banked (default) offsets
// edges by each frame's baked, banked `edgeRight` and fills with a flat color, matching
// TrackCore.buildEdges + drawTop's non-flat ribbon fill. Flat/Elevation instead offset by the
// UNROLLED `h` axis (mirrors buildFlatEdges -- "the track's plan-view footprint (width only)
// without banking distorting the top-down shape") and fill each segment by interpolated roll or
// elevation (rollFillColor/elevationFillColor) instead of a flat color. `minElev`/`maxElev` are
// ignored outside Elevation mode.
void drawBakedPath(ImDrawList* drawList, const ImVec2& canvasOrigin, const TopDownView& view, const tox::Path& path,
                   TopDownView::RenderMode mode, double minElev, double maxElev) {
  const std::size_t n = path.centerline.size();
  if (n < 2) return;
  const bool flatEdges = mode != TopDownView::RenderMode::Banked;

  const std::size_t segmentCount = path.closed ? n : n - 1;
  for (std::size_t i = 0; i < segmentCount; ++i) {
    const std::size_t j = (i + 1) % n;
    const tox::Frame& fi = path.centerline[i];
    const tox::Frame& fj = path.centerline[j];
    const tox::Vec3& axisI = flatEdges ? fi.h : fi.edgeRight;
    const tox::Vec3& axisJ = flatEdges ? fj.h : fj.edgeRight;
    const tox::Vec3 leftI = fi.pos.clone().addScaledVector(axisI, -fi.halfW);
    const tox::Vec3 rightI = fi.pos.clone().addScaledVector(axisI, fi.halfW);
    const tox::Vec3 leftJ = fj.pos.clone().addScaledVector(axisJ, -fj.halfW);
    const tox::Vec3 rightJ = fj.pos.clone().addScaledVector(axisJ, fj.halfW);
    const ImVec2 quad[4] = {
        toAbsolute(canvasOrigin, view.worldToScreen(leftI.x, leftI.z)),
        toAbsolute(canvasOrigin, view.worldToScreen(leftJ.x, leftJ.z)),
        toAbsolute(canvasOrigin, view.worldToScreen(rightJ.x, rightJ.z)),
        toAbsolute(canvasOrigin, view.worldToScreen(rightI.x, rightI.z)),
    };
    ImU32 fillColor = kRoadColor;
    if (mode == TopDownView::RenderMode::Flat) {
      fillColor = rollFillColor((fi.roll + fj.roll) * 0.5 * 180.0 / std::numbers::pi);
    } else if (mode == TopDownView::RenderMode::Elevation) {
      fillColor = elevationFillColor((fi.pos.y + fj.pos.y) * 0.5, minElev, maxElev);
    }
    drawList->AddConvexPolyFilled(quad, 4, fillColor);
  }

  std::vector<ImVec2> centerline;
  centerline.reserve(n);
  for (const auto& frame : path.centerline) centerline.push_back(toAbsolute(canvasOrigin, view.worldToScreen(frame.pos.x, frame.pos.z)));
  drawList->AddPolyline(centerline.data(), static_cast<int>(centerline.size()), kCenterlineColor,
                        path.closed ? ImDrawFlags_Closed : ImDrawFlags_None, 2.0f);
}

// Mesh polygons are already baked into world space by core's compileTrackMeshes (Vec2d{x, y}
// where y is world Z, matching js/track-mesh.js's localToWorld) -- the editor draws that directly
// rather than re-deriving placement transforms itself, the same reuse-core approach as the road.
void drawMeshRegions(ImDrawList* drawList, const ImVec2& canvasOrigin, const TopDownView& view, const std::vector<tox::MeshRegion>& regions,
                     const std::optional<std::string>& selectedMeshId) {
  for (const auto& region : regions) {
    const bool isSelected = selectedMeshId.has_value() && *selectedMeshId == region.id;
    for (const auto& polygon : region.polygons) {
      if (polygon.outer.size() < 3) continue;
      std::vector<ImVec2> screen;
      screen.reserve(polygon.outer.size());
      for (const auto& v : polygon.outer) screen.push_back(toAbsolute(canvasOrigin, view.worldToScreen(v.x, v.y)));
      drawList->AddConcavePolyFilled(screen.data(), static_cast<int>(screen.size()), kMeshFillColor);
      drawList->AddPolyline(screen.data(), static_cast<int>(screen.size()), isSelected ? kMeshSelectedOutlineColor : kMeshOutlineColor,
                            ImDrawFlags_Closed, isSelected ? 3.0f : 1.5f);
    }
  }
}

// ---- Zones (EDITOR_PARITY_FIXES.md gap 3) --------------------------------------------------
//
// core bakes zones into tox::Track::zones (a mesh-hosted rotated rectangle, or a path-hosted strip
// described by gLo/gHi/gMax/lateral/halfWidth into the host path's own parameter space) but not
// into a ready-made 2D outline the way MeshRegion's polygons already are. For a mesh-hosted zone
// that's just a rotated rectangle; for a path-hosted one this samples the host path's baked
// CENTERLINE (linear interpolation between the nearest two samples) rather than re-evaluating the
// underlying rational spline the way js/editor.js's zoneOutlineWorld does via
// TrackCore.zonePathStrip -- core keeps its own spline Evaluator private to TrackBake.cpp, so nothing
// equivalent is exposed here. Approximate, but the centerline is already sampled densely enough
// (TrackCore.adaptiveSampleCount, ~6m spacing) that the visual difference at editor zoom levels is
// imperceptible; this is a 2D outline for editing, never fed back into physics.

// Maps a path parameter g in [0, gMax] to an interpolated centerline frame, given core's own
// sampling convention: closed paths sample N points spanning [0, gMax) (wrapping,
// track-core.js:471-472), open paths sample N points spanning [0, gMax] inclusive of both ends.
// `hX/hZ` is the UNROLLED horizontal axis (roll/width/cross-section handles are drawn along this,
// not the banked `edgeRight`, matching js/editor.js's own frame.h usage in its roll/width/
// cross-section rendering); `width`/`roll` (radians) are the frame's own baked values.
struct WorldFrame2D {
  double x{0.0}, z{0.0}, rightX{1.0}, rightZ{0.0}, hX{1.0}, hZ{0.0}, width{1.0}, roll{0.0};
};

WorldFrame2D sampleCenterlineAtG(const std::vector<tox::Frame>& centerline, bool closed, double g, double gMax) {
  const std::size_t n = centerline.size();
  if (n == 0) return {};
  if (n == 1) {
    const tox::Frame& only = centerline[0];
    return {only.pos.x, only.pos.z, only.edgeRight.x, only.edgeRight.z, only.h.x, only.h.z, only.width, only.roll};
  }
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
  const tox::Frame& a = centerline[index0];
  const tox::Frame& b = centerline[index1];
  return {a.pos.x + (b.pos.x - a.pos.x) * t,
          a.pos.z + (b.pos.z - a.pos.z) * t,
          a.edgeRight.x + (b.edgeRight.x - a.edgeRight.x) * t,
          a.edgeRight.z + (b.edgeRight.z - a.edgeRight.z) * t,
          a.h.x + (b.h.x - a.h.x) * t,
          a.h.z + (b.h.z - a.h.z) * t,
          a.width + (b.width - a.width) * t,
          a.roll + (b.roll - a.roll) * t};
}

// Rotated rectangle for a mesh-hosted zone; zone.rotation is already radians (baked that way in
// TrackBake.cpp), unlike the degrees the rest of this file works in for placements/edges.
std::vector<WorldPoint2D> meshZoneOutline(const tox::Zone& zone) {
  const double cosine = std::cos(zone.rotation), sine = std::sin(zone.rotation);
  auto corner = [&](double x, double z) { return WorldPoint2D{zone.x + x * cosine - z * sine, zone.z + x * sine + z * cosine}; };
  return {corner(-zone.halfLength, -zone.halfWidth), corner(zone.halfLength, -zone.halfWidth), corner(zone.halfLength, zone.halfWidth),
          corner(-zone.halfLength, zone.halfWidth)};
}

// Left rail (gLo..gHi at lateral-halfWidth) followed by the reversed right rail, mirroring
// zoneOutlineWorld's `strip.left` then reversed `strip.right` assembly exactly.
std::vector<WorldPoint2D> pathZoneOutline(const tox::Track& baked, const tox::Zone& zone) {
  if (zone.hostPathIndex < 0 || zone.hostPathIndex >= static_cast<int>(baked.paths.size())) return {};
  const auto& centerline = baked.paths[zone.hostPathIndex].centerline;
  if (centerline.empty()) return {};
  constexpr int kRows = 8;
  std::vector<WorldPoint2D> left(kRows + 1), right(kRows + 1);
  for (int i = 0; i <= kRows; ++i) {
    const double g = zone.gLo + (zone.gHi - zone.gLo) * (static_cast<double>(i) / kRows);
    const WorldFrame2D sample = sampleCenterlineAtG(centerline, zone.closed, g, zone.gMax);
    left[i] = {sample.x + sample.rightX * (zone.lateral - zone.halfWidth), sample.z + sample.rightZ * (zone.lateral - zone.halfWidth)};
    right[i] = {sample.x + sample.rightX * (zone.lateral + zone.halfWidth), sample.z + sample.rightZ * (zone.lateral + zone.halfWidth)};
  }
  std::vector<WorldPoint2D> outline = std::move(left);
  outline.reserve(outline.size() + right.size());
  for (int i = kRows; i >= 0; --i) outline.push_back(right[i]);
  return outline;
}

std::vector<WorldPoint2D> zoneOutlineWorld(const tox::Track& baked, const tox::Zone& zone) {
  return zone.kind == "mesh" ? meshZoneOutline(zone) : pathZoneOutline(baked, zone);
}

bool pointInWorldPolygon(const std::vector<WorldPoint2D>& points, double x, double z) {
  bool inside = false;
  for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
    const WorldPoint2D& a = points[i];
    const WorldPoint2D& b = points[j];
    if ((a.z > z) != (b.z > z) && x < (b.x - a.x) * (z - a.z) / (b.z - a.z) + a.x) inside = !inside;
  }
  return inside;
}

void drawZones(ImDrawList* drawList, const ImVec2& canvasOrigin, const TopDownView& view, const tox::Track& baked,
               const std::optional<std::string>& selectedZoneId) {
  for (const auto& zone : baked.zones) {
    const std::vector<WorldPoint2D> outline = zoneOutlineWorld(baked, zone);
    if (outline.size() < 3) continue;
    std::vector<ImVec2> screen;
    screen.reserve(outline.size());
    for (const auto& p : outline) screen.push_back(toAbsolute(canvasOrigin, view.worldToScreen(p.x, p.z)));
    const bool isStartGrid = zone.effect == "startGrid";
    drawList->AddConcavePolyFilled(screen.data(), static_cast<int>(screen.size()), isStartGrid ? kZoneStartGridFillColor : kZoneBoostFillColor);
    const bool isSelected = selectedZoneId.has_value() && *selectedZoneId == zone.id;
    drawList->AddPolyline(screen.data(), static_cast<int>(screen.size()),
                          isSelected ? kZoneSelectedStrokeColor : (isStartGrid ? kZoneStartGridStrokeColor : kZoneBoostStrokeColor),
                          ImDrawFlags_Closed, isSelected ? 3.0f : 1.5f);
  }
}

// Topmost zone under a world point, mirroring zoneAtTop's reverse iteration (later-added zones
// draw on top).
const tox::Zone* zoneAtWorld(const tox::Track* baked, double worldX, double worldZ) {
  if (baked == nullptr) return nullptr;
  for (auto it = baked->zones.rbegin(); it != baked->zones.rend(); ++it) {
    const std::vector<WorldPoint2D> outline = zoneOutlineWorld(*baked, *it);
    if (outline.size() >= 3 && pointInWorldPolygon(outline, worldX, worldZ)) return &*it;
  }
  return nullptr;
}

// ---- Add-point context menu (EDITOR_PARITY_FIXES.md gap 13) ----------------------------------

struct NearestPathPlacement {
  int pathIndex{-1};
  double t{0.0}, lateral{0.0};
  const tox::Frame* frame{nullptr};
};

// Mirrors nearestPathPlacement (js/editor.js:4238-4257): nearest centerline sample across every
// path, plus the lateral offset from it -- used to place a zone/trigger/aux point at a right-click
// world position. Approximated off the baked centerline's own discrete samples rather than JS's
// fine-grained live spline evaluator (same tradeoff already accepted for zone/trigger outlines,
// EDITOR_PARITY_FIXES.md gaps 3/4 -- no evaluator is exposed to cpp/editor).
std::optional<NearestPathPlacement> nearestPathPlacement(const tox::Track* baked, double worldX, double worldZ) {
  if (baked == nullptr) return std::nullopt;
  int bestPath = -1, bestIndex = -1;
  double bestDistSq = std::numeric_limits<double>::infinity();
  for (int pi = 0; pi < static_cast<int>(baked->paths.size()); ++pi) {
    const auto& centerline = baked->paths[pi].centerline;
    for (int i = 0; i < static_cast<int>(centerline.size()); ++i) {
      const double dx = centerline[i].pos.x - worldX, dz = centerline[i].pos.z - worldZ;
      const double distSq = dx * dx + dz * dz;
      if (distSq < bestDistSq) {
        bestDistSq = distSq;
        bestPath = pi;
        bestIndex = i;
      }
    }
  }
  if (bestPath < 0) return std::nullopt;
  const tox::Path& path = baked->paths[bestPath];
  const tox::Frame& frame = path.centerline[bestIndex];
  const int n = static_cast<int>(path.centerline.size());
  const double t = path.closed ? static_cast<double>(bestIndex) / n : static_cast<double>(bestIndex) / std::max(1, n - 1);
  const double lateral = (worldX - frame.pos.x) * frame.edgeRight.x + (worldZ - frame.pos.z) * frame.edgeRight.z;
  return NearestPathPlacement{bestPath, t, lateral, &frame};
}

// ---- Physics-sample overlay (EDITOR_PARITY_FIXES.md gap 10) ----------------------------------
//
// A read-only debug overlay showing the baked centerline frames physics actually reads: one dot
// per tox::Path::centerline entry, selectable for inspection only (no drag). Mirrors editor.js's
// showPhysicsPoints/physicsSel/drawTop's dot loop. One accepted divergence: JS specifically
// re-samples at TrackCore.N_DEFAULT (its own fixed preview constant), not the adaptive count the
// real game runtime uses; this instead shows the track's *actual* baked centerline (core's own
// adaptive-by-length sampling, see CLAUDE.md's "Game conventions") -- the true physics samples the
// current native bake produced, which is arguably more useful for a physics-debug overlay than a
// separately-forced fixed count, and needs no extra bake path.

// Nearest baked centerline frame (across all paths) to a world point, within `pickRadiusWorld` --
// mirrors physicsPointAtTop's small-threshold nearest-dot search.
std::optional<TopDownView::PhysicsSampleRef> physicsPointAtWorld(const tox::Track* baked, double worldX, double worldZ,
                                                                  double pickRadiusWorld) {
  if (baked == nullptr) return std::nullopt;
  std::optional<TopDownView::PhysicsSampleRef> best;
  double bestDistSq = pickRadiusWorld * pickRadiusWorld;
  for (int pi = 0; pi < static_cast<int>(baked->paths.size()); ++pi) {
    const auto& centerline = baked->paths[pi].centerline;
    for (int i = 0; i < static_cast<int>(centerline.size()); ++i) {
      const double dx = centerline[i].pos.x - worldX, dz = centerline[i].pos.z - worldZ;
      const double distSq = dx * dx + dz * dz;
      if (distSq < bestDistSq) {
        bestDistSq = distSq;
        best = TopDownView::PhysicsSampleRef{pi, i};
      }
    }
  }
  return best;
}

void drawPhysicsPoints(ImDrawList* drawList, const ImVec2& canvasOrigin, const TopDownView& view, const tox::Track& baked) {
  const auto& sel = view.physicsSelection();
  for (int pi = 0; pi < static_cast<int>(baked.paths.size()); ++pi) {
    const auto& centerline = baked.paths[pi].centerline;
    for (int i = 0; i < static_cast<int>(centerline.size()); ++i) {
      const bool isSelected = sel.has_value() && sel->pathIndex == pi && sel->frameIndex == i;
      const ImVec2 screen = toAbsolute(canvasOrigin, view.worldToScreen(centerline[i].pos.x, centerline[i].pos.z));
      drawList->AddCircleFilled(screen, isSelected ? 5.0f : 2.2f, isSelected ? kPhysicsSelectedColor : kPhysicsPointColor);
      if (isSelected) drawList->AddCircle(screen, 5.0f, IM_COL32(255, 255, 255, 255), 0, 2.0f);
    }
  }
}

// ---- Triggers (EDITOR_PARITY_FIXES.md gap 4) -------------------------------------------------
//
// Unlike zones, core bakes a trigger directly into a complete world-space gate frame
// (tox::Trigger::center/right/up/fwd, halfWidth, height) -- js/editor.js's own triggerFrameXZ
// re-derives the same thing client-side from the authored host, but core already did the
// equivalent work at bake time, so this reuses it verbatim rather than re-deriving anything.

ImU32 triggerColor(const tox::Trigger& trigger) {
  if (trigger.type != "checkpoint") return kTriggerDummyColor;
  return trigger.role == "finish" ? kTriggerFinishColor : kTriggerCheckpointColor;
}

void drawTriggers(ImDrawList* drawList, const ImVec2& canvasOrigin, const TopDownView& view, const tox::Track& baked,
                  const std::optional<std::string>& selectedTriggerId) {
  for (const auto& trigger : baked.triggers) {
    const ImVec2 a = toAbsolute(canvasOrigin, view.worldToScreen(trigger.center.x - trigger.right.x * trigger.halfWidth,
                                                                 trigger.center.z - trigger.right.z * trigger.halfWidth));
    const ImVec2 b = toAbsolute(canvasOrigin, view.worldToScreen(trigger.center.x + trigger.right.x * trigger.halfWidth,
                                                                 trigger.center.z + trigger.right.z * trigger.halfWidth));
    const bool isSelected = selectedTriggerId.has_value() && *selectedTriggerId == trigger.id;
    const ImU32 color = triggerColor(trigger);
    drawList->AddLine(a, b, color, isSelected ? 4.0f : 2.5f);

    const ImVec2 center = toAbsolute(canvasOrigin, view.worldToScreen(trigger.center.x, trigger.center.z));
    constexpr float kArrowLen = 14.0f;
    auto drawArrow = [&](double sign) {
      const double dx = trigger.fwd.x * sign, dz = trigger.fwd.z * sign;
      const double len = std::hypot(dx, dz);
      if (len <= 0.0) return;
      const ImVec2 tip(center.x + static_cast<float>(dx / len * kArrowLen), center.y + static_cast<float>(dz / len * kArrowLen));
      drawList->AddLine(center, tip, color, isSelected ? 4.0f : 2.5f);
      const double angle = std::atan2(tip.y - center.y, tip.x - center.x);
      const ImVec2 wing1(tip.x - static_cast<float>(std::cos(angle - 0.4) * 6.0), tip.y - static_cast<float>(std::sin(angle - 0.4) * 6.0));
      const ImVec2 wing2(tip.x - static_cast<float>(std::cos(angle + 0.4) * 6.0), tip.y - static_cast<float>(std::sin(angle + 0.4) * 6.0));
      drawList->AddTriangleFilled(tip, wing1, wing2, color);
    };
    if (trigger.direction == "both" || trigger.direction == "forward") drawArrow(1.0);
    if (trigger.direction == "both" || trigger.direction == "backward") drawArrow(-1.0);

    drawList->AddCircleFilled(a, isSelected ? 4.0f : 3.0f, color);
    drawList->AddCircleFilled(b, isSelected ? 4.0f : 3.0f, color);
  }
}

// Local-to-world for one mesh vertex, mirroring js/track-mesh.js's localToWorld and
// TrackMesh.cpp's transform() exactly (rotation in degrees, CCW, local y -> world z). Needed only
// for rail-edge picking/highlighting: core's baked MeshRegion carries just the rail SUBSET already
// flagged (what physics needs), not every edge, but Rails mode must be able to pick ANY edge to
// flag it in the first place -- so this one case works from the authored asset instead of reusing
// a core bake, unlike every other rendering path in this file.
WorldPoint2D meshVertexWorld(const MeshPlacement& placement, const MeshVertex& vertex) {
  const double angle = placement.rotation * std::numbers::pi / 180.0;
  const double cosine = std::cos(angle), sine = std::sin(angle);
  return {vertex.x * cosine - vertex.y * sine + placement.x, vertex.x * sine + vertex.y * cosine + placement.z};
}

const MeshVertex* findVertex(const MeshAsset& asset, int id) {
  for (const auto& v : asset.vertices)
    if (v.id == id) return &v;
  return nullptr;
}

double distanceSqToSegment(double px, double pz, double ax, double az, double bx, double bz) {
  const double dx = bx - ax, dz = bz - az;
  const double lengthSq = dx * dx + dz * dz;
  const double t = lengthSq > 0.0 ? std::clamp(((px - ax) * dx + (pz - az) * dz) / lengthSq, 0.0, 1.0) : 0.0;
  const double cx = ax + t * dx, cz = az + t * dz;
  return (px - cx) * (px - cx) + (pz - cz) * (pz - cz);
}

// Topmost trigger under a world point within `tolWorld`, mirroring triggerAtTop's reverse
// iteration (later-added triggers draw on top) and its distToScreenSegment-based 8px pick radius
// (converted to world units by the caller, same convention as meshEdgeAtWorld's tolWorld).
const tox::Trigger* triggerAtWorld(const tox::Track* baked, double worldX, double worldZ, double tolWorld) {
  if (baked == nullptr) return nullptr;
  for (auto it = baked->triggers.rbegin(); it != baked->triggers.rend(); ++it) {
    const double ax = it->center.x - it->right.x * it->halfWidth, az = it->center.z - it->right.z * it->halfWidth;
    const double bx = it->center.x + it->right.x * it->halfWidth, bz = it->center.z + it->right.z * it->halfWidth;
    if (std::sqrt(distanceSqToSegment(worldX, worldZ, ax, az, bx, bz)) <= tolWorld) return &*it;
  }
  return nullptr;
}

struct MeshEdgeHit {
  std::string meshId, assetId;
  int edgeId;
};

// Nearest mesh edge (any edge, flagged or not) to a world point, within a screen-space-derived
// world tolerance -- mirrors editor.js's meshEdgeAtWorld, iterating every placement's asset edges.
std::optional<MeshEdgeHit> meshEdgeAtWorld(const TrackDefinition& track, double worldX, double worldZ, double tolWorld) {
  std::optional<MeshEdgeHit> best;
  double bestDistSq = tolWorld * tolWorld;
  for (const auto& placement : track.meshes) {
    const auto assetIt = track.meshAssets.find(placement.assetId);
    if (assetIt == track.meshAssets.end()) continue;
    for (const auto& edge : assetIt->second.edges) {
      const MeshVertex* v0 = findVertex(assetIt->second, edge.vertex0);
      const MeshVertex* v1 = findVertex(assetIt->second, edge.vertex1);
      if (v0 == nullptr || v1 == nullptr) continue;
      const WorldPoint2D a = meshVertexWorld(placement, *v0);
      const WorldPoint2D b = meshVertexWorld(placement, *v1);
      const double distSq = distanceSqToSegment(worldX, worldZ, a.x, a.z, b.x, b.z);
      if (distSq <= bestDistSq) {
        bestDistSq = distSq;
        best = MeshEdgeHit{placement.id, placement.assetId, edge.id};
      }
    }
  }
  return best;
}

// Every rail-flagged edge across every placement, highlighted on top of the mesh fill so Rails
// mode shows what's already flagged (not just what's being hovered/toggled this click).
void drawMeshRails(ImDrawList* drawList, const ImVec2& canvasOrigin, const TopDownView& view, const TrackDefinition& track,
                   const std::optional<SelectedRail>& selectedRail) {
  for (const auto& placement : track.meshes) {
    const auto assetIt = track.meshAssets.find(placement.assetId);
    if (assetIt == track.meshAssets.end()) continue;
    for (const auto& edge : assetIt->second.edges) {
      if (!edge.rail) continue;
      const MeshVertex* v0 = findVertex(assetIt->second, edge.vertex0);
      const MeshVertex* v1 = findVertex(assetIt->second, edge.vertex1);
      if (v0 == nullptr || v1 == nullptr) continue;
      const bool isSelected = selectedRail.has_value() && selectedRail->meshId == placement.id && selectedRail->edgeId == edge.id;
      const WorldPoint2D a = meshVertexWorld(placement, *v0);
      const WorldPoint2D b = meshVertexWorld(placement, *v1);
      drawList->AddLine(toAbsolute(canvasOrigin, view.worldToScreen(a.x, a.z)), toAbsolute(canvasOrigin, view.worldToScreen(b.x, b.z)),
                        isSelected ? kRailEdgeSelectedColor : kRailEdgeColor, isSelected ? 4.0f : 3.0f);
    }
  }
}

// ---- Roll/width/cross-section on-canvas handles (EDITOR_PARITY_FIXES.md gap 1) ---------------
//
// Mirrors js/editor.js's drawTop() roll/width/crossSection blocks (js/editor.js:1238-1310) and
// their rollTint()/rollLineEnd() helpers, restricted -- as JS itself is -- to only the
// CURRENTLY-SELECTED path's points (`curPrev = pathPreviews[sel.path]`), to avoid cluttering
// every other path's curve with handles. Positions come from sampleCenterlineAtG() (smooth
// interpolation over the baked centerline) rather than JS's frameAtT() (nearest-frame rounding);
// same "close enough, no evaluator exposed" tradeoff already used for zone outlines above, and
// the visual difference is imperceptible at editor zoom levels. This finishes what gap 1's own
// note deferred ("no on-canvas handle to click") now that sampleCenterlineAtG already exists for
// zones -- still no on-canvas DRAG, though (values are edited via PropertiesPanel.cpp only).

// Mirrors rollTint(): right-lean (negative) -> cyan, left-lean (positive) -> magenta-ish.
ImU32 rollTint(double rollDeg) {
  const double t = std::clamp(rollDeg / 25.0, -1.0, 1.0);
  const int r = static_cast<int>(std::lround(120.0 + 120.0 * std::max(0.0, t)));
  const int b = static_cast<int>(std::lround(120.0 + 120.0 * std::max(0.0, -t)));
  return IM_COL32(r, 150, b, 255);
}

// The screen-space endpoint of a roll point's line, mirrors rollLineEnd(): right (+h) if roll >
// 0, left if roll < 0, length scaled by how much of the full +-180 range is used.
ImVec2 rollHandleScreen(const TopDownView& view, const WorldFrame2D& f, double rollDeg) {
  const double sign = rollDeg > 0.0 ? 1.0 : (rollDeg < 0.0 ? -1.0 : 0.0);
  const double len = f.width * (std::min(180.0, std::abs(rollDeg)) / 180.0);
  const auto p = view.worldToScreen(f.x + f.hX * sign * len, f.z + f.hZ * sign * len);
  return {static_cast<float>(p.x), static_cast<float>(p.y)};
}
// Width's two line-end handles, symmetric about the centerline along +-h.
std::pair<ImVec2, ImVec2> widthHandlesScreen(const TopDownView& view, const WorldFrame2D& f, double width) {
  const double halfW = std::max(1.0, width) / 2.0;
  const auto right = view.worldToScreen(f.x + f.hX * halfW, f.z + f.hZ * halfW);
  const auto left = view.worldToScreen(f.x - f.hX * halfW, f.z - f.hZ * halfW);
  return {ImVec2(static_cast<float>(left.x), static_cast<float>(left.y)), ImVec2(static_cast<float>(right.x), static_cast<float>(right.y))};
}
// Cross-section's single line-end handle, offset from the centerline along +-h by curvature.
ImVec2 crossSectionHandleScreen(const TopDownView& view, const WorldFrame2D& f, double curvature) {
  const double len = (f.width / 2.0) * std::clamp(curvature, -1.0, 1.0);
  const auto p = view.worldToScreen(f.x + f.hX * len, f.z + f.hZ * len);
  return {static_cast<float>(p.x), static_cast<float>(p.y)};
}

void drawAuxPoints(ImDrawList* drawList, const ImVec2& canvasOrigin, const TopDownView& view, const TrackDefinition& track,
                   const tox::Track* baked, int currentPathIndex, const SelectedPoint& selection) {
  if (currentPathIndex < 0 || currentPathIndex >= static_cast<int>(track.paths.size())) return;
  if (baked == nullptr || currentPathIndex >= static_cast<int>(baked->paths.size())) return;
  const Path& path = track.paths[currentPathIndex];
  const auto& centerline = baked->paths[currentPathIndex].centerline;
  if (centerline.empty()) return;

  for (int i = 0; i < static_cast<int>(path.points.size()); ++i) {
    const TrackPoint& point = path.points[i];
    if (point.kind == PointKind::Position) continue;
    if (point.kind == PointKind::Roll && !view.showRollPoints()) continue;
    if (point.kind == PointKind::Width && !view.showWidthPoints()) continue;
    if (point.kind == PointKind::CrossSection && !view.showCrossSectionPoints()) continue;

    const WorldFrame2D f = sampleCenterlineAtG(centerline, path.closed, point.t, 1.0);
    const bool isSelected = selection.pathIndex == currentPathIndex && selection.pointIndex == i;
    const ImVec2 center = toAbsolute(canvasOrigin, view.worldToScreen(f.x, f.z));
    const float handleRadius = isSelected ? 7.0f : 5.0f;
    const float strokeWidth = isSelected ? 3.0f : 1.5f;

    if (point.kind == PointKind::Roll) {
      const ImVec2 end = toAbsolute(canvasOrigin, rollHandleScreen(view, f, point.roll));
      const ImU32 tint = rollTint(point.roll);
      drawList->AddLine(center, end, tint, isSelected ? 3.0f : 2.0f);
      drawList->AddCircleFilled(end, handleRadius, kAuxHandleFillColor);
      drawList->AddCircle(end, handleRadius, tint, 0, strokeWidth);
    } else if (point.kind == PointKind::Width) {
      const auto [leftLocal, rightLocal] = widthHandlesScreen(view, f, point.width);
      const ImVec2 leftS = toAbsolute(canvasOrigin, leftLocal), rightS = toAbsolute(canvasOrigin, rightLocal);
      drawList->AddLine(leftS, rightS, kWidthColor, isSelected ? 3.0f : 2.0f);
      for (const ImVec2& hs : {leftS, rightS}) {
        drawList->AddCircleFilled(hs, handleRadius, kAuxHandleFillColor);
        drawList->AddCircle(hs, handleRadius, kWidthColor, 0, strokeWidth);
      }
    } else {  // CrossSection
      const ImVec2 end = toAbsolute(canvasOrigin, crossSectionHandleScreen(view, f, point.curvature));
      drawList->AddLine(center, end, kCrossSectionColor, isSelected ? 3.0f : 2.0f);
      drawList->AddRectFilled(ImVec2(end.x - handleRadius, end.y - handleRadius), ImVec2(end.x + handleRadius, end.y + handleRadius),
                              kAuxHandleFillColor);
      drawList->AddRect(ImVec2(end.x - handleRadius, end.y - handleRadius), ImVec2(end.x + handleRadius, end.y + handleRadius),
                        kCrossSectionColor, 0.0f, 0, strokeWidth);
    }
  }
}

// Nearest roll/width/cross-section handle to a LOCAL (canvas-relative) screen point, within
// `pickRadiusPx`, on the current path -- mirrors editor.js's crossSectionHandleAtTop/
// widthHandleAtTop/rollHandleAtTop and their mousedown priority (checked in that exact order,
// all three before a position-point hit -- js/editor.js:3270-3277).
std::optional<SelectedPoint> auxHandleAtLocal(const TrackDefinition& track, const tox::Track* baked, int currentPathIndex,
                                              const TopDownView& view, const ImVec2& mouseLocal, float pickRadiusPx) {
  if (currentPathIndex < 0 || currentPathIndex >= static_cast<int>(track.paths.size())) return std::nullopt;
  if (baked == nullptr || currentPathIndex >= static_cast<int>(baked->paths.size())) return std::nullopt;
  const Path& path = track.paths[currentPathIndex];
  const auto& centerline = baked->paths[currentPathIndex].centerline;
  if (centerline.empty()) return std::nullopt;

  auto within = [&](const ImVec2& p) {
    const float dx = mouseLocal.x - p.x, dy = mouseLocal.y - p.y;
    return dx * dx + dy * dy <= pickRadiusPx * pickRadiusPx;
  };

  for (PointKind kind : {PointKind::CrossSection, PointKind::Width, PointKind::Roll}) {
    if (kind == PointKind::Roll && !view.showRollPoints()) continue;
    if (kind == PointKind::Width && !view.showWidthPoints()) continue;
    if (kind == PointKind::CrossSection && !view.showCrossSectionPoints()) continue;
    for (int i = 0; i < static_cast<int>(path.points.size()); ++i) {
      const TrackPoint& point = path.points[i];
      if (point.kind != kind) continue;
      const WorldFrame2D f = sampleCenterlineAtG(centerline, path.closed, point.t, 1.0);
      if (kind == PointKind::Roll) {
        if (within(rollHandleScreen(view, f, point.roll))) return SelectedPoint{currentPathIndex, i};
      } else if (kind == PointKind::Width) {
        const auto [left, right] = widthHandlesScreen(view, f, point.width);
        if (within(left) || within(right)) return SelectedPoint{currentPathIndex, i};
      } else {
        if (within(crossSectionHandleScreen(view, f, point.curvature))) return SelectedPoint{currentPathIndex, i};
      }
    }
  }
  return std::nullopt;
}

void drawAuthoredPositionPoints(ImDrawList* drawList, const ImVec2& canvasOrigin, const TopDownView& view, const TrackDefinition& track,
                                const SelectedPoint& selection, const std::optional<SelectedPoint>& hovered) {
  for (int pi = 0; pi < static_cast<int>(track.paths.size()); ++pi) {
    const auto& points = track.paths[pi].points;
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
      if (points[i].kind != PointKind::Position) continue;
      const bool isSelected = selection.pathIndex == pi && selection.pointIndex == i;
      const bool isHovered = hovered.has_value() && hovered->pathIndex == pi && hovered->pointIndex == i;
      const ImVec2 screen = toAbsolute(canvasOrigin, view.worldToScreen(points[i].pos.x, points[i].pos.z));
      const float radius = isSelected ? kPointRadius + 2.0f : kPointRadius;
      drawList->AddCircleFilled(screen, radius, isSelected ? kSelectedPointColor : kPositionPointColor);
      // Selected: a crisp white border right at the (already-larger) fill's edge -- a "handle"
      // look that reads as selected regardless of hover state or background.
      if (isSelected) drawList->AddCircle(screen, radius, kSelectedOutlineColor, 0, 1.5f);
      // Hovered: a separate, softer ring further out, so it never gets confused with the tighter
      // selection border above even when both apply to the same point.
      if (isHovered) drawList->AddCircle(screen, radius + 3.0f, kHoverRingColor, 0, 2.0f);
    }
  }
}

void drawCreateDraft(ImDrawList* drawList, const ImVec2& canvasOrigin, const TopDownView& view, const std::vector<tox::Vec3>& draft) {
  if (draft.empty()) return;
  std::vector<ImVec2> screen;
  screen.reserve(draft.size());
  for (const auto& p : draft) screen.push_back(toAbsolute(canvasOrigin, view.worldToScreen(p.x, p.z)));
  if (screen.size() > 1) drawList->AddPolyline(screen.data(), static_cast<int>(screen.size()), kCreateDraftColor, ImDrawFlags_None, 2.0f);
  for (const auto& s : screen) drawList->AddCircleFilled(s, kPointRadius, kCreateDraftColor);
}

// Mesh regions are hit-tested against the baked (world-space) region polygons, matched back to
// the authored placement by id -- picked only after every position point, so a large region can
// never steal a click from a control point drawn on top of it (CLAUDE.md's editor conventions).
const tox::MeshRegion* meshRegionAt(const tox::Track* baked, double worldX, double worldZ) {
  if (baked == nullptr) return nullptr;
  for (const auto& region : baked->meshRegions)
    if (region.contains(worldX, worldZ)) return &region;
  return nullptr;
}

double angleFromOriginDeg(double originX, double originZ, double worldX, double worldZ) {
  return std::atan2(worldZ - originZ, worldX - originX) * 180.0 / std::numbers::pi;
}

// Left click/drag: hit-test + select + move a position point or mesh placement (Edit mode only --
// mirrors editor.js's dragging === 'top'/'meshTop'/'meshRotate'). Shift+drag on a mesh rotates it
// about its own placement origin instead of moving it. Returns true if the track was mutated this
// frame. Freezes the view's auto-fit bounds for the duration of any drag (see
// TopDownView::freezeBounds) so moving/rotating something doesn't fight the camera auto-fitting
// around it.
bool handleEditModeInput(EditorState& state, TopDownView& view, const tox::Track* baked, const TrackBounds2D& preDragBounds,
                         const ImVec2& mouseLocal, double pickRadiusWorld, bool hovered, bool itemActive) {
  bool mutated = false;
  // Decided once per gesture, at the mousedown that starts it -- mirrors editor.js's mousedown
  // handler picking a `dragging` mode ('top'/'meshTop'/'panTop'/...) once and sticking with it,
  // rather than re-deriving "what to drag" from whatever happens to be selected on every
  // subsequent frame (which would let a stale selection from an earlier, unrelated click hijack a
  // later empty-space pan drag). Left-drag-on-empty-space panning itself mirrors editor.js's
  // mousedown fallthrough to `dragging = 'panTop'` when nothing else was hit.
  static bool panDragActive = false;
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    // Roll/width/cross-section handles (EDITOR_PARITY_FIXES.md gap 1) win over a position-point
    // hit, mirroring editor.js's mousedown priority (crossSection, then width, then roll, all
    // before nodeAtTop's position check -- js/editor.js:3270-3277).
    const auto auxHit = auxHandleAtLocal(state.track(), baked, state.currentPathIndex(), view, mouseLocal, kPickRadiusPx);
    const WorldPoint2D world = view.screenToWorld(mouseLocal.x, mouseLocal.y);
    if (auxHit.has_value()) {
      state.selectPoint(auxHit->pathIndex, auxHit->pointIndex);
      view.clearPhysicsSelection();
    } else if (view.showPositionPoints() && state.selectPositionAt(world.x, world.z, pickRadiusWorld)) {
      view.clearPhysicsSelection();
    } else {
      // Physics sample points (debug overlay, EDITOR_PARITY_FIXES.md gap 10): picked after
      // authored control points so editing is never obstructed, but before zones/triggers/mesh
      // regions -- mirrors physicsPointAtTop's placement in editor.js's mousedown handler.
      // Read-only: selecting one just shows its baked values, no drag.
      const auto physicsHit = view.showPhysicsPoints() ? physicsPointAtWorld(baked, world.x, world.z, kPickRadiusPx / view.scale()) : std::nullopt;
      if (physicsHit.has_value()) {
        view.selectPhysicsSample(physicsHit->pathIndex, physicsHit->frameIndex);
        state.clearSelection();
        state.clearMeshSelection();
        state.clearZoneSelection();
        state.clearTriggerSelection();
      } else {
        view.clearPhysicsSelection();
        // Zones checked before mesh regions: a zone is usually the smaller, more specific thing
        // drawn on top of a region it sits on (a boost pad on a mesh plaza, say), so it should win
        // a click over the region beneath it.
        const tox::Zone* zone = zoneAtWorld(baked, world.x, world.z);
        if (zone != nullptr) {
          state.selectZone(zone->id);
        } else {
          // Triggers: gate lines, picked alongside zones (before the big mesh regions) -- mirrors
          // js/editor.js's mousedown ordering (zoneAtTop then triggerAtTop, both before
          // meshAtWorld).
          const tox::Trigger* trigger = triggerAtWorld(baked, world.x, world.z, pickRadiusWorld);
          if (trigger != nullptr) {
            state.selectTrigger(trigger->id);
          } else {
            const tox::MeshRegion* region = meshRegionAt(baked, world.x, world.z);
            if (region != nullptr)
              state.selectMesh(region->id);
            else
              state.clearMeshSelection();
          }
        }
      }
    }
    panDragActive = !state.selection().valid() && !state.selectedMeshId().has_value() && !state.selectedZoneId().has_value() &&
                    !state.selectedTriggerId().has_value();
  }

  // Gated on itemActive (this canvas's own InvisibleButton captured the mouse-down), not just a
  // global drag gesture -- ImGui::IsMouseDragging() alone is true regardless of which window's
  // widget is actually being dragged, so without this gate, dragging a point in ElevationView's
  // own canvas (a separate InvisibleButton) would ALSO be seen as a drag here on every frame both
  // windows draw, spuriously overwriting the selected point's X/Z with whatever world position the
  // mouse happens to be over in THIS view. Mirrors the itemActive gating handleRailsModeInput
  // already uses for right-click panning, just applied to the left-click point/mesh drag path too.
  const bool draggingGesture = itemActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f);
  // selectionIsPosition() (not just selection().valid()) guards this: a roll/width/cross-section
  // handle counts as a valid selection too (EDITOR_PARITY_FIXES.md gap 1's handles are click-to-
  // select only), but dragSelectedTo() writes to Vec3 pos.x/z, which those point kinds don't use
  // for placement at all (they're positioned by `t` along the baked centerline) -- without this
  // guard, dragging after clicking a roll handle would silently mutate an inert field and push a
  // no-visible-effect undo step instead of doing nothing, which is what gap 1 actually promises.
  if (draggingGesture && state.selectionIsPosition() && !panDragActive) {
    if (!state.dragging()) {
      state.beginDrag();
      view.freezeBounds(preDragBounds);
    }
    const WorldPoint2D world = view.snapWorldXZ(view.screenToWorld(mouseLocal.x, mouseLocal.y));
    state.dragSelectedTo(world.x, world.z);
    mutated = true;
  } else if (state.dragging() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    state.endDrag();
    view.releaseBoundsFreeze();
  } else if (draggingGesture && panDragActive) {
    // Left-drag-on-empty-space pan, mirrors editor.js's 'panTop' -- decided at mousedown (see
    // panDragActive's own comment above), not just "nothing happens to be selected right now".
    view.pan(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y);
  } else if (draggingGesture && state.selectedMeshId().has_value()) {
    const WorldPoint2D world = view.screenToWorld(mouseLocal.x, mouseLocal.y);
    const bool rotate = ImGui::GetIO().KeyShift;
    if (!state.meshDragging() && !state.meshRotating()) {
      view.freezeBounds(preDragBounds);
      if (rotate) {
        const MeshPlacement* placement = state.findMeshPlacement(*state.selectedMeshId());
        if (placement != nullptr) state.beginMeshRotate(angleFromOriginDeg(placement->x, placement->z, world.x, world.z));
      } else {
        state.beginMeshDrag(world.x, world.z);
      }
    }
    if (state.meshRotating()) {
      const MeshPlacement* placement = state.findMeshPlacement(*state.selectedMeshId());
      if (placement != nullptr) state.dragMeshRotateTo(angleFromOriginDeg(placement->x, placement->z, world.x, world.z));
    } else if (state.meshDragging()) {
      const WorldPoint2D snapped =
          view.snapWorldXZ({world.x + state.meshDragOffsetX(), world.z + state.meshDragOffsetZ()});
      state.dragMeshTo(snapped.x, snapped.z);
    }
    mutated = true;
  } else if ((state.meshDragging() || state.meshRotating()) && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    state.endMeshDrag();
    state.endMeshRotate();
    view.releaseBoundsFreeze();
  }
  return mutated;
}

// Rails mode is modal: a left click either toggles the nearest edge within pick tolerance, or (no
// edge hit) falls back to panning -- mirrors editor.js's topCanvas mousedown 'rails' branch
// exactly, including that a miss still starts a pan rather than doing nothing.
bool handleRailsModeInput(EditorState& state, TopDownView& view, const ImVec2& mouseLocal, double edgePickToleranceWorld, bool hovered,
                          bool itemActive) {
  bool mutated = false;
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    const WorldPoint2D world = view.screenToWorld(mouseLocal.x, mouseLocal.y);
    const auto hit = meshEdgeAtWorld(state.track(), world.x, world.z, edgePickToleranceWorld);
    if (hit.has_value())
      mutated = state.toggleRailEdge(hit->meshId, hit->assetId, hit->edgeId);
    else
      state.clearRailSelection();
  }
  if (itemActive && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) view.pan(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y);
  return mutated;
}

// Left click: add a draft point, or close/finish the draft (mirrors createModeClick). Right
// click: cancel the draft (mirrors create mode's button===2 handling).
bool handleCreateModeInput(EditorState& state, const TopDownView& view, const ImVec2& mouseLocal, double pickRadiusWorld, bool hovered) {
  if (!hovered) return false;
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    state.cancelCreateDraft();
    return false;
  }
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    const WorldPoint2D world = view.screenToWorld(mouseLocal.x, mouseLocal.y);
    const WorldPoint2D snapped = view.snapWorldXZ(world);
    return state.createModeClick(world.x, world.z, pickRadiusWorld, snapped.x, snapped.z);
  }
  return false;
}

}  // namespace

bool DrawTopDownCanvas(TopDownView& view, EditorState& state, const tox::Track* baked) {
  const TrackBounds2D bounds = computeViewBounds(state.track(), baked);

  ImGui::BeginChild("TopDownCanvas", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
  ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  canvasSize.x = std::max(1.0f, canvasSize.x);
  canvasSize.y = std::max(1.0f, canvasSize.y);

  view.computeView(bounds, canvasSize.x, canvasSize.y);

  // Zoom slider + Home (EDITOR_PARITY_FIXES.md-adjacent UI pass), mirrors editor.html's
  // #topZoomControl: a vertical zoom slider (same -100..250 range as scroll-wheel zoom, see
  // TopDownView::kZoomSliderMin/Max) plus a reset-to-default button, overlaid at the bottom-right
  // corner of the canvas.
  //
  // Submitted here, BEFORE topDownCanvasInput below, so it gets first claim on clicks in its own
  // screen rect: ImGui resolves overlapping widgets by submission order, not draw order -- once
  // ANY earlier-submitted item under the mouse becomes ImGui's ActiveId, every later-submitted
  // item's own ItemHoverable() check fails for the rest of that click (ActiveIdAllowOverlap isn't
  // set here), so it can never become hovered/clicked either. topDownCanvasInput is a single
  // InvisibleButton spanning the WHOLE canvas; if it were submitted first (as it originally was,
  // with this control drawn afterward purely for visual layering), it would silently swallow
  // every click landing anywhere inside the slider's rect, including on the slider itself -- this
  // is exactly why the slider previously didn't register clicks at all.
  //
  // To still render on top of the canvas background/road/point drawing (which happens later, via
  // raw drawList calls that don't participate in hover/active resolution at all -- only actual
  // widgets like this one do), the window draw list is split into two channels: this control's
  // own draw commands go to channel 1 now, everything else stays on channel 0, and
  // ChannelsMerge() at the very end concatenates them back in that order (0 under 1) so the
  // control paints over the canvas content regardless of submission order.
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->ChannelsSplit(2);
  drawList->ChannelsSetCurrent(1);
  {
    constexpr float kControlWidth = 40.0f, kSliderHeight = 160.0f, kMargin = 14.0f, kPad = 8.0f;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float lineHeight = ImGui::GetTextLineHeight();
    const float groupHeight = lineHeight + style.ItemSpacing.y + kSliderHeight + style.ItemSpacing.y + lineHeight + style.ItemSpacing.y +
                              ImGui::GetFrameHeight();
    const ImVec2 groupPos(canvasOrigin.x + canvasSize.x - kControlWidth - kMargin - kPad,
                          canvasOrigin.y + canvasSize.y - groupHeight - kMargin - kPad);
    const ImVec2 panelMin(groupPos.x - kPad, groupPos.y - kPad);
    const ImVec2 panelMax(groupPos.x + kControlWidth + kPad, groupPos.y + groupHeight + kPad);
    // Mirrors #topZoomControl's CSS: rgba(16,32,46,0.82) fill, #2c6a9e 1px border, 8px radius.
    drawList->AddRectFilled(panelMin, panelMax, IM_COL32(16, 32, 46, 209), 8.0f);
    drawList->AddRect(panelMin, panelMax, IM_COL32(44, 106, 158, 255), 8.0f, 0, 1.0f);

    ImGui::SetCursorScreenPos(groupPos);
    ImGui::PushID("TopDownZoomControl");
    ImGui::BeginGroup();
    // Mirrors .zoomLabel's --accent (#4fd6ff) colour.
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(79, 214, 255, 255));
    ImGui::TextUnformatted(" +");
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(10, 26, 38, 255));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(15, 36, 52, 255));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(20, 46, 66, 255));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, IM_COL32(79, 214, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(140, 230, 255, 255));
    float zoomValue = static_cast<float>(view.zoomSlider());
    // Unlike zoomAt() (scroll wheel), dragging the slider does not re-anchor on a screen point --
    // mirrors editor.js's topZoomSlider 'input' handler, which calls setTopZoomSliderValue()
    // directly rather than zoomTopAt(), so it zooms about the view's current center.
    if (ImGui::VSliderFloat("##zoom", ImVec2(kControlWidth, kSliderHeight), &zoomValue, static_cast<float>(TopDownView::kZoomSliderMin),
                            static_cast<float>(TopDownView::kZoomSliderMax), ""))
      view.setZoomSlider(zoomValue);
    ImGui::PopStyleColor(5);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(79, 214, 255, 255));
    ImGui::TextUnformatted(" -");
    ImGui::PopStyleColor();
    // Mirrors #topHomeBtn's #16344a bg / #1f4c6b hover.
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(22, 52, 74, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(31, 76, 107, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(44, 106, 158, 255));
    if (ImGui::Button("Home", ImVec2(kControlWidth, 0))) view.resetView();
    ImGui::PopStyleColor(3);
    ImGui::EndGroup();
    ImGui::PopID();
  }
  drawList->ChannelsSetCurrent(0);
  // The zoom control's SetCursorScreenPos(groupPos)/EndGroup() left ImGui's cursor wherever the
  // control's group ended (bottom-right corner), not back at canvasOrigin -- every subsequent
  // widget positions itself from the CURRENT cursor, not from any parameter, so without this
  // reset topDownCanvasInput below would be placed at the wrong screen location entirely, and
  // every hover/click test against it (control points, mesh regions, zones, triggers -- the whole
  // canvas) would silently stop matching the mouse's actual position.
  ImGui::SetCursorScreenPos(canvasOrigin);

  ImGui::InvisibleButton("topDownCanvasInput", canvasSize,
                         ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
  const bool hovered = ImGui::IsItemHovered();
  const bool itemActive = ImGui::IsItemActive();
  const bool windowFocused = ImGui::IsWindowFocused();
  const ImVec2 mouseLocal = ImVec2(ImGui::GetIO().MousePos.x - canvasOrigin.x, ImGui::GetIO().MousePos.y - canvasOrigin.y);

  if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
    // 15 slider units per wheel notch (editor.js uses deltaY*0.16 against ~100px/notch deltaY,
    // i.e. ~16 units/notch); ImGui's MouseWheel is already normalized to ~1 per notch.
    view.zoomAt(mouseLocal.x, mouseLocal.y, ImGui::GetIO().MouseWheel * 15.0, bounds);
  }

  bool mutated = false;
  static WorldPoint2D contextMenuWorld;  // set right before OpenPopup, read once BeginPopup opens it
  const double pickRadiusWorld = kPickRadiusPx / view.scale();
  switch (state.mode()) {
    case EditMode::Edit: {
      mutated = handleEditModeInput(state, view, baked, bounds, mouseLocal, pickRadiusWorld, hovered, itemActive);
      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) view.pan(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y);
      if (windowFocused && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
        if (state.selection().valid())
          mutated = state.deleteSelectedPoint() || mutated;
        else if (state.selectedMeshId().has_value())
          mutated = state.deleteSelectedMesh() || mutated;
        else if (state.selectedZoneId().has_value())
          mutated = state.deleteSelectedZone() || mutated;
        else if (state.selectedTriggerId().has_value())
          mutated = state.deleteSelectedTrigger() || mutated;
      }
      // Right-click context menu (EDITOR_NATIVE_FILE_IO_PLAN.md M9, extended by
      // EDITOR_PARITY_FIXES.md gap 13): a right-*click* (no drag) opens it instead of panning; a
      // real drag still pans, since ResetMouseDragDelta below only ever fires on release, after
      // the drag's own per-frame pan deltas already applied. Mirrors editor.html's #addPointMenu
      // except "Position" (`insertNear`) -- that needs inserting a point mid-segment, i.e. gap 11's
      // (unimplemented) segment machinery, so it's left out here rather than half-done.
      if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        const ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right, 0.0f);
        if (std::abs(dragDelta.x) < 3.0f && std::abs(dragDelta.y) < 3.0f) {
          contextMenuWorld = view.screenToWorld(mouseLocal.x, mouseLocal.y);
          ImGui::OpenPopup("TopDownContextMenu");
        }
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
      }
      if (ImGui::BeginPopup("TopDownContextMenu")) {
        // Add control point: Roll/Width/Cross-section, mirroring insertRollPointAtWorld/
        // insertWidthPoint/insertCrossSectionPoint -- seeded from the nearest baked centerline
        // frame's actual current value at that point (not a schema default), same as JS.
        const std::optional<NearestPathPlacement> nearPlacement = nearestPathPlacement(baked, contextMenuWorld.x, contextMenuWorld.z);
        ImGui::TextDisabled("Add control point");
        ImGui::BeginDisabled(!nearPlacement.has_value());
        // Position (EDITOR_PARITY_FIXES.md gap 11, finishing what gap 13 deferred): mirrors
        // insertNear -- `t` (already a curve-parametric fraction, whether it came from the fine
        // evaluator in JS or the baked centerline's discrete samples here) reconstructs the
        // approximate segment index the same way insertNear's own `g = t * gMax` does. World X/Z
        // are the exact click position (not the nearest sample's); elevation is the nearest
        // baked frame's Y, standing in for JS's real curve-evaluated Y at that point.
        if (ImGui::MenuItem("Position")) {
          const Path& authoredPath = state.track().paths[nearPlacement->pathIndex];
          const int n = EditorState::positionCount(authoredPath);
          const double gMax = authoredPath.closed ? n : n - 1;
          const double g = nearPlacement->t * gMax;
          const int insertAt = authoredPath.closed ? (static_cast<int>(std::floor(g)) + 1) % (n + 1)
                                                    : std::min(n, static_cast<int>(std::floor(g)) + 1);
          if (state
                  .insertPositionOnSegment(nearPlacement->pathIndex, insertAt, contextMenuWorld.x, nearPlacement->frame->pos.y,
                                           contextMenuWorld.z)
                  .has_value())
            mutated = true;
        }
        if (ImGui::MenuItem("Roll")) {
          const auto index = state.addAuxPoint(nearPlacement->pathIndex, PointKind::Roll, nearPlacement->t);
          if (index.has_value()) {
            const double rollDeg = nearPlacement->frame->roll * 180.0 / std::numbers::pi;
            state.editAuxPoint(nearPlacement->pathIndex, *index, [&](TrackPoint& p) { p.roll = rollDeg; });
            mutated = true;
          }
        }
        if (ImGui::MenuItem("Width")) {
          const auto index = state.addAuxPoint(nearPlacement->pathIndex, PointKind::Width, nearPlacement->t);
          if (index.has_value()) {
            const double width = nearPlacement->frame->width;
            state.editAuxPoint(nearPlacement->pathIndex, *index, [&](TrackPoint& p) { p.width = width; });
            mutated = true;
          }
        }
        if (ImGui::MenuItem("Cross-Section")) {
          const auto index = state.addAuxPoint(nearPlacement->pathIndex, PointKind::CrossSection, nearPlacement->t);
          if (index.has_value()) {
            const double curvature = nearPlacement->frame->crossSectionCurvature, tightness = nearPlacement->frame->crossSectionTightness,
                         thickness = nearPlacement->frame->crossSectionThickness;
            state.editAuxPoint(nearPlacement->pathIndex, *index, [&](TrackPoint& p) {
              p.curvature = curvature;
              p.tightness = tightness;
              p.thickness = thickness;
            });
            mutated = true;
          }
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::TextDisabled("Add mesh");
        if (ImGui::MenuItem("Paste Mesh")) {
          // Mirrors importMeshFromClipboard(centreOn): centred on the click, unlike the toolbar
          // Paste Mesh button (world origin) or Import Mesh (current view centre).
          if (const auto text = readClipboardText()) {
            mutated = !state.importMeshFromJsonText(*text, "pasted-mesh", contextMenuWorld.x, contextMenuWorld.z).has_value() || mutated;
          }
        }

        // Add zone/trigger: mirrors addZoneAt/addTriggerAt's path-anchored branch (mesh-hosted
        // creation from this menu is out of scope -- gaps 3/4 already don't support it either).
        ImGui::Separator();
        ImGui::TextDisabled("Add zone");
        ImGui::BeginDisabled(!nearPlacement.has_value());
        if (ImGui::MenuItem("Boost")) {
          mutated = state.addPathZone(nearPlacement->pathIndex, "velocityChange", nearPlacement->t, nearPlacement->lateral).has_value() || mutated;
        }
        if (ImGui::MenuItem("Start Grid")) {
          mutated = state.addPathZone(nearPlacement->pathIndex, "startGrid", nearPlacement->t, nearPlacement->lateral).has_value() || mutated;
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::TextDisabled("Add trigger");
        ImGui::BeginDisabled(!nearPlacement.has_value());
        if (ImGui::MenuItem("Dummy")) {
          mutated = state.addPathTrigger(nearPlacement->pathIndex, "dummy", nearPlacement->t).has_value() || mutated;
        }
        if (ImGui::MenuItem("Checkpoint")) {
          mutated = state.addPathTrigger(nearPlacement->pathIndex, "checkpoint", nearPlacement->t).has_value() || mutated;
        }
        ImGui::EndDisabled();

        ImGui::EndPopup();
      }
      break;
    }
    case EditMode::Create:
      mutated = handleCreateModeInput(state, view, mouseLocal, pickRadiusWorld, hovered);
      if (mutated) state.setMode(EditMode::Edit);  // mirrors setEditMode('edit') after finishCreateDraft
      break;
    case EditMode::Rails: {
      const double edgePickToleranceWorld = kMeshEdgePickPx / view.scale();
      mutated = handleRailsModeInput(state, view, mouseLocal, edgePickToleranceWorld, hovered, ImGui::IsItemActive());
      break;
    }
  }

  drawList->AddRectFilled(canvasOrigin, ImVec2(canvasOrigin.x + canvasSize.x, canvasOrigin.y + canvasSize.y), kBackgroundColor);
  drawGrid(drawList, canvasOrigin, canvasSize, view);
  if (baked != nullptr) {
    // Elevation render mode colors each segment by its position in the FULL track's elevation
    // range (every path, not just the one being drawn) -- mirrors drawTop's `allY =
    // pathPreviews.flatMap(p => p.yAt)` computed once before the per-path draw loop.
    double minElev = std::numeric_limits<double>::infinity(), maxElev = -std::numeric_limits<double>::infinity();
    if (view.renderMode() == TopDownView::RenderMode::Elevation) {
      for (const auto& path : baked->paths)
        for (const auto& frame : path.centerline) {
          minElev = std::min(minElev, frame.pos.y);
          maxElev = std::max(maxElev, frame.pos.y);
        }
    }
    for (const auto& path : baked->paths) drawBakedPath(drawList, canvasOrigin, view, path, view.renderMode(), minElev, maxElev);
    drawMeshRegions(drawList, canvasOrigin, view, baked->meshRegions, state.selectedMeshId());
    drawZones(drawList, canvasOrigin, view, *baked, state.selectedZoneId());
    drawTriggers(drawList, canvasOrigin, view, *baked, state.selectedTriggerId());
    if (view.showPhysicsPoints()) drawPhysicsPoints(drawList, canvasOrigin, view, *baked);
  }
  drawMeshRails(drawList, canvasOrigin, view, state.track(), state.selectedRail());
  if (view.showPositionPoints()) {
    // Hover highlight (distinct from click-driven selection): only meaningful in Edit mode, the
    // only mode where a plain click on a position point does anything (Create mode's clicks build
    // a draft path; Rails mode picks mesh edges, not authored points).
    std::optional<SelectedPoint> hoveredPosition;
    if (hovered && state.mode() == EditMode::Edit) {
      const WorldPoint2D hoverWorld = view.screenToWorld(mouseLocal.x, mouseLocal.y);
      hoveredPosition = state.hoverTestPosition(hoverWorld.x, hoverWorld.z, pickRadiusWorld);
    }
    drawAuthoredPositionPoints(drawList, canvasOrigin, view, state.track(), state.selection(), hoveredPosition);
  }
  // Roll/width/cross-section handles (EDITOR_PARITY_FIXES.md gap 1), drawn after position points
  // -- mirrors editor.js's drawTop() drawing them in that same order (position at
  // js/editor.js:1171, roll/width/crossSection at :1245-1298), so they sit on top.
  drawAuxPoints(drawList, canvasOrigin, view, state.track(), baked, state.currentPathIndex(), state.selection());
  if (state.mode() == EditMode::Create) drawCreateDraft(drawList, canvasOrigin, view, state.createDraft());

  // Merges channel 1 (the zoom control, submitted early for interaction priority -- see the
  // comment where ChannelsSplit was called) back on top of channel 0 (everything drawn above).
  drawList->ChannelsMerge();

  ImGui::EndChild();
  return mutated;
}

}  // namespace editor
