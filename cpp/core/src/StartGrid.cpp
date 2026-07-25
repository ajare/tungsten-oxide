// StartGrid.cpp — bodies for include/StartGrid.hpp, transliterated from
// js/ship-grid.js and the grid/settle portion of js/track-game.js.
#include "StartGrid.hpp"

#include <algorithm>
#include <cmath>

namespace tox {
namespace StartGrid {

GridSlot gridSlot(int index, const GridSlotOptions& options) {
  const int row = index / GRID_COLUMNS;
  const int column = index % GRID_COLUMNS;  // 0 left, 1 right
  const bool leftIsAhead = row % 2 == 0;
  const bool isAhead = column == (leftIsAhead ? 0 : 1);
  const double lateralLimit = std::max(0.0, options.lateralLimit);
  const double halfSpacing = std::min(options.lateralSpacing / 2.0, lateralLimit);
  GridSlot slot;
  slot.row = row;
  slot.column = column;
  slot.lateral = halfSpacing == 0.0 ? 0.0 : (column == 0 ? -halfSpacing : halfSpacing);
  slot.behind = row * options.rowSpacing + (isAhead ? 0.0 : options.stagger);
  return slot;
}

std::vector<GridSlot> gridSlots(int count, const GridSlotOptions& options) {
  std::vector<GridSlot> out;
  out.reserve(static_cast<size_t>(std::max(0, count)));
  for (int i = 0; i < count; i++) out.push_back(gridSlot(i, options));
  return out;
}

Sample interpolatedGridFrame(const Path& path, int startIndex, double distanceBehind, bool reverse) {
  const auto& cl = path.centerline;
  const int count = static_cast<int>(cl.size());
  const int step = reverse ? 1 : -1;
  int at = startIndex, next = at;
  double remaining = distanceBehind, frac = 0.0;
  for (int n = 0; n < count && remaining > 1e-9; n++) {
    const int candidate = path.closed ? (at + step + count) % count : at + step;
    if (candidate < 0 || candidate >= count) break;
    const double len = cl[at].pos.distanceTo(cl[candidate].pos);
    if (remaining <= len && len > 0.0) {
      next = candidate;
      frac = remaining / len;
      remaining = 0.0;
      break;
    }
    remaining -= len;
    at = candidate;
    next = at;
    frac = 0.0;
  }
  const Frame& a = cl[at];
  const Frame& b = cl[next];
  Sample s;
  s.pos = a.pos.clone().lerp(b.pos, frac);
  s.tangent = a.tangent.clone().lerp(b.tangent, frac).normalize();
  s.edgeRight = a.edgeRight.clone().lerp(b.edgeRight, frac).normalize();
  s.normal = a.normal.clone().lerp(b.normal, frac).normalize();
  s.sLeft = a.sLeft + (b.sLeft - a.sLeft) * frac;
  s.sRight = a.sRight + (b.sRight - a.sRight) * frac;
  s.crossSectionCurvature = a.crossSectionCurvature + (b.crossSectionCurvature - a.crossSectionCurvature) * frac;
  s.crossSectionTightness = a.crossSectionTightness + (b.crossSectionTightness - a.crossSectionTightness) * frac;
  return s;
}

std::vector<Pose> startingGridPoses(const Simulation& sim, const Track& track, int count) {
  const auto& paths = track.paths;
  const int pathIndex = std::clamp(track.definition.start.path, 0, static_cast<int>(paths.size()) - 1);
  const Path& path = paths[static_cast<size_t>(pathIndex)];
  const int pointIndex = std::clamp(track.definition.start.point, 0, static_cast<int>(path.anchors.size()) - 1);
  const Vec3& anchor = path.anchors[static_cast<size_t>(pointIndex)];
  const bool reverse = track.definition.start.reverse;

  int startIndex = 0;
  double bestD = std::numeric_limits<double>::infinity();
  for (size_t i = 0; i < path.centerline.size(); i++) {
    const double d = path.centerline[i].pos.distanceToSquared(anchor);
    if (d < bestD) {
      bestD = d;
      startIndex = static_cast<int>(i);
    }
  }

  std::vector<Pose> poses;
  poses.reserve(static_cast<size_t>(std::max(0, count)));
  for (int i = 0; i < count; i++) {
    const GridSlot rough = gridSlot(i);
    const Sample frame = interpolatedGridFrame(path, startIndex, rough.behind, reverse);
    const double lo = frame.sLeft + TrackCore::COLLISION_WALL_MARGIN + SHIP_HALF_WIDTH;
    const double hi = frame.sRight - TrackCore::COLLISION_WALL_MARGIN - SHIP_HALF_WIDTH;
    GridSlotOptions narrowOpts;
    narrowOpts.lateralLimit = std::max(0.0, std::min(-lo, hi));
    const GridSlot slot = gridSlot(i, narrowOpts);

    SurfaceFrame surface = curvedSurfaceFrame(frame, slot.lateral);
    Sample canonical = frame;
    // Settle the analytically-placed slot onto the exact same sampled surface
    // the parked physics branch uses, so an idle ship does not creep while the
    // two representations converge over its first frames.
    for (int n = 0; n < 3; n++) {
      canonical = sim.sampleTrack(surface.pos.x, surface.pos.y, surface.pos.z);
      const Projection proj = projectToSurface(canonical, surface.pos.x, surface.pos.y, surface.pos.z);
      surface = curvedSurfaceFrame(canonical, TrackCore::clamp(proj.s, proj.loS, proj.hiS));
    }

    Vec3 forward = canonical.tangent.clone().multiplyScalar(reverse ? -1.0 : 1.0).normalize();
    tangentize(forward, surface.normal, forward);
    poses.push_back(Pose{surface.pos, surface.normal, forward});
  }
  return poses;
}

}  // namespace StartGrid
}  // namespace tox
