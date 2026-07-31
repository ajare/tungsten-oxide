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

// Temporary route sampling used only to choose mesh cuts, unrelated to the authored path's own
// control density.
constexpr double kRouteControlSpacing = 250.0;

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

// Bakes `path` alone through core's real loader and reads
// the baked centerline's first/last frame. buildCenterline samples an open path's parameter range
// as (i/(N-1))*(CP_N-1), which lands EXACTLY on 0 and CP_N-1 at the array's own first/last index
// regardless of N -- so this is the same value the evaluator would give, not an approximation. See
// RandomTrack.hpp's header comment.
tox::Vec3 bakeOpenPathEndpoint(const Path& path, bool atEnd) {
  TrackDefinition probe;
  Path openPath = path;
  openPath.closed = false;
  probe.paths.push_back(std::move(openPath));
  const tox::TrackLoadResult loaded = tox::Track::fromJson(toJson(probe));
  if (!loaded || loaded.track->paths.empty()) return tox::Vec3(0.0, 0.0, 0.0);
  const auto& centerline = loaded.track->paths[0].centerline;
  if (centerline.empty()) return tox::Vec3(0.0, 0.0, 0.0);
  return atEnd ? centerline.back().pos : centerline.front().pos;
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

// Mirrors generatedPlatformAsset: a flat length x width rectangle with rails on its two long
// sides only (entry/exit stay open ledges).
MeshAsset generatedPlatformAsset(const std::string& id, const std::string& name, double length, double width) {
  MeshAsset asset;
  asset.id = id;
  asset.name = name;
  asset.railHeight = 6.0;  // TrackCore.DEFAULT_RAIL_HEIGHT
  const double hl = length / 2.0, hw = width / 2.0;
  asset.vertices = {{0, -hl, -hw}, {1, hl, -hw}, {2, hl, hw}, {3, -hl, hw}};
  asset.edges = {
      {0, 0, 1, true},   // y (width) constant at -hw: a long side -> rail
      {1, 1, 2, false},  // x (length) constant at +hl: the exit ledge
      {2, 2, 3, true},   // y constant at +hw: a long side -> rail
      {3, 3, 0, false},  // x constant at -hl: the entry ledge
  };
  MeshPolygon polygon;
  polygon.id = 0;
  polygon.edges = {{0, 0, 1}, {1, 1, 2}, {2, 2, 3}, {3, 3, 0}};
  asset.polygons = {polygon};
  return asset;
}

int sequencePlatformCount(Mulberry32& rnd) {
  const double r = rnd.next();
  return r < 0.5 ? 2 : (r < 0.8 ? 3 : 4);
}

std::vector<TrackPoint*> positionPointsInOrder(Path& path) {
  std::vector<TrackPoint*> out;
  for (auto& p : path.points)
    if (p.kind == PointKind::Position) out.push_back(&p);
  return out;
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

  // Temporary route controls for choosing well-spaced mesh endpoints (mirrors the `route` array).
  // Ordinary paths are simplified afterward; physics/rendering sample their own centerline and
  // don't need these authored every ~250m.
  std::vector<tox::Vec3> route;
  for (int i = 0; i < n; ++i) {
    const int j = (i + 1) % n;
    const double len = std::hypot(std::hypot(xs[j] - xs[i], ys[j] - ys[i]), zs[j] - zs[i]);
    const int steps = std::max(1, static_cast<int>(std::ceil(len / kRouteControlSpacing)));
    for (int k = 0; k < steps; ++k) {
      const double f = static_cast<double>(k) / steps;
      route.emplace_back(xs[i] + (xs[j] - xs[i]) * f, ys[i] + (ys[j] - ys[i]) * f, zs[i] + (zs[j] - zs[i]) * f);
    }
  }
  const int routeLen = static_cast<int>(route.size());

  const double meshChance = (ranges.meshChanceMin + (ranges.meshChanceMax - ranges.meshChanceMin) * t) / 100.0;
  int wantedSections = 0;
  for (int i = 0; i < ranges.maxMeshSections; ++i)
    if (rnd.next() < meshChance) ++wantedSections;

  // Candidate cuts are separated by at least 500m and stay clear of the first few controls
  // reserved for the starting grid; platforms are deliberately safe and aligned, so a candidate is
  // rejected unless its chord is close to both the outgoing and receiving path directions.
  struct Cut {
    int start, end, span;
    double drop{0.0};
  };
  std::vector<Cut> cuts;
  const int minOrdinarySteps = std::max(2, static_cast<int>(std::ceil(500.0 / kRouteControlSpacing)));
  for (int attempt = 0; attempt < 300 && static_cast<int>(cuts.size()) < wantedSections; ++attempt) {
    const int start = 4 + static_cast<int>(std::floor(rnd.next() * std::max(1, routeLen - 8)));
    const double desiredLength = ranges.meshLengthMin + (ranges.meshLengthMax - ranges.meshLengthMin) * rnd.next();
    const int span = std::max(1, std::min(routeLen - 2, static_cast<int>(std::round(desiredLength / kRouteControlSpacing))));
    const int end = (start + span) % routeLen;
    if (end <= start || end >= routeLen - 2) continue;  // keep cuts non-wrapping; the route itself still wraps

    auto alignment = [](double ax, double az, double bx, double bz) {
      const double al = std::hypot(ax, az) > 0.0 ? std::hypot(ax, az) : 1.0;
      const double bl = std::hypot(bx, bz) > 0.0 ? std::hypot(bx, bz) : 1.0;
      return (ax * bx + az * bz) / (al * bl);
    };
    const double bridgeX = route[end].x - route[start].x, bridgeZ = route[end].z - route[start].z;
    const double intoX = route[start].x - route[start - 2].x, intoZ = route[start].z - route[start - 2].z;
    const double outX = route[end + 2].x - route[end].x, outZ = route[end + 2].z - route[end].z;
    if (alignment(intoX, intoZ, bridgeX, bridgeZ) < 0.985 || alignment(bridgeX, bridgeZ, outX, outZ) < 0.985) continue;

    bool separated = true;
    for (const auto& c : cuts) {
      const int rawDiff = std::abs(start - c.start);
      const int dist = std::min(rawDiff, routeLen - rawDiff);
      if (dist < minOrdinarySteps + std::max(span, c.span)) {
        separated = false;
        break;
      }
    }
    if (separated) cuts.push_back({start, end, span});
  }
  std::sort(cuts.begin(), cuts.end(), [](const Cut& a, const Cut& b) { return a.start < b.start; });

  auto boostCountFor = [&]() {
    return std::clamp(static_cast<int>(std::round(ranges.boostMin + (ranges.boostMax - ranges.boostMin) * t + (rnd.next() - 0.5) * 2.0)),
                      ranges.boostMin, ranges.boostMax);
  };

  if (cuts.empty()) {
    std::vector<tox::Vec3> sparseLoop(n);
    for (int i = 0; i < n; ++i) sparseLoop[i] = tox::Vec3(xs[i], ys[i], zs[i]);
    Path path = generatedPath("random-path-1", sparseLoop, rnd, ranges, t, false, true);

    TrackDefinition track;
    track.name = "Random Track";
    track.start = {0, 0, false};
    track.paths.push_back(path);

    const int boostCount = boostCountFor();
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

  // Lower every receiving end and blend that drop into the first few ordinary controls. The
  // outgoing endpoint remains untouched and therefore meets the first horizontal platform flush.
  for (auto& cut : cuts) {
    const double drop = ranges.endDropMin + (ranges.endDropMax - ranges.endDropMin) * rnd.next();
    cut.drop = drop;
    const double original = route[cut.end].y;
    const double target = route[cut.start].y - drop;
    const double delta = target - original;
    for (int k = 0; k <= 4; ++k) {
      const int index = (cut.end + k) % routeLen;
      route[index].y += delta * (1.0 - k / 4.0);
    }
  }

  // One open ordinary path follows each cut: receiving end -> around the old loop -> next
  // outgoing end. Mesh/ramp structures reconnect those endpoints.
  std::vector<Path> paths;
  for (std::size_t i = 0; i < cuts.size(); ++i) {
    const int from = cuts[i].end, to = cuts[(i + 1) % cuts.size()].start;
    std::vector<tox::Vec3> coords;
    for (int at = from, guard = 0; guard <= routeLen; at = (at + 1) % routeLen, ++guard) {
      coords.push_back(route[at]);
      if (at == to) break;
    }
    paths.push_back(generatedPath("random-path-" + std::to_string(i + 1), coords, rnd, ranges, t, false, false));
  }

  // Settle each receiving end back to its section's authored drop by moving only its first
  // dedicated endpoint controls; the ordinary path is free to climb again afterward. Solves the
  // rational endpoint blend (two dedicated controls don't contribute with unit weight) with one
  // probe bake rather than assuming linearity analytically.
  for (std::size_t i = 0; i < cuts.size(); ++i) {
    Path& destination = paths[(i + 1) % cuts.size()];
    const tox::Vec3 outgoing = bakeOpenPathEndpoint(paths[i], true);
    const double desiredY = outgoing.y - cuts[(i + 1) % cuts.size()].drop;

    std::vector<TrackPoint*> controls = positionPointsInOrder(destination);
    int prefix = std::min<int>(2, static_cast<int>(controls.size()));
    // Do not put the endpoint correction boundary through a flattened tight-turn group. Extend
    // the shifted prefix until every such group is wholly inside or outside it, preserving the
    // no-grade-through-tight-turn invariant.
    bool extended = true;
    while (extended) {
      extended = false;
      for (int j = 1; j + 1 < static_cast<int>(controls.size()); ++j) {
        const tox::Vec3 &a = controls[j - 1]->pos, &b = controls[j]->pos, &c = controls[j + 1]->pos;
        const double ix = b.x - a.x, iz = b.z - a.z, ox = c.x - b.x, oz = c.z - b.z;
        const double il = std::hypot(ix, iz) > 0.0 ? std::hypot(ix, iz) : 1.0;
        const double ol = std::hypot(ox, oz) > 0.0 ? std::hypot(ox, oz) : 1.0;
        const double angle = std::acos(std::clamp((ix * ox + iz * oz) / (il * ol), -1.0, 1.0));
        if (angle >= std::numbers::pi / 8.0 && j - 1 < prefix && j + 1 >= prefix) {
          prefix = std::min<int>(static_cast<int>(controls.size()), j + 2);
          extended = true;
        }
      }
    }

    const double beforeY = bakeOpenPathEndpoint(destination, false).y;
    for (int j = 0; j < prefix; ++j) controls[j]->pos.y += 1.0;
    const double response = bakeOpenPathEndpoint(destination, false).y - beforeY;
    for (int j = 0; j < prefix; ++j) controls[j]->pos.y -= 1.0;
    const double shift = std::abs(response) > 1e-9 ? (desiredY - beforeY) / response : (desiredY - beforeY);
    for (int j = 0; j < prefix; ++j) controls[j]->pos.y += shift;
  }

  // Platforms (and, where the next surface is level or rising, a short launch ramp) bridge each
  // cut's outgoing/receiving endpoints.
  std::map<std::string, MeshAsset> meshAssets;
  std::vector<MeshPlacement> meshes;
  struct PlatformRecord {
    MeshPlacement placement;
    double length, width;
  };
  std::vector<PlatformRecord> platformRecords;
  int rampNumber = 0, meshNumber = 0;

  for (std::size_t i = 0; i < cuts.size(); ++i) {
    const tox::Vec3 a = bakeOpenPathEndpoint(paths[i], true);
    const tox::Vec3 b = bakeOpenPathEndpoint(paths[(i + 1) % cuts.size()], false);
    const double dx = b.x - a.x, dz = b.z - a.z;
    const double distance = std::hypot(dx, dz) > 0.0 ? std::hypot(dx, dz) : 1.0;
    const double ux = dx / distance, uz = dz / distance;
    const double rotation = std::atan2(uz, ux) * 180.0 / std::numbers::pi;
    const bool isSequence = rnd.next() < ranges.sequenceChance / 100.0;
    const int count = isSequence ? sequencePlatformCount(rnd) : 1;
    const double platformWidth = std::max(36.0 * 2.0, ranges.widthMax);
    // Reserve enough total empty distance for safe jumps, then distribute it equally before,
    // between, and after the platforms.
    const double totalGap = std::max(30.0, std::min(115.0, distance - count * 18.0));
    const double platformLength = std::max(18.0, (distance - totalGap) / count);
    const double gap = std::max(0.0, (distance - platformLength * count) / (count + 1));

    std::vector<double> levels(count);
    for (int j = 0; j < count; ++j) {
      const double f = static_cast<double>(j) / count;
      const double trend = a.y + (b.y - a.y) * f;
      levels[j] = (j == 0) ? a.y : trend + (rnd.next() * 2.0 - 1.0) * 15.0;
    }
    // Whatever rises/falls happen inside the sequence, its final launch must be above the
    // receiving path so a flat or ramp-assisted exit can land rather than jumping upward.
    levels[count - 1] = b.y + 8.0;

    double cursor = gap;
    for (int j = 0; j < count; ++j) {
      const double length = platformLength;
      const double centerAlong = cursor + length / 2.0;
      const std::string assetId = "random-platform-asset-" + std::to_string(++meshNumber);
      const std::string meshId = "random-platform-" + std::to_string(meshNumber);
      meshAssets.emplace(assetId,
                         generatedPlatformAsset(assetId, "Platform " + std::to_string(meshNumber), length, platformWidth));

      MeshPlacement placement;
      placement.id = meshId;
      placement.assetId = assetId;
      placement.x = a.x + ux * centerAlong;
      placement.z = a.z + uz * centerAlong;
      placement.rotation = rotation;
      placement.elevation = levels[j];
      meshes.push_back(placement);
      platformRecords.push_back({placement, length, platformWidth});

      const double nextLevel = (j + 1 < count) ? levels[j + 1] : b.y;
      if (nextLevel >= levels[j] - 0.5) {
        // Horizontal platforms cannot provide upward launch velocity. A short open spline ramp
        // starts at the ledge and projects most of the way across the evenly-spaced gap toward
        // the next surface.
        const double rampLength = std::min(28.0, gap * 0.75);
        const double rise = std::min(12.0, std::max(3.0, nextLevel - levels[j] + 3.0));
        std::vector<tox::Vec3> ramp;
        for (int q = 0; q < 4; ++q) {
          const double f = static_cast<double>(q) / 3.0;
          const double along = cursor + length + rampLength * f;
          ramp.emplace_back(a.x + ux * along, levels[j] + rise * f, a.z + uz * along);
        }
        paths.push_back(generatedPath("random-ramp-" + std::to_string(++rampNumber), ramp, rnd, ranges, t, true, false));
      }
      cursor += length + gap;
    }
  }

  const int startPointCount =
      static_cast<int>(std::count_if(paths[0].points.begin(), paths[0].points.end(), [](const TrackPoint& p) { return p.kind == PointKind::Position; }));
  Start start;
  start.path = 0;
  start.point = std::max(1, std::min(startPointCount - 2, startPointCount / 3));
  start.reverse = false;

  std::vector<Zone> zones;
  const int boostCount = boostCountFor();
  std::vector<const Path*> ordinaryPaths;
  for (const auto& p : paths)
    if (p.id.rfind("random-ramp-", 0) != 0) ordinaryPaths.push_back(&p);
  std::vector<const PlatformRecord*> boostablePlatforms;
  for (const auto& p : platformRecords)
    if (p.length >= 40.0 + 8.0) boostablePlatforms.push_back(&p);  // DEFAULT_ZONE_LENGTH + 8

  for (int i = 0; i < boostCount; ++i) {
    const bool useMesh = !boostablePlatforms.empty() && rnd.next() < 0.3;
    Zone zone;
    zone.id = "random-boost-" + std::to_string(i + 1);
    zone.effect = "velocityChange";
    zone.factor = 1.5;
    zone.duration = 2.0;
    if (useMesh) {
      const PlatformRecord& p = *boostablePlatforms[static_cast<std::size_t>(std::floor(rnd.next() * boostablePlatforms.size()))];
      zone.width = p.width * 0.3;
      zone.length = std::min(40.0, p.length - 4.0);
      zone.host.kind = "mesh";
      zone.host.meshId = p.placement.id;
      zone.host.x = p.placement.x;
      zone.host.z = p.placement.z;
      zone.host.rotation = p.placement.rotation;
    } else {
      const Path& path = *ordinaryPaths[static_cast<std::size_t>(std::floor(rnd.next() * ordinaryPaths.size()))];
      zone.width = 36.0 * 0.3;
      zone.length = 40.0;
      zone.host.kind = "path";
      zone.host.pathId = path.id;
      zone.host.t = 0.15 + 0.7 * rnd.next();
      zone.host.lateral = 0.0;
    }
    zones.push_back(std::move(zone));
  }

  // Intermediate checkpoints only -- see RandomTrack.hpp's header comment on the auto-finish gap.
  std::vector<Trigger> triggers;
  for (std::size_t i = 0; i < cuts.size(); ++i) {
    Trigger trigger;
    trigger.id = "random-checkpoint-" + std::to_string(i + 1);
    trigger.type = "checkpoint";
    trigger.role = "intermediate";
    trigger.direction = "forward";
    trigger.width = 40.0;   // TrackCore.DEFAULT_TRIGGER_WIDTH
    trigger.height = 12.0;  // TrackCore.DEFAULT_TRIGGER_HEIGHT
    trigger.rotation = 0.0;
    trigger.host.kind = "path";
    trigger.host.pathId = paths[(i + 1) % cuts.size()].id;
    trigger.host.t = 0.08;
    triggers.push_back(std::move(trigger));
  }

  TrackDefinition track;
  track.name = "Random Track";
  track.start = start;
  track.meshAssets = std::move(meshAssets);
  track.meshes = std::move(meshes);
  track.zones = std::move(zones);
  track.triggers = std::move(triggers);
  track.paths = std::move(paths);
  return track;
}

}  // namespace editor
