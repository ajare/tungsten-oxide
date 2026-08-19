#include "RandomTrack.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <numbers>
#include <set>
#include <vector>

#include "Track.hpp"

namespace editor {
namespace {

// The mulberry32 PRNG: uint32_t wraparound arithmetic gives a deterministic bit sequence from a
// seed (XOR, add, multiply all wrap identically whether the bit pattern is read as signed or
// unsigned).
struct Mulberry32 {
  std::uint32_t a;
  explicit Mulberry32(std::uint32_t seed) : a(seed) {}
  double next() {
    a += 0x6D2B79F5u;
    std::uint32_t t = a;
    t = (t ^ (t >> 15)) * (t | 1u);
    t = (t + ((t ^ (t >> 7)) * (t | 61u))) ^ t;
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
  }
};

// Mirrors measureLoopLength: bake a bare closed loop through core's real loader/spline (not a
// reimplemented evaluator) and sum consecutive centerline frame distances. Roll/width/cross-
// section are core's synthesized defaults (flat, DEFAULT_WIDTH) -- irrelevant here since none of
// them affect centerline position, only the road surface around it.
double measureClosedLoopLength(const std::vector<tox::Vec3>& positions) {
  TrackDefinition probe;
  Path path;
  path.closed = true;
  for (const auto& pos : positions) {
    TrackPoint point;
    point.kind = PointKind::Position;
    point.pos = pos;
    path.points.push_back(point);
  }
  probe.paths.push_back(std::move(path));
  const tox::TrackLoadResult loaded = tox::Track::fromJson(toJson(probe));
  if (!loaded || loaded.track->paths.empty()) return 0.0;
  const auto& centerline = loaded.track->paths[0].centerline;
  if (centerline.size() < 2) return 0.0;
  double length = 0.0;
  for (std::size_t i = 0; i < centerline.size(); ++i) {
    const tox::Vec3& a = centerline[i].pos;
    const tox::Vec3& b = centerline[(i + 1) % centerline.size()].pos;
    length += glm::distance(a, b);
  }
  return length;
}

// Douglas-Peucker-style simplification retaining only geometrically meaningful controls, plus
// short endpoint runs (mirrors simplifyGeneratedCoords). Only called on open, non-ramp paths, per
// generatedPath's own guard.
std::vector<tox::Vec3> simplifyGeneratedCoords(const std::vector<tox::Vec3>& coords, double tolerance = 75.0,
                                               std::size_t minimum = 5) {
  if (coords.size() <= minimum) return coords;
  const std::size_t n = coords.size();

  auto distanceToSegment = [](const tox::Vec3& p, const tox::Vec3& a, const tox::Vec3& b) {
    const double abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
    const double apx = p.x - a.x, apy = p.y - a.y, apz = p.z - a.z;
    const double len2 = abx * abx + aby * aby + abz * abz;
    const double tt = len2 > 1e-9 ? std::clamp((apx * abx + apy * aby + apz * abz) / len2, 0.0, 1.0) : 0.0;
    const double dx = apx - abx * tt, dy = apy - aby * tt, dz = apz - abz * tt;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  };

  std::set<std::size_t> keep{0, 1, n - 2, n - 1};
  std::function<void(std::size_t, std::size_t)> visit = [&](std::size_t lo, std::size_t hi) {
    long farthest = -1;
    double farthestDistance = tolerance;
    for (std::size_t i = lo + 1; i < hi; ++i) {
      const double d = distanceToSegment(coords[i], coords[lo], coords[hi]);
      if (d > farthestDistance) {
        farthest = static_cast<long>(i);
        farthestDistance = d;
      }
    }
    if (farthest < 0) return;
    const auto f = static_cast<std::size_t>(farthest);
    keep.insert(f);
    visit(lo, f);
    visit(f, hi);
  };
  visit(0, n - 1);

  for (std::size_t slot = 1; keep.size() < minimum && slot < minimum - 1; ++slot) {
    keep.insert(static_cast<std::size_t>(std::llround(static_cast<double>(slot) * static_cast<double>(n - 1) /
                                                       static_cast<double>(minimum - 1))));
  }

  std::vector<tox::Vec3> out;
  out.reserve(keep.size());
  for (std::size_t idx : keep) out.push_back(coords[idx]);
  return out;
}

// Union-find grouping of consecutive near-straight runs (turn angle < pi/8), each group's Y
// averaged and reapplied -- mirrors flattenTightTurnElevations. Applied to every non-ramp path
// (both open and closed).
std::vector<tox::Vec3> flattenTightTurnElevations(const std::vector<tox::Vec3>& coords, bool closed) {
  std::vector<tox::Vec3> out = coords;
  const int n = static_cast<int>(out.size());
  if (n < 3) return out;

  std::vector<int> parent(n);
  for (int i = 0; i < n; ++i) parent[i] = i;
  std::function<int(int)> root = [&](int i) {
    while (parent[i] != i) {
      parent[i] = parent[parent[i]];
      i = parent[i];
    }
    return i;
  };
  auto unite = [&](int a, int b) {
    a = root(a);
    b = root(b);
    if (a != b) parent[b] = a;
  };

  const int first = closed ? 0 : 1;
  const int last = closed ? n : n - 1;
  for (int i = first; i < last; ++i) {
    const int pm = (i - 1 + n) % n, pp = (i + 1) % n;
    const double ix = out[i].x - out[pm].x, iz = out[i].z - out[pm].z;
    const double ox = out[pp].x - out[i].x, oz = out[pp].z - out[i].z;
    const double il = std::hypot(ix, iz) > 0.0 ? std::hypot(ix, iz) : 1.0;
    const double ol = std::hypot(ox, oz) > 0.0 ? std::hypot(ox, oz) : 1.0;
    const double angle = std::acos(std::clamp((ix * ox + iz * oz) / (il * ol), -1.0, 1.0));
    if (angle >= std::numbers::pi / 8.0) {
      unite(pm, i);
      unite(i, pp);
    }
  }

  std::map<int, std::pair<double, std::vector<int>>> groups;
  for (int i = 0; i < n; ++i) {
    auto& g = groups[root(i)];
    g.first += out[i].y;
    g.second.push_back(i);
  }
  for (auto& [r, g] : groups) {
    if (g.second.size() < 2) continue;
    const double level = g.first / static_cast<double>(g.second.size());
    for (int i : g.second) out[i].y = level;
  }
  return out;
}

// Builds an authored Path from raw [x,y,z] coords, drawing roll/width/cross-section from `rnd` in
// a fixed order (position points first, then one roll/width/crossSection triplet per coordinate).
Path generatedPath(const std::string& id, std::vector<tox::Vec3> coords, Mulberry32& rnd, const RandomTrackRanges& ranges,
                    double complexityT, bool ramp, bool closed) {
  if (!ramp && !closed) coords = simplifyGeneratedCoords(coords);
  if (!ramp) coords = flattenTightTurnElevations(coords, closed);

  Path path;
  path.id = id;
  path.closed = closed;
  const int n = static_cast<int>(coords.size());
  for (int i = 0; i < n; ++i) {
    TrackPoint point;
    point.kind = PointKind::Position;
    point.id = id + "-p-" + std::to_string(i + 1);
    point.pos = coords[i];
    point.weight = 1.0;
    path.points.push_back(point);
  }

  const double last = std::max(1.0, static_cast<double>(closed ? n : n - 1));
  constexpr double kDefaultWidth = 36.0;  // TrackCore.DEFAULT_WIDTH
  for (int i = 0; i < n; ++i) {
    double roll = 0.0;
    if (!ramp && (closed || (i > 0 && i < n - 1))) {
      const tox::Vec3& pm = coords[(i - 1 + n) % n];
      const tox::Vec3& p = coords[i];
      const tox::Vec3& pp = coords[(i + 1) % n];
      const double inx = p.x - pm.x, inz = p.z - pm.z;
      const double outx = pp.x - p.x, outz = pp.z - p.z;
      const double inl = std::hypot(inx, inz) > 0.0 ? std::hypot(inx, inz) : 1.0;
      const double outl = std::hypot(outx, outz) > 0.0 ? std::hypot(outx, outz) : 1.0;
      const double m = std::clamp((inz * outx - inx * outz) / (inl * outl), -1.0, 1.0);
      roll = std::clamp((std::asin(m) / 0.6) * ranges.maxBanking, -ranges.maxBanking, ranges.maxBanking) * complexityT;
    }
    TrackPoint rollPoint;
    rollPoint.kind = PointKind::Roll;
    rollPoint.t = static_cast<double>(i) / last;
    rollPoint.roll = roll;
    path.points.push_back(rollPoint);

    const bool safeEndpoint = !closed && (ramp || i == 0 || i == n - 1);
    const double sample = ranges.widthMin + (ranges.widthMax - ranges.widthMin) * rnd.next();
    const double width = safeEndpoint ? kDefaultWidth * 2.0 : std::max(1.0, kDefaultWidth + (sample - kDefaultWidth) * complexityT);
    TrackPoint widthPoint;
    widthPoint.kind = PointKind::Width;
    widthPoint.t = static_cast<double>(i) / last;
    widthPoint.width = width;
    path.points.push_back(widthPoint);

    TrackPoint crossSectionPoint;
    crossSectionPoint.kind = PointKind::CrossSection;
    crossSectionPoint.t = static_cast<double>(i) / last;
    crossSectionPoint.curvature = ramp ? 0.0 : -rnd.next() * ranges.maxCurvature * complexityT;
    crossSectionPoint.tightness = 1.0;
    crossSectionPoint.thickness = 4.0;  // TrackCore.DEFAULT_CROSS_SECTION_THICKNESS
    path.points.push_back(crossSectionPoint);
  }
  return path;
}

}  // namespace

TrackDefinition generateRandomTrack(int complexity, std::uint32_t seed, const RandomTrackRanges& ranges) {
  Mulberry32 rnd(seed);
  const double t = (std::clamp(complexity, 1, 10) - 1) / 9.0;
  const int turnsMin = std::max(4, ranges.turnsMin);
  const int turnsMax = std::max(turnsMin, ranges.turnsMax);
  const int n = std::max(4, static_cast<int>(std::round(turnsMin + (turnsMax - turnsMin) * t)));

  // Strictly increasing angle around a centre guarantees a simple (non-self-crossing) loop; radius
  // jitter (growing with complexity) makes the turns, angle jitter stays under half the spacing so
  // order can never invert.
  constexpr double baseR = 1000.0;
  const double jitterAmp = 0.12 + 0.42 * t;
  const double spacing = 2.0 * std::numbers::pi / n;
  std::vector<double> xs(n), zs(n), ys(n, 0.0);
  for (int i = 0; i < n; ++i) {
    const double ang = i * spacing + (rnd.next() - 0.5) * 0.8 * spacing;
    const double r = baseR * (1.0 + (rnd.next() - 0.5) * 2.0 * jitterAmp);
    xs[i] = std::cos(ang) * r;
    zs[i] = std::sin(ang) * r;
  }

  std::vector<tox::Vec3> flat(n);
  for (int i = 0; i < n; ++i) flat[i] = tox::Vec3(xs[i], 0.0, zs[i]);
  const double l0 = measureClosedLoopLength(flat);
  const double targetLength = ranges.lengthMin + (ranges.lengthMax - ranges.lengthMin) * rnd.next();
  const double scale = l0 > 1e-6 ? targetLength / l0 : 1.0;
  for (int i = 0; i < n; ++i) {
    xs[i] *= scale;
    zs[i] *= scale;
  }

  // Smooth rolling hills: a few low-frequency sinusoids around the loop, summed and normalized so
  // elevation is continuous through the closed wrap. Applied after length calibration so maxHill
  // stays a true world-unit cap.
  const int harmonicCount = 2 + static_cast<int>(std::floor(rnd.next() * 3));
  struct Harmonic {
    double freq, phase, amp;
  };
  std::vector<Harmonic> harmonics(harmonicCount);
  double ampSum = 0.0;
  for (auto& h : harmonics) {
    h.freq = 1.0 + std::floor(rnd.next() * 4.0);
    h.phase = rnd.next() * 2.0 * std::numbers::pi;
    h.amp = 0.4 + 0.6 * rnd.next();
    ampSum += h.amp;
  }
  if (ampSum <= 0.0) ampSum = 1.0;
  const double hillAmp = ranges.maxHill * t;
  for (int i = 0; i < n; ++i) {
    const double frac = static_cast<double>(i) / n;
    double y = 0.0;
    for (const auto& h : harmonics) y += h.amp * std::sin(frac * 2.0 * std::numbers::pi * h.freq + h.phase);
    ys[i] = (y / ampSum) * hillAmp;
  }

  // Hills add a few % of 3D length on top of the flat calibration; one final uniform scale settles
  // the true driven length back into [lengthMin, lengthMax] (and keeps hill height at or under
  // maxHill, since the correction factor is <= 1).
  std::vector<tox::Vec3> full(n);
  for (int i = 0; i < n; ++i) full[i] = tox::Vec3(xs[i], ys[i], zs[i]);
  const double l3d = measureClosedLoopLength(full);
  const double corr = l3d > 1e-6 ? targetLength / l3d : 1.0;
  for (int i = 0; i < n; ++i) {
    xs[i] *= corr;
    zs[i] *= corr;
    ys[i] *= corr;
  }

  std::vector<tox::Vec3> loop(n);
  for (int i = 0; i < n; ++i) loop[i] = tox::Vec3(xs[i], ys[i], zs[i]);
  Path path = generatedPath("random-path-1", loop, rnd, ranges, t, false, true);

  TrackDefinition track;
  track.name = "Random Track";
  track.start = {0, 0, false};
  track.paths.push_back(path);

  const int boostCount =
      std::clamp(static_cast<int>(std::round(ranges.boostMin + (ranges.boostMax - ranges.boostMin) * t + (rnd.next() - 0.5) * 2.0)),
                ranges.boostMin, ranges.boostMax);
  for (int i = 0; i < boostCount; ++i) {
    Zone zone;
    zone.id = "random-boost-" + std::to_string(i + 1);
    zone.effect = "velocityChange";
    zone.width = 36.0 * 0.3;
    zone.length = 40.0;
    zone.factor = 1.5;
    zone.duration = 2.0;
    zone.host.kind = "path";
    zone.host.pathId = "random-path-1";
    zone.host.t = 0.1 + 0.8 * rnd.next();
    zone.host.lateral = 0.0;
    track.zones.push_back(std::move(zone));
  }
  return track;
}

}  // namespace editor
