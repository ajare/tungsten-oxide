#include "RandomTrack.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "Track.hpp"

namespace editor {
namespace {

// Bit-exact port of editor.js's mulberry32: uint32_t wraparound arithmetic reproduces JS's
// `|0`/Math.imul/`>>>` exactly for this particular operation sequence (XOR, add, multiply all
// wrap identically whether the bit pattern is read as signed or unsigned).
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
    length += a.distanceTo(b);
  }
  return length;
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

  Path path;
  path.id = "random-path-1";
  path.closed = true;
  std::vector<tox::Vec3> coords(n);
  for (int i = 0; i < n; ++i) coords[i] = tox::Vec3(xs[i], ys[i], zs[i]);
  for (int i = 0; i < n; ++i) {
    TrackPoint point;
    point.kind = PointKind::Position;
    point.id = "random-path-1-p-" + std::to_string(i + 1);
    point.pos = coords[i];
    point.weight = 1.0;
    path.points.push_back(point);
  }
  for (int i = 0; i < n; ++i) {
    const tox::Vec3& prev = coords[(i - 1 + n) % n];
    const tox::Vec3& cur = coords[i];
    const tox::Vec3& next = coords[(i + 1) % n];
    const double inx = cur.x - prev.x, inz = cur.z - prev.z;
    const double outx = next.x - cur.x, outz = next.z - cur.z;
    const double inLen = std::hypot(inx, inz) > 0.0 ? std::hypot(inx, inz) : 1.0;
    const double outLen = std::hypot(outx, outz) > 0.0 ? std::hypot(outx, outz) : 1.0;
    const double m = std::clamp((inz * outx - inx * outz) / (inLen * outLen), -1.0, 1.0);
    const double roll = std::clamp((std::asin(m) / 0.6) * ranges.maxBanking, -ranges.maxBanking, ranges.maxBanking) * t;

    const double frac = static_cast<double>(i) / n;
    TrackPoint rollPoint;
    rollPoint.kind = PointKind::Roll;
    rollPoint.t = frac;
    rollPoint.roll = roll;
    path.points.push_back(rollPoint);

    const double widthSample = ranges.widthMin + (ranges.widthMax - ranges.widthMin) * rnd.next();
    TrackPoint widthPoint;
    widthPoint.kind = PointKind::Width;
    widthPoint.t = frac;
    widthPoint.width = std::max(1.0, 36.0 + (widthSample - 36.0) * t);
    path.points.push_back(widthPoint);

    TrackPoint crossSectionPoint;
    crossSectionPoint.kind = PointKind::CrossSection;
    crossSectionPoint.t = frac;
    crossSectionPoint.curvature = -rnd.next() * ranges.maxCurvature * t;
    crossSectionPoint.tightness = 1.0;
    crossSectionPoint.thickness = 4.0;
    path.points.push_back(crossSectionPoint);
  }

  TrackDefinition track;
  track.name = "Random Track";
  track.start = {0, 0, false};
  track.paths.push_back(std::move(path));

  const int boostCount = std::clamp(
      static_cast<int>(std::round(ranges.boostMin + (ranges.boostMax - ranges.boostMin) * t + (rnd.next() - 0.5) * 2.0)), ranges.boostMin,
      ranges.boostMax);
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
