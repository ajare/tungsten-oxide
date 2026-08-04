#include "TopDownCanvas.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui.h"

#include "MppModelImport.hpp"
#include "TrackCore.hpp"

namespace editor {
namespace {

// Projects a world position into the active ProjectionMode's 2D drag/render plane -- TopDown
// (x, z, today's only behavior, Z increasing = screen down, an existing convention left alone),
// Front (x, -y), Side (z, -y). Both Front and Side put Y (height) in the SECOND (screen-Y-bound,
// via worldToScreen) slot, negated: worldToScreen's second argument increases the screen Y pixel
// coordinate DOWNWARD, so without the negation, moving up in world Y would draw lower on screen --
// the opposite of every "elevation view" convention (and of TopDown's own Z-down convention, which
// nobody expects to mean "up"). Side's first slot is Z, not Y: looking along Side's view direction
// (X = 1) at the YZ plane, Z is the "along the track" axis that belongs on the horizontal screen
// axis, exactly as X is for Front and TopDown. Mirrors EditorState's own private planeCoords.
WorldPoint2D planeCoords(ProjectionMode mode, const tox::Vec3& p) {
  switch (mode) {
    case ProjectionMode::Front: return {p.x, -p.y};
    case ProjectionMode::Side: return {p.z, -p.y};
    case ProjectionMode::TopDown:
    default: return {p.x, p.z};
  }
}

// Inverse of planeCoords: writes (u, v) into the two axes the mode's plane covers, leaving the
// third axis (not touched by this operation) unchanged. Mirrors EditorState's own private
// setPlaneCoords -- used here only where a NEW world position has to be built from a click (the
// context menu's "Add control point"), since every drag/hit-test already routes through
// EditorState's copy.
void setPlaneCoords(ProjectionMode mode, tox::Vec3& p, double u, double v) {
  switch (mode) {
    case ProjectionMode::Front: p.x = u; p.y = -v; break;
    case ProjectionMode::Side: p.z = u; p.y = -v; break;
    case ProjectionMode::TopDown:
    default: p.x = u; p.z = v; break;
  }
}

// v.x/v.z projected through planeCoords, then fed to worldToScreen -- the single conversion every
// rendering call site below should use instead of `view.worldToScreen(v.x, v.z)` directly, now that
// "x, z" no longer always means world X/Z (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 1.2's canvas
// projection-mode generalization, extended past position points to the rest of the canvas per
// follow-up feedback that Front/Side still rendered everything else in the XZ plane).
ScreenPoint2D worldToScreenPlane(const TopDownView& view, ProjectionMode mode, const tox::Vec3& v) {
  const WorldPoint2D p = planeCoords(mode, v);
  return view.worldToScreen(p.x, p.z);
}

constexpr float kPointRadius = 4.0f;
constexpr float kPickRadiusPx = 10.0f;
const ImU32 kBackgroundColor = IM_COL32(8, 20, 29, 255);
const ImU32 kGridColor = IM_COL32(255, 255, 255, 18);
const ImU32 kRoadColor = IM_COL32(60, 70, 82, 255);
const ImU32 kCenterlineColor = IM_COL32(120, 170, 220, 200);
// The "current" curve (EditorState::currentPathIndex(), settable by clicking a curve's ribbon --
// see pathAtWorld -- or the CurvesPanel dropdown) gets a distinct warm/gold centerline, brighter
// and thicker than the default blue, rather than the red used for mesh/zone/trigger selection:
// unlike those, there's always exactly one current path (it defaults to path 0, never "none"), so
// this reads as a persistent "which curve is active" indicator, not a removable selection.
const ImU32 kCurrentPathCenterlineColor = IM_COL32(255, 224, 102, 255);
constexpr float kCenterlineThickness = 2.0f;
constexpr float kCurrentPathCenterlineThickness = 3.5f;
// Selected-point segment highlight: incoming (previous) segment in green #31d66b, outgoing (next)
// segment in red #ff3344, both solid 4px lines -- despite reading like a red/blue convention at a
// glance.
const ImU32 kIncomingSegmentColor = IM_COL32(49, 214, 107, 255);
const ImU32 kOutgoingSegmentColor = IM_COL32(255, 51, 68, 255);
constexpr float kSegmentHighlightThickness = 4.0f;
// Drag-to-weld target ring (see EditorState::hitTestOpenEndpoint's comment): the same green as the
// join-drag target highlight below ('#31d66b'), drawn as an extra outer ring so it reads clearly
// alongside the selected/hover rings above without a fourth distinct fill color to track.
const ImU32 kWeldTargetColor = IM_COL32(49, 214, 107, 255);
constexpr float kWeldTargetRingThickness = 2.5f;
// Shift-drag rubber band (a separate interaction alongside the plain
// drag-to-weld above): a live line from the dragged endpoint to the cursor -- yellow while free,
// the SAME green as kWeldTargetColor once snapped onto a valid drop target ('#ffd23c'/'#31d66b').
// Drawn solid rather than dashed: this file already accepts that simplification elsewhere (e.g.
// drawCreateDraft's solid line for a dashed create-mode draft).
const ImU32 kJoinDragFreeColor = IM_COL32(255, 210, 60, 255);
constexpr float kJoinDragLineThickness = 2.0f;
constexpr float kJoinDragMinPx = 12.0f;
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
// Width color #b6ff3c / cross-section color #d58cff; roll's own stroke
// colour is computed per-point by rollTint() below, not a fixed
// constant. All three handles fill white regardless of selection -- only stroke width/handle size
// change.
const ImU32 kWidthColor = IM_COL32(182, 255, 60, 255);
const ImU32 kCrossSectionColor = IM_COL32(213, 140, 255, 255);
const ImU32 kAuxHandleFillColor = IM_COL32(255, 255, 255, 255);
const ImU32 kCreateDraftColor = IM_COL32(120, 230, 140, 255);
const ImU32 kMeshFillColor = IM_COL32(90, 110, 70, 200);
const ImU32 kMeshOutlineColor = IM_COL32(150, 190, 110, 255);
const ImU32 kMeshSelectedOutlineColor = IM_COL32(255, 90, 90, 255);
const ImU32 kRailEdgeColor = IM_COL32(255, 170, 40, 255);
const ImU32 kRailEdgeSelectedColor = IM_COL32(255, 90, 90, 255);
// Distinct from kRailEdgeColor (a real placed-mesh rail) so a central-reservation's boundary reads
// as its own kind of wall at a glance -- cool violet against the warm rail orange.
const ImU32 kReservationWallColor = IM_COL32(150, 130, 255, 255);
// Disjoint-seam node ring + X cross ('#ffcc44' styling) -- unlike ElevationView.cpp's own disjoint
// styling, which is ring-only, this view also draws the X cross.
const ImU32 kDisjointColor = IM_COL32(255, 204, 68, 255);
// Position-point index/elevation label ('#bfe6f7').
const ImU32 kPositionLabelColor = IM_COL32(191, 230, 247, 255);

// Physics-sample dot colors: '#ff9c3c' idle, '#ff5ea8' selected.
const ImU32 kPhysicsPointColor = IM_COL32(255, 156, 60, 255);
const ImU32 kPhysicsSelectedColor = IM_COL32(255, 94, 168, 255);
constexpr float kMeshEdgePickPx = 8.0f;

// Self-intersection crossing markers: grey/amber for the automatic decision, red/green
// once a user override forces the opposite. kCrossingHaloColor is the dark contrast halo drawn
// behind every marker so it stays visible over any ribbon/centerline color.
enum class CrossingState { AutoCollapse,
                           AutoKeep,
                           ForcedCollapse,
                           ForcedKeep };
const ImU32 kCrossingAutoCollapseColor = IM_COL32(185, 194, 208, 255);  // #b9c2d0
const ImU32 kCrossingAutoKeepColor = IM_COL32(255, 176, 32, 255);       // #ffb020
const ImU32 kCrossingForcedCollapseColor = IM_COL32(255, 51, 85, 255);  // #ff3355
const ImU32 kCrossingForcedKeepColor = IM_COL32(55, 209, 122, 255);     // #37d17a
const ImU32 kCrossingHaloColor = IM_COL32(0, 0, 0, 153);
constexpr float kCrossingHitRadiusPx = 11.0f;

// Zone fill/stroke colors, minus the startGrid checker
// pattern, which is cosmetic only -- a flat fill reads fine at editor zoom levels.
const ImU32 kZoneBoostFillColor = IM_COL32(255, 165, 32, 107);         // rgba(255,165,32,0.42)
const ImU32 kZoneBoostStrokeColor = IM_COL32(255, 176, 32, 255);       // #ffb020
const ImU32 kZoneJumpFillColor = IM_COL32(83, 200, 255, 107);          // rgba(83,200,255,0.42)
const ImU32 kZoneJumpStrokeColor = IM_COL32(83, 200, 255, 255);        // #53c8ff
const ImU32 kZoneStartGridFillColor = IM_COL32(207, 214, 221, 97);     // rgba(207,214,221,0.38)
const ImU32 kZoneStartGridStrokeColor = IM_COL32(207, 214, 221, 255);  // #cfd6dd
const ImU32 kZoneSelectedStrokeColor = IM_COL32(255, 90, 90, 255);

// Trigger colors. Selection is shown
// via line/point weight only (matching drawTriggers), not a separate highlight color.
const ImU32 kTriggerDummyColor = IM_COL32(255, 94, 168, 255);        // #ff5ea8
const ImU32 kTriggerCheckpointColor = IM_COL32(127, 231, 255, 255);  // #7fe7ff
const ImU32 kTriggerFinishColor = IM_COL32(255, 211, 79, 255);       // #ffd34f

// Authored control points' bounds. Projected into the active ProjectionMode's plane so Front/Side
// auto-fit to the track's actual (x, y)/(y, z) extent instead of its XZ footprint. Used to include
// every placed mesh region's baked bounds too (MeshRegion, removed in DRIVABLE_MESH_OBJECTS_PLAN.md
// Milestone 2), so `baked` is unused now but kept as a parameter for callers that still pass it.
TrackBounds2D computeViewBounds(const TrackDefinition& track, const tox::Track* baked, ProjectionMode mode) {
  (void)baked;
  TrackBounds2D bounds{1e300, -1e300, 1e300, -1e300};
  for (const auto& path : track.paths) {
    for (const auto& point : path.points) {
      if (point.kind != PointKind::Position) continue;
      const WorldPoint2D p = planeCoords(mode, point.pos);
      bounds.minX = std::min(bounds.minX, p.x);
      bounds.maxX = std::max(bounds.maxX, p.x);
      bounds.minZ = std::min(bounds.minZ, p.z);
      bounds.maxZ = std::max(bounds.maxZ, p.z);
    }
  }
  for (const auto& placement : track.meshObjects) {
    const WorldPoint2D p = planeCoords(mode, placement.position);
    bounds.minX = std::min(bounds.minX, p.x);
    bounds.maxX = std::max(bounds.maxX, p.x);
    bounds.minZ = std::min(bounds.minZ, p.z);
    bounds.maxZ = std::max(bounds.maxZ, p.z);
  }
  if (bounds.minX > bounds.maxX) return TrackBounds2D{-1.0, 1.0, -1.0, 1.0};
  return bounds;
}

ImVec2 toAbsolute(const ImVec2& canvasOrigin, const ScreenPoint2D& local) {
  return ImVec2(canvasOrigin.x + static_cast<float>(local.x), canvasOrigin.y + static_cast<float>(local.y));
}
ImVec2 toAbsolute(const ImVec2& canvasOrigin, const ImVec2& local) { return ImVec2(canvasOrigin.x + local.x, canvasOrigin.y + local.y); }

// Road-fill color formulas for Flat/Elevation render modes.
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
// Road-fill color for Camber mode: white at zero (no roll, or a straight/near-straight section
// with no meaningful turn direction -- |curvature| below kCamberCurvatureEpsilon), blending toward
// saturated green as the road banks INTO the turn (on-camber) or saturated red as it banks AWAY
// from the turn (off-camber), reaching full saturation at kCamberSaturationRollRad of roll.
//
// `curvature` is TrackCore::pathSignedCurvatureAt's signed value (positive = left turn, i.e. away
// from cross(UP, tangent) -- see its doc comment). Combined with roll's own sign convention from
// TrackBake.cpp's frame() (edgeRight = h*cos(-roll) + bn*sin(-roll), where h = cross(UP, tangent)
// and bn defaults to +up): a positive roll always banks the h/edgeRight side down and the -h side
// up, regardless of heading. Working through both turn directions against that fixed relationship
// shows the two verdicts share the SAME sign as (roll * curvature) for off-camber and OPPOSITE
// signs for on-camber -- e.g. a left turn (positive curvature) with positive roll lowers the
// outside (right/h) edge, which is banking away from the turn: off-camber.
constexpr double kCamberSaturationRollRad = 45.0 * std::numbers::pi / 180.0;
constexpr double kCamberCurvatureEpsilon = 1e-4;  // 1/metres
ImU32 camberFillColor(double rollRad, double curvature) {
  if (std::abs(curvature) < kCamberCurvatureEpsilon) return IM_COL32(255, 255, 255, 255);
  const double t = std::clamp(std::abs(rollRad) / kCamberSaturationRollRad, 0.0, 1.0);
  const bool onCamber = (rollRad * curvature) < 0.0;
  const double targetR = onCamber ? 40.0 : 210.0;
  const double targetG = onCamber ? 210.0 : 50.0;
  constexpr double targetB = 55.0;
  const int r = static_cast<int>(std::lround(255.0 + (targetR - 255.0) * t));
  const int g = static_cast<int>(std::lround(255.0 + (targetG - 255.0) * t));
  const int b = static_cast<int>(std::lround(255.0 + (targetB - 255.0) * t));
  return IM_COL32(r, g, b, 255);
}

// Position-point node fill: blue (low) -> teal -> warm (high). ElevationView.cpp has its own
// independent copy of this same function for its own node fill, per this codebase's established
// per-file duplication of small color helpers.
ImU32 heightColor(double y) {
  const double t = std::clamp(y / 8.0, -1.0, 1.0);
  const int r = static_cast<int>(std::lround(60.0 + 150.0 * std::max(0.0, t)));
  const int g = static_cast<int>(std::lround(150.0 + 60.0 * (1.0 - std::abs(t))));
  const int b = static_cast<int>(std::lround(180.0 - 120.0 * std::max(0.0, t) + 40.0 * std::max(0.0, -t)));
  return IM_COL32(r, g, b, 255);
}

// Configurable top-down reference grid: gated on view.showGrid(), spaced at view.gridSize() world
// units, skipped once screen spacing drops below a `step > 6` threshold rather than smearing into a
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

// `mode`: Banked (default) offsets
// edges by each frame's baked, banked `edgeRight` and fills with a flat color, matching
// TrackCore.buildEdges' non-flat ribbon fill. Flat/Elevation/Camber instead offset by the
// UNROLLED `h` axis (the track's plan-view footprint (width only)
// without banking distorting the top-down shape) and fill each segment by interpolated roll,
// elevation, or camber verdict (rollFillColor/elevationFillColor/camberFillColor) instead of a
// flat color. `minElev`/`maxElev` are ignored outside Elevation mode. `definition` is the
// pre-bake PathDefinition backing `path` -- only used in Camber mode, to evaluate analytical
// curvature via TrackCore::pathSignedCurvatureAt (see that function's doc comment for why this
// isn't derived from the baked frames themselves).
void drawBakedPath(ImDrawList* drawList, const ImVec2& canvasOrigin, const TopDownView& view, const tox::Path& path,
                   const tox::PathDefinition& definition, TopDownView::RenderMode renderMode, double minElev, double maxElev,
                   bool isCurrent, ProjectionMode mode) {
  const std::size_t n = path.centerline.size();
  if (n < 2) return;
  const bool flatEdges = renderMode != TopDownView::RenderMode::Banked;

  // Local cross-track parameter u in [-1, +1] (left edge .. centerline .. right edge), normalized
  // by each ring's own halfW so a gap band from two different-width rings is directly comparable --
  // the same normalization TrackBake.cpp's pathGeometry uses for its own v in [0, 1], just
  // recentred here to match this quad's existing -halfW..+halfW convention.
  auto ringPoint = [&](const tox::Frame& f, const tox::Vec3& axis, double u) {
    return f.pos + axis * (u * f.halfW);
  };
  // A reservation's gap band in u-space for one ring, degenerating to the zero-width point {0,0}
  // when no reservation is active there (reservationHalfGap defaults to 0), so the corner-wise
  // test below never fires and an untouched segment draws exactly as before.
  auto gapBand = [](const tox::Frame& f) {
    const double u = std::min(1.0, f.reservationHalfGap / std::max(f.halfW, 1e-6));
    return std::make_pair(-u, u);
  };

  const std::size_t segmentCount = path.closed ? n : n - 1;
  for (std::size_t i = 0; i < segmentCount; ++i) {
    const std::size_t j = (i + 1) % n;
    const tox::Frame& fi = path.centerline[i];
    const tox::Frame& fj = path.centerline[j];
    const tox::Vec3& axisI = flatEdges ? fi.h : fi.edgeRight;
    const tox::Vec3& axisJ = flatEdges ? fj.h : fj.edgeRight;

    ImU32 fillColor = kRoadColor;
    if (renderMode == TopDownView::RenderMode::Flat) {
      fillColor = rollFillColor((fi.roll + fj.roll) * 0.5 * 180.0 / std::numbers::pi);
    } else if (renderMode == TopDownView::RenderMode::Elevation) {
      fillColor = elevationFillColor((fi.pos.y + fj.pos.y) * 0.5, minElev, maxElev);
    } else if (renderMode == TopDownView::RenderMode::Camber) {
      const double curvatureI = tox::pathSignedCurvatureAt(definition, i, n);
      const double curvatureJ = tox::pathSignedCurvatureAt(definition, j, n);
      fillColor = camberFillColor((fi.roll + fj.roll) * 0.5, (curvatureI + curvatureJ) * 0.5);
    }

    if (fi.reservationHalfGap <= 1e-9 && fj.reservationHalfGap <= 1e-9) {
      // No reservation on this segment: the plain full-width quad, exactly as before.
      const tox::Vec3 leftI = ringPoint(fi, axisI, -1.0), rightI = ringPoint(fi, axisI, 1.0);
      const tox::Vec3 leftJ = ringPoint(fj, axisJ, -1.0), rightJ = ringPoint(fj, axisJ, 1.0);
      const ImVec2 quad[4] = {
          toAbsolute(canvasOrigin, worldToScreenPlane(view, mode, leftI)),
          toAbsolute(canvasOrigin, worldToScreenPlane(view, mode, leftJ)),
          toAbsolute(canvasOrigin, worldToScreenPlane(view, mode, rightJ)),
          toAbsolute(canvasOrigin, worldToScreenPlane(view, mode, rightI)),
      };
      drawList->AddConvexPolyFilled(quad, 4, fillColor);
      continue;
    }

    // A reservation is active on at least one ring of this segment: carve the gap out the same way
    // TrackBake.cpp's pathGeometry does (CENTRAL_RESERVATION_PLAN.md §4f) -- classify each
    // sub-quad's four corners individually against their OWN ring's gap band (breakpoints merged
    // from both rings, so no sub-quad straddles a gap edge), rather than dropping a sub-quad only
    // when both rings agree it's inside the gap, which would draw a staircase instead of the
    // tapered lens the game actually bakes. Coarser than the game's own render mesh (this walks
    // physics-sample rings, ~6m apart, not the finely-subdivided render mesh), but this file already
    // treats its ribbon as an approximation "close enough" for editing at editor zoom levels (see
    // the zone-outline note above).
    const auto [giLo, giHi] = gapBand(fi);
    const auto [gjLo, gjHi] = gapBand(fj);
    const std::set<double> breakSet{-1.0, 1.0, giLo, giHi, gjLo, gjHi};
    const std::vector<double> us(breakSet.begin(), breakSet.end());
    for (std::size_t k = 0; k + 1 < us.size(); ++k) {
      const double a = us[k], z = us[k + 1];
      // Corners in a positively-oriented cycle -- (i,a) (i,z) (j,z) (j,a) -- matching this
      // function's original quad winding (leftI, leftJ, rightJ, rightI) when a=-1, z=1.
      const tox::Frame* ringOf[4] = {&fi, &fi, &fj, &fj};
      const tox::Vec3* axisOf[4] = {&axisI, &axisI, &axisJ, &axisJ};
      const double uOf[4] = {a, z, z, a};
      const std::pair<double, double> bandOf[4] = {{giLo, giHi}, {giLo, giHi}, {gjLo, gjHi}, {gjLo, gjHi}};
      int solidIdx[4], solidCount = 0;
      for (int c = 0; c < 4; ++c) {
        if (uOf[c] > bandOf[c].first + 1e-9 && uOf[c] < bandOf[c].second - 1e-9) continue;  // strictly inside its own ring's gap
        solidIdx[solidCount++] = c;
      }
      // Two or fewer solid corners leaves no solid area: either the whole sub-quad is inside the
      // gap at both rings, or it is inside at one and exactly spans the band at the other.
      if (solidCount <= 2) continue;
      auto pointAt = [&](int c) {
        const tox::Vec3 p = ringPoint(*ringOf[c], *axisOf[c], uOf[c]);
        return toAbsolute(canvasOrigin, worldToScreenPlane(view, mode, p));
      };
      if (solidCount == 4) {
        const ImVec2 quad[4] = {pointAt(0), pointAt(1), pointAt(2), pointAt(3)};
        drawList->AddConvexPolyFilled(quad, 4, fillColor);
      } else {
        const ImVec2 tri[3] = {pointAt(solidIdx[0]), pointAt(solidIdx[1]), pointAt(solidIdx[2])};
        drawList->AddConvexPolyFilled(tri, 3, fillColor);
      }
    }
  }

  std::vector<ImVec2> centerline;
  centerline.reserve(n);
  for (const auto& frame : path.centerline) centerline.push_back(toAbsolute(canvasOrigin, worldToScreenPlane(view, mode, frame.pos)));
  drawList->AddPolyline(centerline.data(), static_cast<int>(centerline.size()),
                        isCurrent ? kCurrentPathCenterlineColor : kCenterlineColor, path.closed ? ImDrawFlags_Closed : ImDrawFlags_None,
                        isCurrent ? kCurrentPathCenterlineThickness : kCenterlineThickness);
}

// A straight screen-space line between the selected
// Position point and one neighbor (not the spline itself), using the baked path's `anchors` --
// the world position of each Position control point in order -- so `SegmentRef::i` (a
// position-space index, see EditorState.hpp) indexes straight into it. No-op if the segment
// doesn't exist (aux point selected, no
// selection, or an open path's boundary point in that direction).
void drawSegmentHighlight(ImDrawList* drawList, const ImVec2& canvasOrigin, const TopDownView& view, const tox::Track* baked,
                          const std::optional<EditorState::SegmentRef>& segment, ImU32 color, ProjectionMode mode) {
  if (!segment.has_value() || baked == nullptr) return;
  if (segment->pathIndex < 0 || segment->pathIndex >= static_cast<int>(baked->paths.size())) return;
  const tox::Path& path = baked->paths[segment->pathIndex];
  const int n = static_cast<int>(path.anchors.size());
  if (segment->i < 0 || segment->i >= n || n < 2) return;
  const tox::Vec3& a = path.anchors[segment->i];
  const tox::Vec3& b = path.anchors[(segment->i + 1) % n];
  drawList->AddLine(toAbsolute(canvasOrigin, worldToScreenPlane(view, mode, a)), toAbsolute(canvasOrigin, worldToScreenPlane(view, mode, b)), color,
                    kSegmentHighlightThickness);
}

// drawMeshRegions/drawReservationWalls (placed-mesh polygons and central-reservation preview
// walls, both sourced from tox::MeshRegion) were removed along with MeshRegion
// (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 2), with no interim replacement -- reservations still
// carve the road void, but draw no wall preview now.

// ---- Zones --------------------------------------------------
//
// core bakes zones into tox::Track::zones (a mesh-hosted rotated rectangle, or a path-hosted strip
// described by gLo/gHi/gMax/lateral/halfWidth into the host path's own parameter space) but not
// into a ready-made 2D outline the way MeshRegion's polygons already are. For a mesh-hosted zone
// that's just a rotated rectangle; for a path-hosted one this samples the host path's baked
// CENTERLINE (linear interpolation between the nearest two samples) rather than re-evaluating the
// underlying rational spline directly -- core keeps its own spline Evaluator private to
// TrackBake.cpp, so nothing equivalent is exposed here. Approximate, but the centerline is already
// sampled densely enough (TrackCore.adaptiveSampleCount, ~6m spacing) that the visual difference at
// editor zoom levels is imperceptible; this is a 2D outline for editing, never fed back into
// physics.

// Maps a path parameter g in [0, gMax] to an interpolated centerline frame, given core's own
// sampling convention: closed paths sample N points spanning [0, gMax) (wrapping), open paths
// sample N points spanning [0, gMax] inclusive of both ends.
//
// Every direction/position field is stored ALREADY PROJECTED into the active ProjectionMode's
// plane (see planeCoords) -- `x`/`z` is a plane position, not necessarily world X/Z; `hX`/`hZ` is
// the UNROLLED horizontal axis (roll/width/cross-section handles are drawn along this, not the
// banked `edgeRight`) projected the same way; `width`/`roll` (radians) are the frame's own baked
// scalars, unaffected by projection. `tangentX/tangentZ` is the raw (un-normalized) driven-
// direction tangent, projected the same way, added for the start-marker arrow -- the only current
// consumer, so it's fine if its magnitude isn't unit length; the arrow only ever uses atan2 on it.
// Projection is a linear map (picking 2 of 3 coordinates), so projecting each of pos/edgeRight/h/
// tangent independently and then doing the surrounding math (offsets, drag deltas, dot products)
// entirely within the projected plane is exactly the "restricted to the 2D plane the user's mouse
// actually lives in" behavior every other on-canvas gesture in this file already wants -- it is NOT
// the same as projecting a full-3D computation's result, but that's the correct choice here, not an
// approximation of it.
struct WorldFrame2D {
  double x{0.0}, z{0.0}, rightX{1.0}, rightZ{0.0}, hX{1.0}, hZ{0.0}, width{1.0}, roll{0.0}, tangentX{0.0}, tangentZ{1.0};
};

WorldFrame2D sampleCenterlineAtG(const std::vector<tox::Frame>& centerline, bool closed, double g, double gMax, ProjectionMode mode) {
  const std::size_t n = centerline.size();
  if (n == 0) return {};
  auto project = [&](const tox::Vec3& pos, const tox::Vec3& right, const tox::Vec3& h, const tox::Vec3& tangent, double width, double roll) {
    const WorldPoint2D p = planeCoords(mode, pos);
    const WorldPoint2D r = planeCoords(mode, right);
    const WorldPoint2D hh = planeCoords(mode, h);
    const WorldPoint2D t = planeCoords(mode, tangent);
    return WorldFrame2D{p.x, p.z, r.x, r.z, hh.x, hh.z, width, roll, t.x, t.z};
  };
  if (n == 1) {
    const tox::Frame& only = centerline[0];
    return project(only.pos, only.edgeRight, only.h, only.tangent, only.width, only.roll);
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
  return project(a.pos + (b.pos - a.pos) * t, a.edgeRight + (b.edgeRight - a.edgeRight) * t, a.h + (b.h - a.h) * t,
                a.tangent + (b.tangent - a.tangent) * t, a.width + (b.width - a.width) * t, a.roll + (b.roll - a.roll) * t);
}

const ImU32 kStartMarkerColor = IM_COL32(141, 255, 157, 255);  // '#8dff9d'
constexpr float kStartMarkerArrowLength = 22.0f;
constexpr float kStartMarkerHeadLength = 8.0f;
constexpr float kStartMarkerHeadSpreadRad = 0.4f;

// Green arrow at the track's start point, along the driven direction (flipped when track.start.
// reverse is set), with a "START" label. Samples the baked centerline via sampleCenterlineAtG,
// like every other on-canvas frame lookup in this file -- an accepted approximation at
// editor zoom (see this file's header comment above WorldFrame2D).
void drawStartMarker(ImDrawList* drawList, const ImVec2& canvasOrigin, const TopDownView& view, const tox::Track& baked, const Start& start,
                     ProjectionMode mode) {
  if (start.path < 0 || start.path >= static_cast<int>(baked.paths.size())) return;
  const tox::Path& path = baked.paths[start.path];
  if (path.centerline.empty()) return;
  const int n = static_cast<int>(path.centerline.size());
  const double gMax = path.closed ? n : n - 1;
  const WorldFrame2D f = sampleCenterlineAtG(path.centerline, path.closed, static_cast<double>(start.point), gMax, mode);

  double dirX = f.tangentX, dirZ = f.tangentZ;
  if (start.reverse) {
    dirX = -dirX;
    dirZ = -dirZ;
  }
  // Screen-space heading: atan2(x, z) matches worldToScreen's x/z -> screen x/y mapping.
  const double angle = std::atan2(dirX, dirZ);
  const ImVec2 p0 = toAbsolute(canvasOrigin, view.worldToScreen(f.x, f.z));
  const ImVec2 tip(p0.x + static_cast<float>(std::sin(angle)) * kStartMarkerArrowLength, p0.y + static_cast<float>(std::cos(angle)) * kStartMarkerArrowLength);
  drawList->AddLine(p0, tip, kStartMarkerColor, 2.5f);
  const double headAngle = std::atan2(tip.x - p0.x, tip.y - p0.y);
  const ImVec2 left(tip.x - static_cast<float>(std::sin(headAngle - kStartMarkerHeadSpreadRad)) * kStartMarkerHeadLength,
                    tip.y - static_cast<float>(std::cos(headAngle - kStartMarkerHeadSpreadRad)) * kStartMarkerHeadLength);
  const ImVec2 right(tip.x - static_cast<float>(std::sin(headAngle + kStartMarkerHeadSpreadRad)) * kStartMarkerHeadLength,
                     tip.y - static_cast<float>(std::cos(headAngle + kStartMarkerHeadSpreadRad)) * kStartMarkerHeadLength);
  drawList->AddTriangleFilled(tip, left, right, kStartMarkerColor);
  drawList->AddText(ImVec2(p0.x + 10.0f, p0.y - 10.0f), kStartMarkerColor, "START");
}

// Along-curve component of an aux-point drag (see EditorState::dragSelectedAuxTTo's comment
// on letting a drag move `t`, not just the perpendicular value). Estimates the local tangent
// direction and
// arc-length-per-t scale by finite-differencing sampleCenterlineAtG around the point's current t,
// wrapping across the seam for a closed path rather than clamping into it (which would flatten the
// derivative right at t=0/1). Then projects the mouse's world offset from the point's current frame
// onto that tangent and converts it to a new t via the local scale -- an approximation (valid for
// the small, continuously-recomputed-every-frame steps an interactive drag makes), consistent with
// this file's existing "approximate but imperceptible at editor zoom" ethos (see this file's header
// comment above WorldFrame2D).
double dragAuxTAlongTangent(const std::vector<tox::Frame>& centerline, bool closed, double currentT, double worldX, double worldZ,
                            const WorldFrame2D& f, ProjectionMode mode) {
  constexpr double kEps = 1e-4;
  double tMinus = currentT - kEps, tPlus = currentT + kEps;
  if (closed) {
    if (tMinus < 0.0) tMinus += 1.0;
    if (tPlus > 1.0) tPlus -= 1.0;
  } else {
    tMinus = std::clamp(tMinus, 0.0, 1.0);
    tPlus = std::clamp(tPlus, 0.0, 1.0);
  }
  const WorldFrame2D a = sampleCenterlineAtG(centerline, closed, tMinus, 1.0, mode);
  const WorldFrame2D b = sampleCenterlineAtG(centerline, closed, tPlus, 1.0, mode);
  double dt = tPlus - tMinus;
  if (closed && dt <= 0.0) dt += 1.0;
  const double dx = b.x - a.x, dz = b.z - a.z;
  const double length = std::sqrt(dx * dx + dz * dz);
  if (dt <= 0.0 || length < 1e-9) return currentT;
  const double tanX = dx / length, tanZ = dz / length;
  const double dsdt = length / dt;
  const double distTan = (worldX - f.x) * tanX + (worldZ - f.z) * tanZ;
  double newT = currentT + distTan / dsdt;
  if (closed) {
    newT = std::fmod(newT, 1.0);
    if (newT < 0.0) newT += 1.0;
  } else {
    newT = std::clamp(newT, 0.0, 1.0);
  }
  return newT;
}

// Left rail (gLo..gHi at lateral-halfWidth) followed by the reversed right rail, mirroring
// zoneOutlineWorld's `strip.left` then reversed `strip.right` assembly exactly. sample.x/z and
// sample.rightX/rightZ already come out of sampleCenterlineAtG projected into `mode`'s plane, and
// since projection is linear, offsetting by rightX/rightZ here produces exactly the same result as
// projecting a full-3D offset would -- no separate handling needed for Front/Side.
std::vector<WorldPoint2D> pathZoneOutline(const tox::Track& baked, const tox::Zone& zone, ProjectionMode mode) {
  if (zone.hostPathIndex < 0 || zone.hostPathIndex >= static_cast<int>(baked.paths.size())) return {};
  const auto& centerline = baked.paths[zone.hostPathIndex].centerline;
  if (centerline.empty()) return {};
  constexpr int kRows = 8;
  std::vector<WorldPoint2D> left(kRows + 1), right(kRows + 1);
  for (int i = 0; i <= kRows; ++i) {
    const double g = zone.gLo + (zone.gHi - zone.gLo) * (static_cast<double>(i) / kRows);
    const WorldFrame2D sample = sampleCenterlineAtG(centerline, zone.closed, g, zone.gMax, mode);
    left[i] = {sample.x + sample.rightX * (zone.lateral - zone.halfWidth), sample.z + sample.rightZ * (zone.lateral - zone.halfWidth)};
    right[i] = {sample.x + sample.rightX * (zone.lateral + zone.halfWidth), sample.z + sample.rightZ * (zone.lateral + zone.halfWidth)};
  }
  std::vector<WorldPoint2D> outline = std::move(left);
  outline.reserve(outline.size() + right.size());
  for (int i = kRows; i >= 0; --i) outline.push_back(right[i]);
  return outline;
}

std::vector<WorldPoint2D> zoneOutlineWorld(const tox::Track& baked, const tox::Zone& zone, ProjectionMode mode) {
  return pathZoneOutline(baked, zone, mode);
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
               const std::optional<std::string>& selectedZoneId, ProjectionMode mode) {
  for (const auto& zone : baked.zones) {
    const std::vector<WorldPoint2D> outline = zoneOutlineWorld(baked, zone, mode);
    if (outline.size() < 3) continue;
    std::vector<ImVec2> screen;
    screen.reserve(outline.size());
    for (const auto& p : outline) screen.push_back(toAbsolute(canvasOrigin, view.worldToScreen(p.x, p.z)));
    const bool isStartGrid = zone.effect == "startGrid";
    const bool isJump = zone.effect == "jump";
    const ImU32 fillColor = isStartGrid ? kZoneStartGridFillColor : isJump ? kZoneJumpFillColor : kZoneBoostFillColor;
    const ImU32 strokeColor = isStartGrid ? kZoneStartGridStrokeColor : isJump ? kZoneJumpStrokeColor : kZoneBoostStrokeColor;
    drawList->AddConcavePolyFilled(screen.data(), static_cast<int>(screen.size()), fillColor);
    const bool isSelected = selectedZoneId.has_value() && *selectedZoneId == zone.id;
    drawList->AddPolyline(screen.data(), static_cast<int>(screen.size()), isSelected ? kZoneSelectedStrokeColor : strokeColor,
                          ImDrawFlags_Closed, isSelected ? 3.0f : 1.5f);
  }
}

// Topmost zone under a world point, mirroring zoneAtTop's reverse iteration (later-added zones
// draw on top).
const tox::Zone* zoneAtWorld(const tox::Track* baked, double worldX, double worldZ, ProjectionMode mode) {
  if (baked == nullptr) return nullptr;
  for (auto it = baked->zones.rbegin(); it != baked->zones.rend(); ++it) {
    const std::vector<WorldPoint2D> outline = zoneOutlineWorld(*baked, *it, mode);
    if (outline.size() >= 3 && pointInWorldPolygon(outline, worldX, worldZ)) return &*it;
  }
  return nullptr;
}

// ---- Drivable mesh object placements (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 5.2) ------------
//
// Drawn/hit-tested directly against the AUTHORED `TrackDefinition::meshObjects` list, not `baked`
// -- unlike paths/zones/triggers, core never compiles a placement into anything (see the plan's
// "`.mppmodel` loading is host-only" architecture note), so there's no baked record to read, and
// editing still works even when the track currently fails to bake. There's also no bounding-box or
// real geometry to draw (the editor never loads the referenced `.mppmodel` either) -- just a fixed-
// size diamond marker plus a short line showing which way it currently faces, projected through the
// active ProjectionMode like every other on-canvas entity.
constexpr float kMeshObjectMarkerRadiusPx = 9.0f;
constexpr float kMeshObjectFacingLengthPx = 16.0f;

// TopDown -> yaw (rotation.x), Front -> pitch (rotation.y), Side -> roll (rotation.z) -- read-only
// counterpart to EditorState's own dragSelectedMeshObjectRotationTo, which writes via the same
// mode-to-axis mapping (duplicated there rather than shared, since that one also owns the
// push-history-once-per-gesture bookkeeping this free function has no business doing).
double meshObjectRotationDeg(ProjectionMode mode, const ModelPlacement& placement) {
  switch (mode) {
    case ProjectionMode::Front: return placement.rotation.y;
    case ProjectionMode::Side: return placement.rotation.z;
    case ProjectionMode::TopDown:
    default: return placement.rotation.x;
  }
}

// Scale-then-rotate(yaw about Y, then pitch about X, then roll about Z)-then-translate, mirroring
// cpp/core/src/TrackBake.cpp's and cpp/tungsten-monoxide/src/Map.cpp's own placementTransformPosition
// exactly (same convention, reimplemented independently here too -- editor/core/host don't share
// this kind of file-local helper, per this codebase's established convention).
tox::Vec3 placementTransformPosition(const ModelPlacement& placement, const tox::Vec3& local) {
  constexpr double kDegToRad = std::numbers::pi / 180.0;
  tox::Vec3 scaled(local.x * placement.scale.x, local.y * placement.scale.y, local.z * placement.scale.z);
  tox::Vec3 rotated = tox::applyAxisAngle(scaled, tox::Vec3(0.0, 1.0, 0.0), placement.rotation.x * kDegToRad);
  rotated = tox::applyAxisAngle(rotated, tox::Vec3(1.0, 0.0, 0.0), placement.rotation.y * kDegToRad);
  rotated = tox::applyAxisAngle(rotated, tox::Vec3(0.0, 0.0, 1.0), placement.rotation.z * kDegToRad);
  return rotated + placement.position;
}

// A placement's modelId names an embedded <Model id> in track.models (TRACK_MODEL_LIST_PLAN.md
// Milestone 6), not a raw path -- resolve it to that Model's own <ModelFile> reference. Returns
// nullptr if the id doesn't match any embedded Model (a stale/hand-edited reference).
const std::string* resolveModelFileReference(const TrackDefinition& track, const std::string& modelId) {
  for (const auto& model : track.models)
    if (model.id == modelId) return &model.modelFile;
  return nullptr;
}

// Loads (and caches for the process's lifetime -- no on-disk-change invalidation, an accepted
// limitation matching this codebase's "no triangle budget set now, measure against real content
// once it exists" posture for placement rendering elsewhere) a .mppmodel's geometry, resolved as a
// plain relative path against `baseDir` (mirrors main.cpp's own save-location-relative resolution
// convention). `modelFileReference` is the embedded Model's own <ModelFile> text (see
// resolveModelFileReference above), not the placement's modelId. Returns nullptr on any failure
// (missing file, bad format, no baseDir yet) -- best-effort, a placement always still renders its
// marker either way (see drawMeshObjectPlacements below).
const ImportedMppModel* loadCachedPlacementGeometry(const std::string& modelFileReference, const std::filesystem::path& baseDir) {
  static std::unordered_map<std::string, std::optional<ImportedMppModel>> cache;
  if (baseDir.empty() || modelFileReference.empty()) return nullptr;
  const std::string key = (baseDir / modelFileReference).lexically_normal().string();
  const auto found = cache.find(key);
  if (found != cache.end()) return found->second.has_value() ? &*found->second : nullptr;
  std::optional<ImportedMppModel> loaded;
  try {
    loaded = readMppModelGeometry(key);
  } catch (const std::exception&) {
    loaded.reset();
  }
  auto [it, inserted] = cache.emplace(key, std::move(loaded));
  (void)inserted;
  return it->second.has_value() ? &*it->second : nullptr;
}

void drawMeshObjectPlacements(ImDrawList* drawList, const ImVec2& canvasOrigin, const TopDownView& view, const TrackDefinition& track,
                              const std::optional<std::string>& selectedMeshObjectId, ProjectionMode mode,
                              const std::filesystem::path& modelBaseDir) {
  for (const auto& placement : track.meshObjects) {
    const bool isSelected = selectedMeshObjectId.has_value() && *selectedMeshObjectId == placement.id;
    const ImU32 outlineColor = isSelected ? kMeshSelectedOutlineColor : kMeshOutlineColor;

    // Real geometry (TRACK_MODEL_LIST_PLAN.md Milestone 4.2), drawn under the marker: every
    // triangle of every mesh, transformed by the placement's 6-DOF transform and projected through
    // the active plane -- same flat-fill convention every other filled shape on this canvas already
    // uses (drawBakedPath/drawZones/etc.), not real lit 3D rendering.
    if (const std::string* modelFileReference = resolveModelFileReference(track, placement.modelId)) {
      if (const ImportedMppModel* geometry = loadCachedPlacementGeometry(*modelFileReference, modelBaseDir)) {
        for (const ImportedMppMesh& mesh : geometry->meshes) {
          for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const ImVec2 a = toAbsolute(canvasOrigin, worldToScreenPlane(view, mode, placementTransformPosition(placement, mesh.vertices[mesh.indices[i]].position)));
            const ImVec2 b = toAbsolute(canvasOrigin, worldToScreenPlane(view, mode, placementTransformPosition(placement, mesh.vertices[mesh.indices[i + 1]].position)));
            const ImVec2 c = toAbsolute(canvasOrigin, worldToScreenPlane(view, mode, placementTransformPosition(placement, mesh.vertices[mesh.indices[i + 2]].position)));
            drawList->AddTriangleFilled(a, b, c, isSelected ? kMeshSelectedOutlineColor : kMeshFillColor);
          }
        }
      }
    }

    const ImVec2 center = toAbsolute(canvasOrigin, worldToScreenPlane(view, mode, placement.position));
    const ImVec2 diamond[4] = {ImVec2(center.x, center.y - kMeshObjectMarkerRadiusPx), ImVec2(center.x + kMeshObjectMarkerRadiusPx, center.y),
                               ImVec2(center.x, center.y + kMeshObjectMarkerRadiusPx), ImVec2(center.x - kMeshObjectMarkerRadiusPx, center.y)};
    drawList->AddConvexPolyFilled(diamond, 4, kMeshFillColor);
    drawList->AddPolyline(diamond, 4, outlineColor, ImDrawFlags_Closed, isSelected ? 3.0f : 1.5f);
    const double facingRad = meshObjectRotationDeg(mode, placement) * std::numbers::pi / 180.0;
    const ImVec2 facingEnd(center.x + static_cast<float>(std::cos(facingRad)) * kMeshObjectFacingLengthPx,
                           center.y + static_cast<float>(std::sin(facingRad)) * kMeshObjectFacingLengthPx);
    drawList->AddLine(center, facingEnd, outlineColor, 2.0f);
  }
}

// Nearest placement marker to a world point within `tolWorld`, topmost (later-added) wins on a tie
// -- mirrors zoneAtWorld/triggerAtWorld's own reverse-iteration convention.
const ModelPlacement* meshObjectAtWorld(const TrackDefinition& track, double worldX, double worldZ, double tolWorld,
                                                     ProjectionMode mode) {
  const ModelPlacement* best = nullptr;
  double bestDistSq = tolWorld * tolWorld;
  for (auto it = track.meshObjects.rbegin(); it != track.meshObjects.rend(); ++it) {
    const WorldPoint2D p = planeCoords(mode, it->position);
    const double dx = worldX - p.x, dz = worldZ - p.z;
    const double distSq = dx * dx + dz * dz;
    if (distSq <= bestDistSq) {
      bestDistSq = distSq;
      best = &*it;
    }
  }
  return best;
}

// ---- Add-point context menu -------------------------------------------------------------------

struct NearestPathPlacement {
  int pathIndex{-1};
  double t{0.0}, lateral{0.0};
  const tox::Frame* frame{nullptr};
};

// Nearest centerline sample across every
// path, plus the lateral offset from it -- used to place a zone/trigger/aux point at a right-click
// world position. Approximated off the baked centerline's own discrete samples rather than a
// fine-grained live spline evaluator (same tradeoff already accepted for zone/trigger outlines --
// no evaluator is exposed to cpp/editor).
std::optional<NearestPathPlacement> nearestPathPlacement(const tox::Track* baked, double worldX, double worldZ, ProjectionMode mode) {
  if (baked == nullptr) return std::nullopt;
  int bestPath = -1, bestIndex = -1;
  double bestDistSq = std::numeric_limits<double>::infinity();
  for (int pi = 0; pi < static_cast<int>(baked->paths.size()); ++pi) {
    const auto& centerline = baked->paths[pi].centerline;
    for (int i = 0; i < static_cast<int>(centerline.size()); ++i) {
      const WorldPoint2D p = planeCoords(mode, centerline[i].pos);
      const double dx = p.x - worldX, dz = p.z - worldZ;
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
  // Lateral offset computed entirely within the active plane -- pos/edgeRight both projected via
  // planeCoords first, same "linear projection commutes with the surrounding math" reasoning as
  // sampleCenterlineAtG's own header comment.
  const WorldPoint2D framePos = planeCoords(mode, frame.pos);
  const WorldPoint2D frameRight = planeCoords(mode, frame.edgeRight);
  const double lateral = (worldX - framePos.x) * frameRight.x + (worldZ - framePos.z) * frameRight.z;
  return NearestPathPlacement{bestPath, t, lateral, &frame};
}

// ---- Physics-sample overlay ----------------------------------
//
// A read-only debug overlay showing the baked centerline frames physics actually reads: one dot
// per tox::Path::centerline entry, selectable for inspection only (no drag). Shows the track's
// *actual* baked centerline (core's own adaptive-by-length sampling, see CLAUDE.md's "Physics
// core") rather than some separately-forced fixed sample count -- the true physics samples the
// current native bake produced, which is more useful for a physics-debug overlay, and needs no
// extra bake path.

// Nearest baked centerline frame (across all paths) to a world point, within `pickRadiusWorld`.
std::optional<TopDownView::PhysicsSampleRef> physicsPointAtWorld(const tox::Track* baked, double worldX, double worldZ,
                                                                 double pickRadiusWorld, ProjectionMode mode) {
  if (baked == nullptr) return std::nullopt;
  std::optional<TopDownView::PhysicsSampleRef> best;
  double bestDistSq = pickRadiusWorld * pickRadiusWorld;
  for (int pi = 0; pi < static_cast<int>(baked->paths.size()); ++pi) {
    const auto& centerline = baked->paths[pi].centerline;
    for (int i = 0; i < static_cast<int>(centerline.size()); ++i) {
      const WorldPoint2D p = planeCoords(mode, centerline[i].pos);
      const double dx = p.x - worldX, dz = p.z - worldZ;
      const double distSq = dx * dx + dz * dz;
      if (distSq < bestDistSq) {
        bestDistSq = distSq;
        best = TopDownView::PhysicsSampleRef{pi, i};
      }
    }
  }
  return best;
}

void drawPhysicsPoints(ImDrawList* drawList, const ImVec2& canvasOrigin, const TopDownView& view, const tox::Track& baked, ProjectionMode mode) {
  const auto& sel = view.physicsSelection();
  for (int pi = 0; pi < static_cast<int>(baked.paths.size()); ++pi) {
    const auto& centerline = baked.paths[pi].centerline;
    for (int i = 0; i < static_cast<int>(centerline.size()); ++i) {
      const bool isSelected = sel.has_value() && sel->pathIndex == pi && sel->frameIndex == i;
      const ImVec2 screen = toAbsolute(canvasOrigin, worldToScreenPlane(view, mode, centerline[i].pos));
      drawList->AddCircleFilled(screen, isSelected ? 5.0f : 2.2f, isSelected ? kPhysicsSelectedColor : kPhysicsPointColor);
      if (isSelected) drawList->AddCircle(screen, 5.0f, IM_COL32(255, 255, 255, 255), 0, 2.0f);
    }
  }
}

// ---- Self-intersection crossing markers ------------------------------------------------------
//
// `baked->selfIntersections` is the core-side detection result (TrackBake.cpp's removeSelfLoops,
// an unbounded full pairwise scan on the pre-collapse edges) --
// already computed once per non-dragging bake by main.cpp's rebake(), not re-derived here. This
// file only re-derives, per crossing, the effective forced/auto STATE from the current override
// list, so cycling an override never needs a re-detection pass.
CrossingState crossingStateFor(const tox::SelfIntersection& crossing, const std::vector<SelfIntersectionOverride>& overrides) {
  const auto it = std::find_if(overrides.begin(), overrides.end(), [&](const SelfIntersectionOverride& o) {
    return o.side == crossing.side && ((o.a == crossing.a && o.b == crossing.b) || (o.a == crossing.b && o.b == crossing.a));
  });
  if (it != overrides.end()) return it->action == "collapse" ? CrossingState::ForcedCollapse : CrossingState::ForcedKeep;
  return crossing.span <= tox::TrackCore::DEFAULT_SELF_INTERSECTION_SPAN ? CrossingState::AutoCollapse : CrossingState::AutoKeep;
}

ImU32 crossingColor(CrossingState state) {
  switch (state) {
    case CrossingState::AutoCollapse: return kCrossingAutoCollapseColor;
    case CrossingState::AutoKeep: return kCrossingAutoKeepColor;
    case CrossingState::ForcedCollapse: return kCrossingForcedCollapseColor;
    case CrossingState::ForcedKeep: return kCrossingForcedKeepColor;
  }
  return kCrossingAutoCollapseColor;
}

bool crossingIsCollapsed(CrossingState state) { return state == CrossingState::AutoCollapse || state == CrossingState::ForcedCollapse; }

// Nearest crossing marker to a LOCAL (canvas-relative) screen point, within kCrossingHitRadiusPx,
// or nullptr -- a linear search over the crossing cache. Returns a pointer into
// `baked->selfIntersections` (stable for the
// lifetime of this frame's bake) rather than a copy, since the caller only needs side/a/b.
const tox::SelfIntersection* crossingAtLocal(const tox::Track* baked, const TopDownView& view, const ImVec2& mouseLocal, ProjectionMode mode) {
  if (baked == nullptr) return nullptr;
  const tox::SelfIntersection* best = nullptr;
  float bestDistSq = kCrossingHitRadiusPx * kCrossingHitRadiusPx;
  for (const auto& crossing : baked->selfIntersections) {
    const ScreenPoint2D s = worldToScreenPlane(view, mode, crossing.point);
    const float dx = mouseLocal.x - static_cast<float>(s.x), dy = mouseLocal.y - static_cast<float>(s.y);
    const float distSq = dx * dx + dy * dy;
    if (distSq <= bestDistSq) {
      bestDistSq = distSq;
      best = &crossing;
    }
  }
  return best;
}

// One dot per detected crossing, colored by effective state; filled disc = collapsed, hollow ring
// (with a small center pip) = kept, including the dark contrast halo so markers stay visible over
// any ribbon/centerline color.
void drawCrossings(ImDrawList* drawList, const ImVec2& canvasOrigin, const TopDownView& view, const tox::Track& baked,
                   const std::vector<SelfIntersectionOverride>& overrides, ProjectionMode mode) {
  for (const auto& crossing : baked.selfIntersections) {
    const CrossingState state = crossingStateFor(crossing, overrides);
    const bool collapsed = crossingIsCollapsed(state);
    const ImU32 color = crossingColor(state);
    const ImVec2 screen = toAbsolute(canvasOrigin, worldToScreenPlane(view, mode, crossing.point));
    drawList->AddCircleFilled(screen, 9.0f, kCrossingHaloColor);
    if (collapsed) drawList->AddCircleFilled(screen, 6.5f, color);
    drawList->AddCircle(screen, 6.5f, color, 0, 2.5f);
    if (!collapsed) drawList->AddCircleFilled(screen, 1.6f, color);
  }
}

// ---- Triggers ----------------------------------------------------------------------------------
//
// Unlike zones, core bakes a trigger directly into a complete world-space gate frame
// (tox::Trigger::center/right/up/fwd, halfWidth, height), so this reuses it verbatim rather than
// re-deriving anything client-side.

ImU32 triggerColor(const tox::Trigger& trigger) {
  if (trigger.type != "checkpoint") return kTriggerDummyColor;
  return trigger.role == "finish" ? kTriggerFinishColor : kTriggerCheckpointColor;
}

void drawTriggers(ImDrawList* drawList, const ImVec2& canvasOrigin, const TopDownView& view, const tox::Track& baked,
                  const std::optional<std::string>& selectedTriggerId, const std::optional<std::string>& hoveredTriggerId,
                  ProjectionMode mode) {
  for (const auto& trigger : baked.triggers) {
    const ImVec2 a = toAbsolute(canvasOrigin, worldToScreenPlane(view, mode, trigger.center - trigger.right * trigger.halfWidth));
    const ImVec2 b = toAbsolute(canvasOrigin, worldToScreenPlane(view, mode, trigger.center + trigger.right * trigger.halfWidth));
    const bool isSelected = selectedTriggerId.has_value() && *selectedTriggerId == trigger.id;
    const bool isHovered = hoveredTriggerId.has_value() && *hoveredTriggerId == trigger.id;
    const ImU32 color = triggerColor(trigger);
    drawList->AddLine(a, b, color, isSelected ? 4.0f : 2.5f);

    const ImVec2 center = toAbsolute(canvasOrigin, worldToScreenPlane(view, mode, trigger.center));
    constexpr float kArrowLen = 14.0f;
    const WorldPoint2D fwdPlane = planeCoords(mode, trigger.fwd);
    auto drawArrow = [&](double sign) {
      const double dx = fwdPlane.x * sign, dz = fwdPlane.z * sign;
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

    // Center drag handle (new functionality -- lets a selected trigger be dragged along its host
    // path to change host.t; previously host.t was panel-edited only). White-filled with the
    // trigger's own color as a stroke, matching the roll/width/cross-section aux-handle convention
    // (kAuxHandleFillColor) so it reads as "a draggable handle", distinct from the endpoint
    // markers below (which fill WITH the trigger's color instead).
    constexpr float kCenterHandleRadius = 5.0f;
    drawList->AddCircleFilled(center, kCenterHandleRadius, kAuxHandleFillColor);
    drawList->AddCircle(center, kCenterHandleRadius, color, 0, 2.0f);
    if (isSelected) drawList->AddCircle(center, kCenterHandleRadius, kSelectedOutlineColor, 0, 1.5f);
    if (isHovered) drawList->AddCircle(center, kCenterHandleRadius + 3.0f, kHoverRingColor, 0, 2.0f);

    const float endpointRadius = isSelected ? 4.0f : 3.0f;
    drawList->AddCircleFilled(a, endpointRadius, color);
    drawList->AddCircleFilled(b, endpointRadius, color);
    // Selected: a crisp white border right at the endpoint fill's edge -- the same "handle" look
    // AND ring layering drawAuthoredPositionPoints uses, so hover and selection read as distinct
    // states here too (rather than both just changing line/point weight the way selection alone
    // used to).
    if (isSelected) {
      drawList->AddCircle(a, endpointRadius, kSelectedOutlineColor, 0, 1.5f);
      drawList->AddCircle(b, endpointRadius, kSelectedOutlineColor, 0, 1.5f);
    }
    // Hovered: a separate, softer ring further out, so it never gets confused with the tighter
    // selection border above even when both apply to the same trigger.
    if (isHovered) {
      drawList->AddCircle(a, endpointRadius + 3.0f, kHoverRingColor, 0, 2.0f);
      drawList->AddCircle(b, endpointRadius + 3.0f, kHoverRingColor, 0, 2.0f);
    }
  }
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
const tox::Trigger* triggerAtWorld(const tox::Track* baked, double worldX, double worldZ, double tolWorld, ProjectionMode mode) {
  if (baked == nullptr) return nullptr;
  for (auto it = baked->triggers.rbegin(); it != baked->triggers.rend(); ++it) {
    const WorldPoint2D a = planeCoords(mode, it->center - it->right * it->halfWidth);
    const WorldPoint2D b = planeCoords(mode, it->center + it->right * it->halfWidth);
    if (std::sqrt(distanceSqToSegment(worldX, worldZ, a.x, a.z, b.x, b.z)) <= tolWorld) return &*it;
  }
  return nullptr;
}

// meshEdgeAtWorld/drawMeshRails (Rails-mode edge picking/highlighting) were removed along with
// MeshPlacement/MeshAsset and EditMode::Rails (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 2).

// ---- Roll/width/cross-section on-canvas handles ------------------------------------------------
//
// Restricted to only the CURRENTLY-SELECTED path's points, to avoid cluttering
// every other path's curve with handles. Positions come from sampleCenterlineAtG() (smooth
// interpolation over the baked centerline), a "close enough, no evaluator exposed" tradeoff already
// used for zone outlines above -- the visual difference is imperceptible at editor zoom levels.
// Still no on-canvas DRAG (values are edited via PropertiesPanel.cpp only).

// Right-lean (negative) -> cyan, left-lean (positive) -> magenta-ish.
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
                   const tox::Track* baked, int currentPathIndex, const SelectedPoint& selection, ProjectionMode mode) {
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

    const WorldFrame2D f = sampleCenterlineAtG(centerline, path.closed, point.t, 1.0, mode);
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
// `pickRadiusPx`, on the current path -- checked in a fixed priority order (cross-section, width,
// roll), all three before a position-point hit.
std::optional<SelectedPoint> auxHandleAtLocal(const TrackDefinition& track, const tox::Track* baked, int currentPathIndex,
                                              const TopDownView& view, const ImVec2& mouseLocal, float pickRadiusPx, ProjectionMode mode) {
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
      const WorldFrame2D f = sampleCenterlineAtG(centerline, path.closed, point.t, 1.0, mode);
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
                                const SelectedPoint& selection, const std::optional<SelectedPoint>& hovered,
                                const std::optional<EditorState::OpenEndpointRef>& weldTarget,
                                const std::vector<Connection>& disjointSeams, ProjectionMode projectionMode) {
  for (int pi = 0; pi < static_cast<int>(track.paths.size()); ++pi) {
    const Path& path = track.paths[pi];
    const auto& points = path.points;
    // First/last raw indices among this path's Position points -- needed both for the weld-target
    // check below (a position-space concept, see EditorState::OpenEndpointRef) and for `isEndpoint`,
    // computed over the control-point-only sequence.
    int firstPosRaw = -1, lastPosRaw = -1;
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
      if (points[i].kind != PointKind::Position) continue;
      if (firstPosRaw < 0) firstPosRaw = i;
      lastPosRaw = i;
    }
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
      if (points[i].kind != PointKind::Position) continue;
      const bool isSelected = selection.pathIndex == pi && selection.pointIndex == i;
      const bool isHovered = hovered.has_value() && hovered->pathIndex == pi && hovered->pointIndex == i;
      const bool isWeldTarget =
          weldTarget.has_value() && weldTarget->pathIndex == pi && i == (weldTarget->atEnd ? lastPosRaw : firstPosRaw);
      const bool isEndpoint = !path.closed && (i == firstPosRaw || i == lastPosRaw);
      const bool isDisjoint = std::find_if(disjointSeams.begin(), disjointSeams.end(),
                                           [&](const Connection& s) { return s.pointId == points[i].id; }) != disjointSeams.end();
      const WorldPoint2D plane = planeCoords(projectionMode, points[i].pos);
      const ImVec2 screen = toAbsolute(canvasOrigin, view.worldToScreen(plane.x, plane.z));
      const float radius = isSelected ? kPointRadius + 2.0f : kPointRadius;
      // Square = open-path endpoint (join-eligible), circle otherwise.
      if (isEndpoint) {
        drawList->AddRectFilled(ImVec2(screen.x - radius, screen.y - radius), ImVec2(screen.x + radius, screen.y + radius),
                                isSelected ? kSelectedPointColor : heightColor(points[i].pos.y));
      } else {
        drawList->AddCircleFilled(screen, radius, isSelected ? kSelectedPointColor : heightColor(points[i].pos.y));
      }
      // Selected: a crisp white border right at the (already-larger) fill's edge -- a "handle"
      // look that reads as selected regardless of hover state or background. Disjoint seams get a
      // thicker amber stroke instead when not selected.
      const ImU32 strokeColor = isSelected ? kSelectedOutlineColor : (isDisjoint ? kDisjointColor : IM_COL32(0, 0, 0, 153));
      if (isEndpoint) {
        drawList->AddRect(ImVec2(screen.x - radius, screen.y - radius), ImVec2(screen.x + radius, screen.y + radius), strokeColor, 0.0f, 0,
                          isSelected || isDisjoint ? 3.0f : 1.5f);
      } else {
        drawList->AddCircle(screen, radius, strokeColor, 0, isSelected || isDisjoint ? 3.0f : 1.5f);
      }
      // Hovered: a separate, softer ring further out, so it never gets confused with the tighter
      // selection border above even when both apply to the same point.
      if (isHovered) drawList->AddCircle(screen, radius + 3.0f, kHoverRingColor, 0, 2.0f);
      // Drag-to-weld target: a wider green ring still, so it's unmistakable even though the
      // dragged point itself is what's under the cursor, not this one.
      if (isWeldTarget) drawList->AddCircle(screen, radius + 6.0f, kWeldTargetColor, 0, kWeldTargetRingThickness);
      // Disjoint X cross, on top of everything else, sized off the same radius+4 offset.
      if (isDisjoint) {
        drawList->AddLine(ImVec2(screen.x - radius - 4.0f, screen.y - radius - 4.0f), ImVec2(screen.x + radius + 4.0f, screen.y + radius + 4.0f),
                          kDisjointColor, 2.0f);
        drawList->AddLine(ImVec2(screen.x + radius + 4.0f, screen.y - radius - 4.0f), ImVec2(screen.x - radius - 4.0f, screen.y + radius + 4.0f),
                          kDisjointColor, 2.0f);
      }
      // Index + elevation label: "<path>.<index> (y<elevation>)".
      char label[32];
      std::snprintf(label, sizeof(label), "%d.%d (y%d)", pi, i, static_cast<int>(std::lround(points[i].pos.y)));
      drawList->AddText(ImVec2(screen.x + 9.0f, screen.y - 5.0f), kPositionLabelColor, label);
    }
  }
}

void drawCreateDraft(ImDrawList* drawList, const ImVec2& canvasOrigin, const TopDownView& view, const std::vector<tox::Vec3>& draft,
                     ProjectionMode mode) {
  if (draft.empty()) return;
  std::vector<ImVec2> screen;
  screen.reserve(draft.size());
  for (const auto& p : draft) screen.push_back(toAbsolute(canvasOrigin, worldToScreenPlane(view, mode, p)));
  if (screen.size() > 1) drawList->AddPolyline(screen.data(), static_cast<int>(screen.size()), kCreateDraftColor, ImDrawFlags_None, 2.0f);
  for (const auto& s : screen) drawList->AddCircleFilled(s, kPointRadius, kCreateDraftColor);
}

// Nearest path (by baked centerline segment distance) to a world point, tolerant of the ribbon's
// own half-width (plus a small screen-derived pick margin at the edges) so a click anywhere on the
// visibly-drawn road counts as clicking that curve, not just its thin centerline. Picked last, after
// every other clickable thing (points/zones/triggers), since the ribbon is the largest,
// least-specific target on the canvas.
std::optional<int> pathAtWorld(const tox::Track* baked, double worldX, double worldZ, double edgeTolWorld, ProjectionMode mode) {
  if (baked == nullptr) return std::nullopt;
  std::optional<int> best;
  double bestDistSq = std::numeric_limits<double>::infinity();
  for (std::size_t i = 0; i < baked->paths.size(); ++i) {
    const tox::Path& path = baked->paths[i];
    const std::size_t n = path.centerline.size();
    if (n < 2) continue;
    const std::size_t segmentCount = path.closed ? n : n - 1;
    for (std::size_t s = 0; s < segmentCount; ++s) {
      const std::size_t t = (s + 1) % n;
      const tox::Frame& fa = path.centerline[s];
      const tox::Frame& fb = path.centerline[t];
      const WorldPoint2D pa = planeCoords(mode, fa.pos);
      const WorldPoint2D pb = planeCoords(mode, fb.pos);
      const double distSq = distanceSqToSegment(worldX, worldZ, pa.x, pa.z, pb.x, pb.z);
      const double tol = std::max(fa.halfW, fb.halfW) + edgeTolWorld;
      if (distSq <= tol * tol && distSq < bestDistSq) {
        bestDistSq = distSq;
        best = static_cast<int>(i);
      }
    }
  }
  return best;
}

double angleFromOriginDeg(double originX, double originZ, double worldX, double worldZ) {
  return std::atan2(worldZ - originZ, worldX - originX) * 180.0 / std::numbers::pi;
}

// Shift+drag-to-rotate angle, generalized per ProjectionMode (DRIVABLE_MESH_OBJECTS_PLAN.md
// Milestone 1.3): TopDown -> yaw (atan2(dz, dx), matching today's mesh-region gesture's
// convention), Front -> pitch (atan2(dy, dx)), Side -> roll (atan2(dz, dy)). Built on the same
// planeCoords() projection drag-to-move (1.2) already uses, so all three axes share one convention:
// the two plane coordinates feed the same atan2 math regardless of which world axes they are.
double rotateAngleDeg(ProjectionMode mode, const tox::Vec3& origin, const tox::Vec3& worldPos) {
  const WorldPoint2D o = planeCoords(mode, origin);
  const WorldPoint2D w = planeCoords(mode, worldPos);
  return angleFromOriginDeg(o.x, o.z, w.x, w.z);
}

// Live state for the shift-drag rubber-band gesture. `from` has a value for the entire
// gesture once armed; `target` is whichever OTHER open endpoint the cursor currently rests on, if
// any; `currentLocal` is the cursor's canvas-local position, used to draw the free end of the line
// when not snapped to a target.
struct JoinDragPreview {
  std::optional<EditorState::OpenEndpointRef> from;
  std::optional<EditorState::OpenEndpointRef> target;
  ImVec2 currentLocal{0.0f, 0.0f};
};

// Left click/drag: hit-test + select + move a position point or mesh placement (Edit mode only).
// Shift+drag on a mesh rotates it
// about its own placement origin instead of moving it. Returns true if the track was mutated this
// frame. Freezes the view's auto-fit bounds for the duration of any drag (see
// TopDownView::freezeBounds) so moving/rotating something doesn't fight the camera auto-fitting
// around it.
bool handleEditModeInput(EditorState& state, TopDownView& view, const tox::Track* baked, const TrackBounds2D& preDragBounds,
                         const ImVec2& mouseLocal, double pickRadiusWorld, bool hovered, bool itemActive,
                         std::optional<EditorState::OpenEndpointRef>& outWeldTarget, JoinDragPreview& outJoinDrag) {
  const ProjectionMode mode = state.projectionMode();
  bool mutated = false;
  outWeldTarget = std::nullopt;
  outJoinDrag = JoinDragPreview{};
  // Decided once per gesture, at the mousedown that starts it, rather than re-deriving "what to
  // drag" from whatever happens to be selected on every
  // subsequent frame (which would let a stale selection from an earlier, unrelated click hijack a
  // later pan drag). Left dragging any area that did not select a draggable object falls through
  // to a plain pan.
  static bool panDragActive = false;
  // Live only during an open-endpoint drag (see selectedOpenEndpointEnd()'s comment); persists
  // across frames (unlike outWeldTarget, which is reset every call) so the release branch below --
  // itself a separate `else if` that fires on a frame where draggingGesture may already be false --
  // still knows what was last hovered when the mouse actually went up.
  static std::optional<EditorState::OpenEndpointRef> weldTarget;
  // Set once per gesture (see the position-drag branch below): the endpoint the dragged point was
  // ALREADY resting on when this drag started, if any -- excluded from weldTarget candidates for
  // the rest of the gesture so merely being selected while coincident never re-triggers a weld.
  static std::optional<EditorState::OpenEndpointRef> weldExcludeTarget;
  // Shift-drag rubber-band gesture: a SEPARATE gesture from the
  // plain drag-to-weld above -- it never relocates the dragged point itself, only
  // previews a connection or, on release into empty space, appends a new point. `joinDragFrom`
  // persists across frames like weldTarget above (the release check runs on a frame where
  // draggingGesture may already be false); `joinDragStartLocal` is the mousedown position, used
  // only to measure the release-time drag distance against kJoinDragMinPx.
  static std::optional<EditorState::OpenEndpointRef> joinDragFrom;
  static std::optional<EditorState::OpenEndpointRef> joinDragTarget;
  static ImVec2 joinDragStartLocal;
  const std::optional<EditorState::OpenEndpointRef> shiftClickEndpointHit =
      (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::GetIO().KeyShift)
          ? [&] {
              const WorldPoint2D world = view.screenToWorld(mouseLocal.x, mouseLocal.y);
              // excludePathIndex -1 finds the globally nearest open endpoint (nothing legitimately
              // has that index, so nothing is excluded) -- there's no "self" yet since no gesture
              // has started.
              return state.hitTestOpenEndpoint(world.x, world.z, pickRadiusWorld, -1, false);
            }()
          : std::nullopt;
  if (shiftClickEndpointHit.has_value()) {
    // Shift-clicking an open endpoint starts the rubber-band gesture instead of the normal
    // select-and-drag handling below -- shift plus an endpoint hit is checked BEFORE falling
    // through to the plain-click branch.
    joinDragFrom = shiftClickEndpointHit;
    joinDragTarget.reset();
    joinDragStartLocal = mouseLocal;
  } else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    // Roll/width/cross-section handles win over a position-point
    // hit, checked in priority order (crossSection, then width, then roll, all
    // before a position-point check).
    const auto auxHit = auxHandleAtLocal(state.track(), baked, state.currentPathIndex(), view, mouseLocal, kPickRadiusPx, mode);
    const WorldPoint2D world = view.screenToWorld(mouseLocal.x, mouseLocal.y);
    // Self-intersection crossing markers: a click cycles the
    // crossing's override instead of selecting/dragging anything -- checked before a position-point
    // hit, and skipped entirely while shift is held, since shift is reserved for the
    // rubber-band gesture above.
    const tox::SelfIntersection* crossingHit =
        !ImGui::GetIO().KeyShift ? crossingAtLocal(baked, view, mouseLocal, mode) : nullptr;
    if (auxHit.has_value()) {
      state.selectPoint(auxHit->pathIndex, auxHit->pointIndex);
      view.clearPhysicsSelection();
    } else if (crossingHit != nullptr) {
      state.cycleCrossingOverride(crossingHit->side, crossingHit->a, crossingHit->b);
      mutated = true;
    } else if (view.showPositionPoints() && state.selectPositionAt(world.x, world.z, pickRadiusWorld)) {
      view.clearPhysicsSelection();
    } else {
      // Physics sample points (debug overlay): picked after
      // authored control points so editing is never obstructed, but before zones/triggers/mesh
      // regions. Read-only: selecting one just shows its baked values, no drag.
      const auto physicsHit =
          view.showPhysicsPoints() ? physicsPointAtWorld(baked, world.x, world.z, kPickRadiusPx / view.scale(), mode) : std::nullopt;
      if (physicsHit.has_value()) {
        view.selectPhysicsSample(physicsHit->pathIndex, physicsHit->frameIndex);
        state.clearSelection();
        state.clearZoneSelection();
        state.clearTriggerSelection();
        state.clearMeshObjectSelection();
      } else {
        view.clearPhysicsSelection();
        // Zones checked before triggers: a zone is usually the smaller, more specific thing.
        const tox::Zone* zone = zoneAtWorld(baked, world.x, world.z, mode);
        if (zone != nullptr) {
          state.selectZone(zone->id);
        } else {
          // Triggers: gate lines, picked alongside zones.
          const tox::Trigger* trigger = triggerAtWorld(baked, world.x, world.z, pickRadiusWorld, mode);
          if (trigger != nullptr) {
            state.selectTrigger(trigger->id);
          } else {
            // Mesh object placement markers: fixed-size, picked alongside zones/triggers, before
            // the much-larger road ribbon.
            const ModelPlacement* meshObject = meshObjectAtWorld(state.track(), world.x, world.z, pickRadiusWorld, mode);
            if (meshObject != nullptr) {
              state.selectMeshObject(meshObject->id);
            } else {
              // Nothing else was hit at this point (aux/position/physics/zone/trigger/mesh object
              // all missed), so clear every object selection before checking the road. Leaving a
              // stale selection here blocked panDragActive just as a stale point selection did,
              // making left-drag panning appear broken after selecting one.
              state.deselectAll();
              // Clicking the road itself (see pathAtWorld's
              // comment): picked last since it's the biggest, least-specific target on the
              // canvas. Selects that curve as "current" for the panels/dropdown.
              const auto pathHit = pathAtWorld(baked, world.x, world.z, pickRadiusWorld, mode);
              if (pathHit.has_value()) {
                state.setCurrentPathIndex(*pathHit);
              }
            }
          }
        }
      }
    }
    // A road click still selects the current path, but a path is not an object drag target. Let
    // a subsequent left drag pan from the road as well as from empty background; only actual
    // object selections reserve the gesture for their respective edit operation.
    panDragActive = !state.selection().valid() && !state.selectedZoneId().has_value() &&
                    !state.selectedTriggerId().has_value() && !state.selectedMeshObjectId().has_value();
  }

  // Gated on itemActive (this canvas's own InvisibleButton captured the mouse-down), not just a
  // global drag gesture -- ImGui::IsMouseDragging() alone is true regardless of which window's
  // widget is actually being dragged, so without this gate, dragging a point in ElevationView's
  // own canvas (a separate InvisibleButton) would ALSO be seen as a drag here on every frame both
  // windows draw, spuriously overwriting the selected point's X/Z with whatever world position the
  // mouse happens to be over in THIS view. Mirrors the itemActive gating handleRailsModeInput
  // already uses for right-click panning, just applied to the left-click point/mesh drag path too.
  // Excludes an active join-drag gesture (see below): that gesture never moves the dragged point,
  // pans, or drags a mesh/width/roll/cross-section/trigger handle, so none of the draggingGesture-
  // keyed branches below should fire while it owns the mouse -- otherwise a stale selection from
  // before the shift-click (e.g. a still-selected position point) would ALSO start moving under
  // the same drag.
  const bool draggingGesture = itemActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) && !joinDragFrom.has_value();

  // Join-drag update + release: runs independently of
  // draggingGesture's 2px threshold -- only the release-into-empty-space branch below is
  // distance-gated.
  if (joinDragFrom.has_value()) {
    if (itemActive && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      const WorldPoint2D world = view.screenToWorld(mouseLocal.x, mouseLocal.y);
      // Excludes only the dragged endpoint itself, not that path's OTHER endpoint -- dragging one
      // end onto the other end of the SAME open path is exactly how a curve closes itself,
      // mirroring hitTestOpenEndpoint's own doc comment and the plain drag-to-weld above.
      joinDragTarget = state.hitTestOpenEndpoint(world.x, world.z, pickRadiusWorld, joinDragFrom->pathIndex, joinDragFrom->atEnd);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
      if (joinDragTarget.has_value()) {
        // Over a valid target: connect. joinPathEndpoints itself makes the two endpoints coincide
        // (by copying the target point onto the source's slot), so there's no separate "snap" step
        // needed here.
        mutated = state.joinPathEndpoints(joinDragFrom->pathIndex, joinDragFrom->atEnd, joinDragTarget->pathIndex, joinDragTarget->atEnd) || mutated;
      } else {
        // Released into empty space: extend the curve instead, but only past a minimum drag
        // distance (kJoinDragMinPx), so a plain shift-click (no real drag) doesn't spuriously append
        // a point. Snapped to the grid.
        const float dx = mouseLocal.x - joinDragStartLocal.x, dy = mouseLocal.y - joinDragStartLocal.y;
        if (std::sqrt(dx * dx + dy * dy) >= kJoinDragMinPx) {
          const WorldPoint2D dropWorld = view.snapWorldXZ(view.screenToWorld(mouseLocal.x, mouseLocal.y));
          mutated = state.extendOpenPathFromEndpoint(joinDragFrom->pathIndex, joinDragFrom->atEnd, dropWorld.x, dropWorld.z).has_value() || mutated;
        }
      }
      joinDragFrom.reset();
      joinDragTarget.reset();
    } else {
      // Not released this frame -- report the live preview for drawJoinDragLine (below panDragActive
      // won't touch it since draggingGesture is already false for the whole rest of this gesture).
      outJoinDrag.from = joinDragFrom;
      outJoinDrag.target = joinDragTarget;
      outJoinDrag.currentLocal = mouseLocal;
    }
  }

  // selectionIsPosition() (not just selection().valid()) guards this: a roll/width/cross-section
  // handle counts as a valid selection too (those handles are click-to-
  // select only), but dragSelectedTo() writes to Vec3 pos.x/z, which those point kinds don't use
  // for placement at all (they're positioned by `t` along the baked centerline) -- without this
  // guard, dragging after clicking a roll handle would silently mutate an inert field and push a
  // no-visible-effect undo step instead of doing nothing, which is the actual intended behavior.
  if (draggingGesture && state.selectionIsPosition() && !panDragActive) {
    const bool startingNewDrag = !state.dragging();
    if (startingNewDrag) {
      // Drag-to-weld shouldn't fire just because the selected point already happens to be resting
      // on another open endpoint (e.g. right after a previous weld left them coincident) -- only
      // an actual drag ONTO an endpoint should weld. Captured once, before this gesture's first
      // dragSelectedTo() moves the point, and excluded from every candidate for the rest of the
      // gesture (even if the drag briefly leaves and returns to this exact spot).
      const auto draggedEnd = state.selectedOpenEndpointEnd();
      const tox::Vec3& restingPos = state.track().paths[state.selection().pathIndex].points[state.selection().pointIndex].pos;
      weldExcludeTarget = draggedEnd.has_value()
                              ? state.hitTestOpenEndpoint(restingPos.x, restingPos.z, pickRadiusWorld, state.selection().pathIndex, *draggedEnd)
                              : std::nullopt;
      state.beginDrag();
      view.freezeBounds(preDragBounds);
    }
    const WorldPoint2D world = view.snapWorldXZ(view.screenToWorld(mouseLocal.x, mouseLocal.y));
    state.dragSelectedTo(world.x, world.z);
    mutated = true;
    // Drag-to-weld (new functionality -- see EditorState::hitTestOpenEndpoint's comment): while
    // dragging an open path's own start/end point, look for another open endpoint under it so the
    // caller can highlight it now and joinPathEndpoints() can weld to it on release below.
    const auto draggedEnd = state.selectedOpenEndpointEnd();
    auto candidate = draggedEnd.has_value()
                         ? state.hitTestOpenEndpoint(world.x, world.z, pickRadiusWorld, state.selection().pathIndex, *draggedEnd)
                         : std::nullopt;
    if (candidate.has_value() && weldExcludeTarget.has_value() && candidate->pathIndex == weldExcludeTarget->pathIndex &&
        candidate->atEnd == weldExcludeTarget->atEnd)
      candidate.reset();
    weldTarget = candidate;
    outWeldTarget = weldTarget;
  } else if (draggingGesture && state.selectionIsWidth() && !panDragActive) {
    // On-canvas width-handle drag: distance of the mouse from the width point's centerline
    // position, projected onto the frame's unrolled h axis -- either edge handle sits at halfW
    // along h, so
    // |distance|*2 = full width regardless of which handle was grabbed to start the drag.
    const int pathIndex = state.selection().pathIndex;
    const Path& path = state.track().paths[pathIndex];
    const TrackPoint& point = path.points[state.selection().pointIndex];
    if (baked != nullptr && pathIndex < static_cast<int>(baked->paths.size()) && !baked->paths[pathIndex].centerline.empty()) {
      if (!state.dragging()) {
        state.beginDrag();
        view.freezeBounds(preDragBounds);
      }
      const WorldFrame2D f = sampleCenterlineAtG(baked->paths[pathIndex].centerline, path.closed, point.t, 1.0, mode);
      const WorldPoint2D world = view.screenToWorld(mouseLocal.x, mouseLocal.y);
      const double dist = (world.x - f.x) * f.hX + (world.z - f.z) * f.hZ;
      // Tangential component moves the point along the curve (see
      // dragAuxTAlongTangent's comment), computed from the SAME frame/mouse position as the
      // perpendicular value above, before either mutator fires this frame.
      const double newT = dragAuxTAlongTangent(baked->paths[pathIndex].centerline, path.closed, point.t, world.x, world.z, f, mode);
      state.dragSelectedWidthTo(std::round(std::abs(dist) * 2.0 * 10.0) / 10.0);
      state.dragSelectedAuxTTo(newT);
      mutated = true;
    }
  } else if (draggingGesture && state.selectionIsRoll() && !panDragActive) {
    // On-canvas roll-handle drag: signed distance of the mouse from the roll point's centerline
    // position, projected onto the frame's unrolled h axis (+h = right), divided by the frame's
    // width and scaled to degrees -- inverse of the perpendicular indicator line's own length.
    const int pathIndex = state.selection().pathIndex;
    const Path& path = state.track().paths[pathIndex];
    const TrackPoint& point = path.points[state.selection().pointIndex];
    if (baked != nullptr && pathIndex < static_cast<int>(baked->paths.size()) && !baked->paths[pathIndex].centerline.empty()) {
      if (!state.dragging()) {
        state.beginDrag();
        view.freezeBounds(preDragBounds);
      }
      const WorldFrame2D f = sampleCenterlineAtG(baked->paths[pathIndex].centerline, path.closed, point.t, 1.0, mode);
      const WorldPoint2D world = view.screenToWorld(mouseLocal.x, mouseLocal.y);
      const double dist = (world.x - f.x) * f.hX + (world.z - f.z) * f.hZ;
      const double roll = f.width > 0.0 ? (dist / f.width) * 180.0 : 0.0;
      const double newT = dragAuxTAlongTangent(baked->paths[pathIndex].centerline, path.closed, point.t, world.x, world.z, f, mode);
      state.dragSelectedRollTo(std::round(roll * 10.0) / 10.0);
      state.dragSelectedAuxTTo(newT);
      mutated = true;
    }
  } else if (draggingGesture && state.selectionIsCrossSection() && !panDragActive) {
    // On-canvas cross-section-handle drag, previously click-to-select only in this port -- same
    // pattern as width/roll above, now with the along-curve `t` component too.
    const int pathIndex = state.selection().pathIndex;
    const Path& path = state.track().paths[pathIndex];
    const TrackPoint& point = path.points[state.selection().pointIndex];
    if (baked != nullptr && pathIndex < static_cast<int>(baked->paths.size()) && !baked->paths[pathIndex].centerline.empty()) {
      if (!state.dragging()) {
        state.beginDrag();
        view.freezeBounds(preDragBounds);
      }
      const WorldFrame2D f = sampleCenterlineAtG(baked->paths[pathIndex].centerline, path.closed, point.t, 1.0, mode);
      const WorldPoint2D world = view.screenToWorld(mouseLocal.x, mouseLocal.y);
      const double dist = (world.x - f.x) * f.hX + (world.z - f.z) * f.hZ;
      const double curvature = f.width > 0.0 ? dist / (f.width / 2.0) : 0.0;
      const double newT = dragAuxTAlongTangent(baked->paths[pathIndex].centerline, path.closed, point.t, world.x, world.z, f, mode);
      state.dragSelectedCurvatureTo(std::round(curvature * 100.0) / 100.0);
      state.dragSelectedAuxTTo(newT);
      mutated = true;
    }
  } else if (draggingGesture && state.selectedTriggerId().has_value() && !panDragActive) {
    // On-canvas trigger center-handle drag (previously host.t was panel-edited only). Keeps the
    // trigger on its CURRENT host path via tangent-projection (dragAuxTAlongTangent, same helper
    // the aux-point t-drags use) rather than re-hosting onto whatever path is nearest the cursor --
    // there's no live spline evaluator here to do that search. No-op for a
    // mesh-hosted trigger (dragSelectedTriggerTTo itself refuses; skipped here too since there's no
    // path/centerline to sample).
    const Trigger* trigger = state.findTrigger(*state.selectedTriggerId());
    int pathIndex = -1;
    if (trigger != nullptr && trigger->host.kind == "path") {
      const auto& paths = state.track().paths;
      for (int i = 0; i < static_cast<int>(paths.size()); ++i)
        if (paths[i].id == trigger->host.pathId) {
          pathIndex = i;
          break;
        }
    }
    if (trigger != nullptr && pathIndex >= 0 && baked != nullptr && pathIndex < static_cast<int>(baked->paths.size()) &&
        !baked->paths[pathIndex].centerline.empty()) {
      if (!state.dragging()) {
        state.beginDrag();
        view.freezeBounds(preDragBounds);
      }
      const bool closed = state.track().paths[pathIndex].closed;
      const WorldFrame2D f = sampleCenterlineAtG(baked->paths[pathIndex].centerline, closed, trigger->host.t, 1.0, mode);
      const WorldPoint2D world = view.screenToWorld(mouseLocal.x, mouseLocal.y);
      const double newT = dragAuxTAlongTangent(baked->paths[pathIndex].centerline, closed, trigger->host.t, world.x, world.z, f, mode);
      state.dragSelectedTriggerTTo(newT);
      mutated = true;
    }
  } else if (draggingGesture && state.selectedMeshObjectId().has_value() && !panDragActive) {
    // On-canvas mesh object placement drag: plain drag moves it (shared dragging_/beginDrag
    // lifecycle, same as every other on-canvas drag above); shift+drag rotates it about its own
    // position instead, using the entity-agnostic rotateGestureActive_/beginRotateGesture/
    // dragRotateGestureTo plumbing Milestone 1.3 built ahead of time for exactly this. Which
    // rotation.x/y/z field a shift-drag writes follows the active ProjectionMode (see
    // meshObjectRotationDeg above, and EditorState::dragSelectedMeshObjectRotationTo which
    // actually performs the write), same TopDown=yaw/Front=pitch/Side=roll split
    // dragSelectedMeshObjectTo's plain-drag axes already use.
    const ModelPlacement* placement = state.findMeshObjectPlacement(*state.selectedMeshObjectId());
    if (placement != nullptr) {
      const WorldPoint2D world = view.screenToWorld(mouseLocal.x, mouseLocal.y);
      if (ImGui::GetIO().KeyShift) {
        const WorldPoint2D origin = planeCoords(mode, placement->position);
        if (!state.rotateGestureActive()) {
          view.freezeBounds(preDragBounds);
          state.beginRotateGesture(meshObjectRotationDeg(mode, *placement), angleFromOriginDeg(origin.x, origin.z, world.x, world.z));
        }
        const double newDeg = state.dragRotateGestureTo(angleFromOriginDeg(origin.x, origin.z, world.x, world.z));
        state.dragSelectedMeshObjectRotationTo(newDeg);
      } else {
        if (!state.dragging()) {
          view.freezeBounds(preDragBounds);
          state.beginDrag();
        }
        const WorldPoint2D snapped = view.snapWorldXZ(world);
        state.dragSelectedMeshObjectTo(snapped.x, snapped.z);
      }
      mutated = true;
    }
  } else if ((state.dragging() || state.rotateGestureActive()) && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    // Weld on release, consuming whatever hitTestOpenEndpoint last found during the drag above --
    // this frame's draggingGesture is typically already false by the time IsMouseReleased fires,
    // so weldTarget (unlike outWeldTarget) has to survive from the last frame that computed it.
    // A no-op for anything that isn't a position-point drag (weldTarget stays empty), including a
    // mesh object placement's plain drag or shift-drag rotate -- this one shared release path
    // covers all of them, including ending whichever of dragging_/rotateGestureActive_ was active.
    if (weldTarget.has_value()) {
      const auto draggedEnd = state.selectedOpenEndpointEnd();
      if (draggedEnd.has_value())
        mutated = state.joinPathEndpoints(state.selection().pathIndex, *draggedEnd, weldTarget->pathIndex, weldTarget->atEnd) || mutated;
    }
    weldTarget.reset();
    weldExcludeTarget.reset();
    state.endDrag();
    state.endRotateGesture();
    view.releaseBoundsFreeze();
  } else if (draggingGesture && panDragActive) {
    // Left-drag-on-empty-space pan -- decided at mousedown (see
    // panDragActive's own comment above), not just "nothing happens to be selected right now".
    view.pan(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y);
  }
  return mutated;
}

// Shift-drag rubber band: a line from the dragged endpoint's world
// position to wherever the cursor currently is -- yellow while free, green (reusing
// kWeldTargetColor) and snapped to the target endpoint's own position once hovering a valid drop
// point. No-op if the gesture isn't active.
void drawJoinDragLine(ImDrawList* drawList, const ImVec2& canvasOrigin, const TopDownView& view, const tox::Track* baked,
                      const JoinDragPreview& joinDrag, ProjectionMode mode) {
  if (!joinDrag.from.has_value() || baked == nullptr) return;
  if (joinDrag.from->pathIndex < 0 || joinDrag.from->pathIndex >= static_cast<int>(baked->paths.size())) return;
  const auto& fromAnchors = baked->paths[joinDrag.from->pathIndex].anchors;
  if (fromAnchors.empty()) return;
  const tox::Vec3& fromPos = joinDrag.from->atEnd ? fromAnchors.back() : fromAnchors.front();
  const ImVec2 from = toAbsolute(canvasOrigin, worldToScreenPlane(view, mode, fromPos));

  ImVec2 to = toAbsolute(canvasOrigin, joinDrag.currentLocal);
  ImU32 color = kJoinDragFreeColor;
  if (joinDrag.target.has_value() && joinDrag.target->pathIndex >= 0 && joinDrag.target->pathIndex < static_cast<int>(baked->paths.size())) {
    const auto& targetAnchors = baked->paths[joinDrag.target->pathIndex].anchors;
    if (!targetAnchors.empty()) {
      const tox::Vec3& targetPos = joinDrag.target->atEnd ? targetAnchors.back() : targetAnchors.front();
      to = toAbsolute(canvasOrigin, worldToScreenPlane(view, mode, targetPos));
      color = kWeldTargetColor;
    }
  }
  drawList->AddLine(from, to, color, kJoinDragLineThickness);
}

// handleRailsModeInput (EditMode::Rails's modal edge-toggle input) was removed along with Rails
// mode itself (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 2).

// Left click: add a draft point, or close/finish the draft. Right click: cancel the draft.
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

// Bounds of whichever of the three mutually-exclusive selection kinds (EditorState::deselectAll's
// comment) is currently selected, for the "Object" zoom-to-selection feature. A single point has
// zero area, so it comes back as a degenerate
// {x,x,z,z} bounds; TopDownView::focusOn floors that to a fixed minimum span rather than zooming
// to infinity. Returns nullopt if nothing is selected, or a selected id/index no longer resolves
// (stale selection, or `baked` not ready yet).
std::optional<TrackBounds2D> selectedObjectBounds(const EditorState& state, const tox::Track* baked, ProjectionMode mode) {
  if (state.selectedMeshObjectId().has_value()) {
    const ModelPlacement* placement = state.findMeshObjectPlacement(*state.selectedMeshObjectId());
    if (placement == nullptr) return std::nullopt;
    const WorldPoint2D p = planeCoords(mode, placement->position);
    return TrackBounds2D{p.x, p.x, p.z, p.z};
  }
  if (state.selectedZoneId().has_value()) {
    if (baked == nullptr) return std::nullopt;
    for (const auto& zone : baked->zones) {
      if (zone.id != *state.selectedZoneId()) continue;
      const std::vector<WorldPoint2D> outline = zoneOutlineWorld(*baked, zone, mode);
      if (outline.empty()) return std::nullopt;
      TrackBounds2D b{outline[0].x, outline[0].x, outline[0].z, outline[0].z};
      for (const auto& p : outline) {
        b.minX = std::min(b.minX, p.x);
        b.maxX = std::max(b.maxX, p.x);
        b.minZ = std::min(b.minZ, p.z);
        b.maxZ = std::max(b.maxZ, p.z);
      }
      return b;
    }
    return std::nullopt;
  }
  if (state.selectedTriggerId().has_value()) {
    if (baked == nullptr) return std::nullopt;
    for (const auto& trigger : baked->triggers) {
      if (trigger.id != *state.selectedTriggerId()) continue;
      const WorldPoint2D a = planeCoords(mode, trigger.center - trigger.right * trigger.halfWidth);
      const WorldPoint2D b = planeCoords(mode, trigger.center + trigger.right * trigger.halfWidth);
      return TrackBounds2D{std::min(a.x, b.x), std::max(a.x, b.x), std::min(a.z, b.z), std::max(a.z, b.z)};
    }
    return std::nullopt;
  }
  if (state.selection().valid()) {
    const SelectedPoint& sel = state.selection();
    if (sel.pathIndex < 0 || sel.pathIndex >= static_cast<int>(state.track().paths.size())) return std::nullopt;
    const Path& path = state.track().paths[sel.pathIndex];
    if (sel.pointIndex < 0 || sel.pointIndex >= static_cast<int>(path.points.size())) return std::nullopt;
    const TrackPoint& point = path.points[sel.pointIndex];
    double x, z;
    if (point.kind == PointKind::Position) {
      const WorldPoint2D p = planeCoords(mode, point.pos);
      x = p.x;
      z = p.z;
    } else {
      // Roll/width/crossSection points have no `pos` of their own -- positioned by `t` along the
      // baked centerline instead, same as the aux-handle rendering/dragging code above.
      if (baked == nullptr || sel.pathIndex >= static_cast<int>(baked->paths.size()) || baked->paths[sel.pathIndex].centerline.empty())
        return std::nullopt;
      const WorldFrame2D f = sampleCenterlineAtG(baked->paths[sel.pathIndex].centerline, path.closed, point.t, 1.0, mode);
      x = f.x;
      z = f.z;
    }
    return TrackBounds2D{x, x, z, z};
  }
  return std::nullopt;
}

}  // namespace

bool FocusOnSelection(TopDownView& view, const EditorState& state, const tox::Track* baked) {
  const auto target = selectedObjectBounds(state, baked, state.projectionMode());
  if (!target.has_value()) return false;
  view.focusOn(*target, computeViewBounds(state.track(), baked, state.projectionMode()));
  return true;
}

bool DrawTopDownCanvas(TopDownView& view, EditorState& state, const tox::Track* baked, std::optional<WorldPoint2D>* hoveredWorldOut,
                       const std::filesystem::path& modelBaseDir) {
  const ProjectionMode mode = state.projectionMode();
  const TrackBounds2D bounds = computeViewBounds(state.track(), baked, mode);

  ImGui::BeginChild("TopDownCanvas", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
  ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  canvasSize.x = std::max(1.0f, canvasSize.x);
  canvasSize.y = std::max(1.0f, canvasSize.y);

  view.computeView(bounds, canvasSize.x, canvasSize.y);

  // Zoom slider + Home: a vertical zoom slider (same -100..250 range as scroll-wheel zoom, see
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
                              ImGui::GetFrameHeight() + style.ItemSpacing.y + ImGui::GetFrameHeight();
    const ImVec2 groupPos(canvasOrigin.x + canvasSize.x - kControlWidth - kMargin - kPad,
                          canvasOrigin.y + canvasSize.y - groupHeight - kMargin - kPad);
    const ImVec2 panelMin(groupPos.x - kPad, groupPos.y - kPad);
    const ImVec2 panelMax(groupPos.x + kControlWidth + kPad, groupPos.y + groupHeight + kPad);
    // rgba(16,32,46,0.82) fill, #2c6a9e 1px border, 8px radius.
    drawList->AddRectFilled(panelMin, panelMax, IM_COL32(16, 32, 46, 209), 8.0f);
    drawList->AddRect(panelMin, panelMax, IM_COL32(44, 106, 158, 255), 8.0f, 0, 1.0f);

    ImGui::SetCursorScreenPos(groupPos);
    ImGui::PushID("TopDownZoomControl");
    ImGui::BeginGroup();
    // #4fd6ff accent colour.
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
    // it zooms about the view's current center instead.
    if (ImGui::VSliderFloat("##zoom", ImVec2(kControlWidth, kSliderHeight), &zoomValue, static_cast<float>(TopDownView::kZoomSliderMin),
                            static_cast<float>(TopDownView::kZoomSliderMax), ""))
      view.setZoomSlider(zoomValue);
    ImGui::PopStyleColor(5);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(79, 214, 255, 255));
    ImGui::TextUnformatted(" -");
    ImGui::PopStyleColor();
    // #16344a bg / #1f4c6b hover.
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(22, 52, 74, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(31, 76, 107, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(44, 106, 158, 255));
    if (ImGui::Button("Home", ImVec2(kControlWidth, 0))) view.resetView();
    ImGui::PopStyleColor(3);
    // Zoom-to-selection, mirroring the 'x'
    // hotkey/View-menu entry in main.cpp -- both go through FocusOnSelection so the bounds
    // resolution logic lives in exactly one place. Disabled when nothing is selected, same
    // pattern as every other "acts on the current selection" button elsewhere in this editor.
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(22, 52, 74, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(31, 76, 107, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(44, 106, 158, 255));
    ImGui::BeginDisabled(!selectedObjectBounds(state, baked, mode).has_value());
    if (ImGui::Button("Object", ImVec2(kControlWidth, 0))) FocusOnSelection(view, state, baked);
    ImGui::EndDisabled();
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

  if (hoveredWorldOut != nullptr) *hoveredWorldOut = hovered ? std::optional(view.screenToWorld(mouseLocal.x, mouseLocal.y)) : std::nullopt;

  if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
    // 15 slider units per wheel notch; ImGui's MouseWheel is already normalized to ~1 per notch.
    view.zoomAt(mouseLocal.x, mouseLocal.y, ImGui::GetIO().MouseWheel * 15.0, bounds);
  }

  bool mutated = false;
  static WorldPoint2D contextMenuWorld;  // set right before OpenPopup, read once BeginPopup opens it
  const double pickRadiusWorld = kPickRadiusPx / view.scale();
  std::optional<EditorState::OpenEndpointRef> weldTarget;
  JoinDragPreview joinDrag;
  switch (state.mode()) {
    case EditMode::Edit: {
      mutated = handleEditModeInput(state, view, baked, bounds, mouseLocal, pickRadiusWorld, hovered, itemActive, weldTarget, joinDrag);
      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) view.pan(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y);
      if (windowFocused && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
        if (state.selection().valid())
          mutated = state.deleteSelectedPoint() || mutated;
        else if (state.selectedZoneId().has_value())
          mutated = state.deleteSelectedZone() || mutated;
        else if (state.selectedTriggerId().has_value())
          mutated = state.deleteSelectedTrigger() || mutated;
        else if (state.selectedMeshObjectId().has_value())
          mutated = state.deleteSelectedMeshObjectPlacement() || mutated;
      }
      // Right-click context menu: a right-*click* (no drag) opens it instead of panning; a
      // real drag still pans, since ResetMouseDragDelta below only ever fires on release, after
      // the drag's own per-frame pan deltas already applied.
      if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        const ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right, 0.0f);
        if (std::abs(dragDelta.x) < 3.0f && std::abs(dragDelta.y) < 3.0f) {
          contextMenuWorld = view.screenToWorld(mouseLocal.x, mouseLocal.y);
          ImGui::OpenPopup("TopDownContextMenu");
        }
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
      }
      if (ImGui::BeginPopup("TopDownContextMenu")) {
        // Add control point: Position/Roll/Width/Cross-section -- seeded from the nearest baked
        // centerline frame's actual current value at that point (not a schema default).
        const std::optional<NearestPathPlacement> nearPlacement = nearestPathPlacement(baked, contextMenuWorld.x, contextMenuWorld.z, mode);
        ImGui::TextDisabled("Add control point");
        ImGui::BeginDisabled(!nearPlacement.has_value());
        // Position: `t` (a curve-parametric fraction from the baked centerline's discrete samples)
        // reconstructs the approximate segment index via `g = t * gMax`. The click's plane
        // coordinates (contextMenuWorld.x/.z, already the active mode's (u, v)) go into the two
        // axes that plane covers; the third axis -- outside the click, e.g. Y in TopDown -- comes
        // from the nearest baked frame instead of a schema default.
        if (ImGui::MenuItem("Position")) {
          const Path& authoredPath = state.track().paths[nearPlacement->pathIndex];
          const int n = EditorState::positionCount(authoredPath);
          const double gMax = authoredPath.closed ? n : n - 1;
          const double g = nearPlacement->t * gMax;
          const int insertAt = authoredPath.closed ? (static_cast<int>(std::floor(g)) + 1) % (n + 1)
                                                   : std::min(n, static_cast<int>(std::floor(g)) + 1);
          tox::Vec3 insertPos = nearPlacement->frame->pos;
          setPlaneCoords(mode, insertPos, contextMenuWorld.x, contextMenuWorld.z);
          if (state.insertPositionOnSegment(nearPlacement->pathIndex, insertAt, insertPos.x, insertPos.y, insertPos.z).has_value())
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

        // Add zone/trigger: mirrors addZoneAt/addTriggerAt's path-anchored branch.
        ImGui::Separator();
        ImGui::TextDisabled("Add zone");
        ImGui::BeginDisabled(!nearPlacement.has_value());
        if (ImGui::MenuItem("Boost")) {
          mutated = state.addPathZone(nearPlacement->pathIndex, "velocityChange", nearPlacement->t, nearPlacement->lateral).has_value() || mutated;
        }
        if (ImGui::MenuItem("Jump")) {
          mutated = state.addPathZone(nearPlacement->pathIndex, "jump", nearPlacement->t, nearPlacement->lateral).has_value() || mutated;
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

        // Place an instance of an already-embedded Model (TRACK_MODEL_LIST_PLAN.md: "File > Load
        // Model..." only embeds now, it doesn't place -- this submenu is the one place placements
        // are actually created). Disabled with nothing to place when the Track has no embedded
        // Models yet.
        ImGui::Separator();
        ImGui::BeginDisabled(state.track().models.empty());
        if (ImGui::BeginMenu("Place Model")) {
          for (const auto& model : state.track().models) {
            const std::string label = model.id.value_or(model.modelFile) + "  (" + model.modelFile + ")";
            if (ImGui::MenuItem(label.c_str()) && model.id.has_value()) {
              state.placeModelInstance(*model.id, contextMenuWorld.x, contextMenuWorld.z);
              mutated = true;
            }
          }
          ImGui::EndMenu();
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
    for (std::size_t i = 0; i < baked->paths.size(); ++i)
      drawBakedPath(drawList, canvasOrigin, view, baked->paths[i], baked->definition.paths[i], view.renderMode(), minElev, maxElev,
                    static_cast<int>(i) == state.currentPathIndex(), mode);
    drawZones(drawList, canvasOrigin, view, *baked, state.selectedZoneId(), mode);
    // Hover highlight (distinct from click-driven selection, new functionality -- triggers
    // previously only ever showed a selected/unselected state): only meaningful in Edit mode, the
    // only mode where a plain click on a trigger does anything, mirrors the same gate
    // hoveredPosition uses below.
    std::optional<std::string> hoveredTriggerId;
    if (hovered && state.mode() == EditMode::Edit) {
      const WorldPoint2D hoverWorld = view.screenToWorld(mouseLocal.x, mouseLocal.y);
      const tox::Trigger* hoveredTrigger = triggerAtWorld(baked, hoverWorld.x, hoverWorld.z, pickRadiusWorld, mode);
      if (hoveredTrigger != nullptr) hoveredTriggerId = hoveredTrigger->id;
    }
    drawTriggers(drawList, canvasOrigin, view, *baked, state.selectedTriggerId(), hoveredTriggerId, mode);
    drawMeshObjectPlacements(drawList, canvasOrigin, view, state.track(), state.selectedMeshObjectId(), mode, modelBaseDir);
    if (view.showPhysicsPoints()) drawPhysicsPoints(drawList, canvasOrigin, view, *baked, mode);
    // Self-intersection crossing markers, drawn right after the
    // physics-point dots and before the start marker, and
    // before the authored point draw further below, so markers never occlude editable handles.
    drawCrossings(drawList, canvasOrigin, view, *baked, state.track().selfIntersectionOverrides, mode);
    // Start marker + direction arrow, drawn right after
    // the physics-point dots and before the selected-segment highlights. EditorState's own
    // mutators already call its private clampStart() after every structural edit, so track().start
    // is valid here without needing to re-clamp (drawStartMarker itself defensively bounds-checks
    // the path index and clamps `point`'s corresponding g via sampleCenterlineAtG regardless).
    drawStartMarker(drawList, canvasOrigin, view, *baked, state.track().start, mode);
  }
  if (view.showPositionPoints()) {
    // Hover highlight (distinct from click-driven selection): only meaningful in Edit mode, the
    // only mode where a plain click on a position point does anything (Create mode's clicks build
    // a draft path; Rails mode picks mesh edges, not authored points).
    std::optional<SelectedPoint> hoveredPosition;
    if (hovered && state.mode() == EditMode::Edit) {
      const WorldPoint2D hoverWorld = view.screenToWorld(mouseLocal.x, mouseLocal.y);
      hoveredPosition = state.hoverTestPosition(hoverWorld.x, hoverWorld.z, pickRadiusWorld);
    }
    // Selected point's adjacent segments: drawn before the
    // point nodes so the nodes render on top.
    drawSegmentHighlight(drawList, canvasOrigin, view, baked, state.selectedIncomingSegment(), kIncomingSegmentColor, mode);
    drawSegmentHighlight(drawList, canvasOrigin, view, baked, state.selectedOutgoingSegment(), kOutgoingSegmentColor, mode);
    drawAuthoredPositionPoints(drawList, canvasOrigin, view, state.track(), state.selection(), hoveredPosition, weldTarget, state.disjointSeams(),
                               mode);
    drawJoinDragLine(drawList, canvasOrigin, view, baked, joinDrag, mode);
  }
  // Roll/width/cross-section handles, drawn after position points so they sit on top.
  drawAuxPoints(drawList, canvasOrigin, view, state.track(), baked, state.currentPathIndex(), state.selection(), mode);
  if (state.mode() == EditMode::Create) drawCreateDraft(drawList, canvasOrigin, view, state.createDraft(), mode);

  // Merges channel 1 (the zoom control, submitted early for interaction priority -- see the
  // comment where ChannelsSplit was called) back on top of channel 0 (everything drawn above).
  drawList->ChannelsMerge();

  ImGui::EndChild();
  return mutated;
}

}  // namespace editor
