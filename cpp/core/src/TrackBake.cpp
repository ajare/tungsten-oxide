// TrackBake.cpp — current-schema authored spline paths to world-space physics
// frames and graphics-API-agnostic triangle batches.
#include "TrackBake.hpp"
#include "Simulation.hpp"
#include "TrackCore.hpp"
#include "TrackMesh.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>

namespace tox {
namespace {
constexpr double PI = 3.14159265358979323846, DEG2RAD = PI / 180.0;
struct Parts {
  std::vector<const TrackPointDefinition*> cp, roll, width, cross;
};
Parts split(const PathDefinition& p) {
  Parts s;
  for (auto& x : p.points) {
    if (x.kind == TrackPointKind::Position)
      s.cp.push_back(&x);
    else if (x.kind == TrackPointKind::Roll)
      s.roll.push_back(&x);
    else if (x.kind == TrackPointKind::Width)
      s.width.push_back(&x);
    else
      s.cross.push_back(&x);
  }
  auto byT = [](auto* a, auto* b) { return a->t < b->t; };
  std::sort(s.roll.begin(), s.roll.end(), byT);
  std::sort(s.width.begin(), s.width.end(), byT);
  std::sort(s.cross.begin(), s.cross.end(), byT);
  return s;
}
double scalar(const std::vector<const TrackPointDefinition*>& p, bool closed, double tq, const std::function<double(const TrackPointDefinition&)>& val) {
  int m = (int)p.size();
  if (m == 1) return val(*p[0]);
  double t = tq;
  if (closed)
    t = std::fmod(std::fmod(t, 1.0) + 1.0, 1.0);
  else
    t = std::max(p.front()->t, std::min(p.back()->t, t));
  struct TV {
    double t, v;
  };
  auto at = [&](int i) {
    if (closed) {
      int k = ((i % m) + m) % m;
      int cycle = (i - k) / m;
      return TV{p[k]->t + cycle, val(*p[k])};
    }
    int k = std::max(0, std::min(m - 1, i));
    return TV{p[k]->t, val(*p[k])};
  };
  int i = closed ? m - 1 : m - 2;
  for (int k = 0; k < m - 1; k++)
    if (t >= p[k]->t && t < p[k + 1]->t) {
      i = k;
      break;
    }
  auto p1 = at(i), p2 = at(i + 1);
  double tt = t;
  if (tt < p1.t) tt += 1;
  double dt = (p2.t - p1.t);
  if (dt == 0) dt = 1e-6;
  double u = (tt - p1.t) / dt;
  auto p0 = at(i - 1), p3 = at(i + 2);
  double d10 = p2.t - p0.t;
  if (d10 == 0) d10 = 1e-6;
  double d21 = p3.t - p1.t;
  if (d21 == 0) d21 = 1e-6;
  double m1 = (p2.v - p0.v) / d10 * dt, m2 = (p3.v - p1.v) / d21 * dt, u2 = u * u, u3 = u2 * u;
  return (2 * u3 - 3 * u2 + 1) * p1.v + (u3 - 2 * u2 + u) * m1 + (-2 * u3 + 3 * u2) * p2.v + (u3 - u2) * m2;
}
struct Sample0 {
  Vec3 pos, tangent;
  double roll, width, curv, tight, thick;
};
class Evaluator {
public:
  Parts p;
  bool closed;
  int n;
  Evaluator(const PathDefinition& d) : p(split(d)), closed(d.closed), n((int)p.cp.size()) {}
  Sample0 eval(double g) const {
    auto roll = [&](double t) { return scalar(p.roll, closed, t, [](auto& x) { return x.roll; }) * DEG2RAD; };
    auto width = [&](double t) { return std::max(1.0, scalar(p.width, closed, t, [](auto& x) { return x.width; })); };
    auto xs = [&](double t) {
      return std::array<double, 3>{
          TrackCore::clampSignedUnit(scalar(p.cross, closed, t, [](auto& x) { return x.curvature; })),
          TrackCore::clampTightness(scalar(p.cross, closed, t, [](auto& x) { return x.tightness; })),
          std::max(0.0, scalar(p.cross, closed, t, [](auto& x) { return x.thickness; }))};
    };
    double gmax = (closed ? n : n - 1);
    if (gmax == 0) gmax = 1;
    if (!closed && g <= 0) {
      auto q = xs(0);
      Vec3 t = n > 1 ? p.cp[1]->pos.clone().sub(p.cp[0]->pos).normalize() : Vec3(0, 0, 1);
      return {p.cp[0]->pos, t, roll(0), width(0), q[0], q[1], q[2]};
    }
    if (!closed && g >= n - 1) {
      auto q = xs(1);
      Vec3 t = n > 1 ? p.cp[n - 1]->pos.clone().sub(p.cp[n - 2]->pos).normalize() : Vec3(0, 0, 1);
      return {p.cp[n - 1]->pos, t, roll(1), width(1), q[0], q[1], q[2]};
    }
    int seg = (int)std::floor(g);
    double u = g - seg, u2 = u * u, u3 = u2 * u;
    double b[4] = {(1 - 3 * u + 3 * u2 - u3) / 6, (4 - 6 * u2 + 3 * u3) / 6, (1 + 3 * u + 3 * u2 - 3 * u3) / 6, u3 / 6};
    double db[4] = {(-3 + 6 * u - 3 * u2) / 6, (-12 * u + 9 * u2) / 6, (3 + 6 * u - 9 * u2) / 6, (3 * u2) / 6};
    auto wrap = [&](int i) { return closed ? ((i % n) + n) % n : std::max(0, std::min(n - 1, i)); };
    int ids[4] = {wrap(seg - 1), wrap(seg), wrap(seg + 1), wrap(seg + 2)};
    Vec3 num, dnum;
    double den = 0, dden = 0;
    for (int k = 0; k < 4; k++) {
      double w = p.cp[ids[k]]->weight, bw = b[k] * w, dbw = db[k] * w;
      num.addScaledVector(p.cp[ids[k]]->pos, bw);
      dnum.addScaledVector(p.cp[ids[k]]->pos, dbw);
      den += bw;
      dden += dbw;
    }
    Vec3 pos = num.clone().multiplyScalar(1 / den);
    Vec3 tan = dnum.clone().multiplyScalar(den).addScaledVector(num, -dden).multiplyScalar(1 / (den * den)).normalize();
    double t = g / gmax;
    auto q = xs(t);
    return {pos, tan, roll(t), width(t), q[0], q[1], q[2]};
  }
};
Frame frame(const Sample0& s) {
  Vec3 h;
  h.crossVectors(Vec3(0, 1, 0), s.tangent).normalize();
  Vec3 bn;
  bn.crossVectors(s.tangent, h).normalize();
  if (bn.y < 0) bn.negate();
  double c = std::cos(-s.roll), si = std::sin(-s.roll);
  Vec3 er = h.clone().multiplyScalar(c).addScaledVector(bn, si);
  Vec3 no = bn.clone().multiplyScalar(c).addScaledVector(h, -si).normalize();
  Frame f;
  f.pos = s.pos;
  f.tangent = s.tangent;
  f.h = h;
  f.edgeRight = er;
  f.normal = no;
  f.roll = s.roll;
  f.width = s.width;
  f.halfW = s.width / 2;
  f.crossSectionCurvature = s.curv;
  f.crossSectionTightness = s.tight;
  f.crossSectionThickness = s.thick;
  return f;
}
// Two-segment cubic Hermite through (t0,v0)-(mid,vMid)-(t1,v1), with explicit derivatives (true
// dv/dt, not pre-scaled) at each of the three points shared by both segments. Used below only to
// build the *base* taper (0 at both ends), so it reproduces the original shared `scalar()`
// helper's three-point Catmull-Rom output bit-for-bit -- the boundary derivative it passes is
// exactly `scalar()`'s own duplicated-endpoint one-sided secant, and the interior derivative is
// the same central-difference secant across the whole span.
double hermitePair(double t, double t0, double v0, double m0, double mid, double vMid, double mMid, double t1, double v1, double m1) {
  auto segment = [](double u0, double v0, double m0, double u1, double v1, double m1, double t) {
    const double dt = u1 - u0;
    if (dt < 1e-9) return v0;
    const double u = (t - u0) / dt, u2 = u * u, u3 = u2 * u;
    return (2 * u3 - 3 * u2 + 1) * v0 + (u3 - 2 * u2 + u) * dt * m0 + (-2 * u3 + 3 * u2) * v1 + (u3 - u2) * dt * m1;
  };
  return t <= mid ? segment(t0, v0, m0, mid, vMid, mMid, t) : segment(mid, vMid, mMid, t1, v1, m1, t);
}
// A Rounded end's profile: the void's boundary sweeps a half-ellipse, `capWidth` across and
// `noseLength` along the path, so the end closes as a dome rather than a flat cut. `d` is distance
// in METRES back along the path from the end -- shaping this in world units rather than in t is
// what makes it an actual dome instead of a t-space lookalike, since a reservation's length and
// its nose are nowhere near proportional.
//
// The vertical tangent at d = 0 is the whole point: it's what a rounded nose looks like, and what
// distinguishes this from Mitred. (Two earlier attempts failed here -- forcing a zero *slope* at
// the tip via Hermite tangents, then a smoothstep crossfade across the half-span. Both left the
// profile flat at the tip, which is exactly what Mitred's shelf looks like, so the two styles were
// indistinguishable.) At d = noseLength it reaches capWidth with a horizontal tangent, blending
// into the flat run without a corner.
//
// noseLength defaults to capWidth/2 (a true half-circle) when unset, but is otherwise independent:
// see tox::ReservationEndCap for why a circle alone is too small to see at track zoom.
double roundedNoseLength(const ReservationEndCap& cap, double capWidth) {
  return cap.noseLength > 0.0 ? cap.noseLength : capWidth / 2.0;
}
double roundedNoseWidth(double d, double capWidth, double noseLength) {
  if (capWidth <= 1e-9 || noseLength <= 1e-9) return 0.0;
  if (d >= noseLength) return capWidth;
  const double x = 1.0 - std::max(0.0, d) / noseLength;  // 1 at the tip, 0 at the dome's base
  return capWidth * std::sqrt(std::max(0.0, 1.0 - x * x));
}
// Half-width of the central-reservation void at `t` (path-fraction domain, same as width/roll/
// cross-section points), clamped so at least a sliver of each lane always remains. Reservations
// are authored non-overlapping (EditorState's job, CENTRAL_RESERVATION_PLAN.md M3), so the first
// span containing `t` is authoritative. `roadWidth` is the road's own (un-carved) width at this
// same `t` -- already what the caller has on hand, being the very frame this gap is being applied
// to -- and `pathLength` is the path's driven length in metres, needed only to size a Rounded end's
// nose (see above).
//
// Built in three steps -- CENTRAL_RESERVATION_PLAN.md's Fixed-vs-Percent width decision, plus its
// Mitred-vs-Rounded end-cap decision:
// 1. `peakHere`: the reservation's own peak width, evaluated AT `t`. Fixed mode holds this at a
//    constant metres value (`r.width`) everywhere, same as the reservation's original,
//    single-`width`-only behavior. Percent mode instead re-derives it every call as a fraction of
//    `roadWidth` -- so if the road itself narrows or widens across the reservation's span, the
//    void's peak tracks it rather than staying fixed.
// 2. `baseWidth`: the plain 0 -> 1 -> 0 taper shape (a Hermite curve through (t0,0)-(mid,1)-(t1,0),
//    unitless -- built with a peak of 1 rather than `r.width` so the same curve serves both width
//    modes) scaled by `peakHere`. Hermite interpolation is linear in its value/derivative
//    parameters, so scaling every one of them by a constant scales the whole curve by that same
//    constant -- meaning this is bit-identical to evaluating the old width-scaled curve directly
//    whenever `peakHere` doesn't vary with `t` (i.e. Fixed mode, always).
// 3. Each end whose style isn't Joined floors its half of the span against that end's cap, clamped
//    to `peakHere` (not `r.width`, which in Percent mode is a 0-100 number, not metres) so a cap
//    never flares wider than the reservation's own local peak:
//    - Mitred with a hard max(baseWidth, capWidth) -- a flat shelf wherever baseWidth would have
//      gone narrower than the cap, ending in a blunt cut of exactly capWidth at the end itself.
//    - Rounded with that same shelf, except its last `noseLength` metres are replaced by the
//      elliptical dome above, closing the void to a point instead of cutting it off square.
struct ReservationGap {
  double halfWidth{0.0};
  int index{-1};
};
ReservationGap reservationHalfGapAt(const PathDefinition& def, double t, double roadWidth, double pathLength) {
  for (int i = 0; i < static_cast<int>(def.reservations.size()); ++i) {
    const auto& r = def.reservations[i];
    if (t < r.t0 || t > r.t1) continue;
    const double mid = (r.t0 + r.t1) / 2;
    const double dt = std::max(1e-9, mid - r.t0);
    const double peakHere = r.widthMode == ReservationWidthMode::Percent ? (r.width / 100.0) * roadWidth : r.width;
    const double taper = std::max(0.0, hermitePair(t, r.t0, 0.0, 1.0 / dt, mid, 1.0, 0.0, r.t1, 0.0, -1.0 / dt));
    const double baseWidth = taper * peakHere;

    const bool firstHalf = t <= mid;
    const ReservationEndCap& cap = firstHalf ? r.endCap0 : r.endCap1;
    const double capWidth = std::min(cap.width, peakHere);
    double gapWidth = baseWidth;
    if (cap.style == ReservationEndCapStyle::Mitred) {
      gapWidth = std::max(baseWidth, capWidth);
    } else if (cap.style == ReservationEndCapStyle::Rounded) {
      const double d = (firstHalf ? t - r.t0 : r.t1 - t) * std::max(0.0, pathLength);
      gapWidth = std::max(baseWidth, roundedNoseWidth(d, capWidth, roundedNoseLength(cap, capWidth)));
    }

    constexpr double kMinLaneWidth = 1.0;
    return {std::max(0.0, std::min(gapWidth / 2.0, roadWidth / 2.0 - kMinLaneWidth)), i};
  }
  return {};
}
void applyReservationGap(Frame& f, const PathDefinition& p, double t, double pathLength) {
  const ReservationGap gap = reservationHalfGapAt(p, t, f.width, pathLength);
  f.reservationHalfGap = gap.halfWidth;
  f.reservationIndex = gap.index;
}
// The centerline without any reservation carving applied. Split out from center() below purely to
// break a cycle: sizing a Rounded end's nose needs the path's length in metres, and length() is
// itself measured off the centerline. Positions don't depend on the carving at all
// (applyReservationGap only writes reservationHalfGap/reservationIndex), so measuring from this is
// identical to measuring from a fully-carved centerline.
std::vector<Frame> centerRaw(const PathDefinition& p, int N) {
  Evaluator e(p);
  std::vector<Frame> o;
  o.reserve(N);
  for (int i = 0; i < N; i++) {
    double g = p.closed ? (double(i) / N) * e.n : (N > 1 ? (double(i) / (N - 1)) * (e.n - 1) : 0);
    o.push_back(frame(e.eval(g)));
  }
  return o;
}
double length(const PathDefinition& p) {
  auto f = centerRaw(p, 200);
  double n = 0;
  for (size_t i = 1; i < f.size(); i++) n += f[i].pos.distanceTo(f[i - 1].pos);
  if (p.closed) n += f.front().pos.distanceTo(f.back().pos);
  return n;
}
std::vector<Frame> center(const PathDefinition& p, int N) {
  std::vector<Frame> o = centerRaw(p, N);
  const double pathLength = length(p);
  for (int i = 0; i < N; i++) {
    const double t = p.closed ? double(i) / N : (N > 1 ? double(i) / (N - 1) : 0);
    applyReservationGap(o[i], p, t, pathLength);
  }
  return o;
}
int sampleCount(const PathDefinition& p) { return std::max(400, std::min(2000, (int)std::round(length(p) / 6))); }
struct Edges {
  std::vector<Vec3> left, right;
};
std::optional<Vec3> lineX(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) {
  double den = (a.x - b.x) * (c.z - d.z) - (a.z - b.z) * (c.x - d.x);
  if (std::fabs(den) < 1e-9) return {};
  double t = ((a.x - c.x) * (c.z - d.z) - (a.z - c.z) * (c.x - d.x)) / den;
  return Vec3(a.x + t * (b.x - a.x), (b.y + c.y) / 2, a.z + t * (b.z - a.z));
}
std::vector<Vec3> trim(std::vector<Vec3> pts, const std::vector<Frame>& f, bool closed) {
  int N = (int)pts.size(), sc = closed ? N : N - 1;
  if (sc <= 0) return pts;
  auto nx = [&](int i) { return closed ? (i + 1) % N : i + 1; };
  std::vector<bool> fw(sc);
  for (int i = 0; i < sc; i++) fw[i] = ((pts[nx(i)].x - pts[i].x) * f[i].tangent.x + (pts[nx(i)].z - pts[i].z) * f[i].tangent.z) > 0;
  int start = 0;
  if (closed) {
    start = -1;
    for (int i = 0; i < sc; i++)
      if (fw[i]) {
        start = i;
        break;
      }
    if (start < 0) return pts;
  }
  for (int i = 0; i < sc;) {
    int s = closed ? (start + i) % sc : i;
    if (fw[s]) {
      i++;
      continue;
    }
    int len = 0;
    while ((closed ? len < sc : i + len < sc) && !fw[closed ? (start + i + len) % sc : i + len]) len++;
    int e = closed ? (start + i + len - 1) % sc : i + len - 1, pr = closed ? (s - 1 + N) % N : std::max(0, s - 1), af = closed ? (e + 2) % N : std::min(N - 1, e + 2), last = nx(e);
    Vec3 mid((pts[s].x + pts[last].x) / 2, (pts[s].y + pts[last].y) / 2, (pts[s].z + pts[last].z) / 2);
    Vec3 x = lineX(pts[pr], pts[s], pts[last], pts[af]).value_or(mid);
    if (std::hypot(x.x - mid.x, x.z - mid.z) > 6 * f[s].halfW) x = mid;
    for (int v = s;; v = closed ? (v + 1) % N : v + 1) {
      pts[v] = x;
      if (v == last) break;
    }
    i += len;
  }
  return pts;
}
bool segmentsCross(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) {
  auto cross = [](const Vec3& o, const Vec3& a, const Vec3& p) { return (a.x - o.x) * (p.z - o.z) - (a.z - o.z) * (p.x - o.x); };
  const double d1 = cross(a, b, c), d2 = cross(a, b, d), d3 = cross(c, d, a), d4 = cross(c, d, b);
  return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}
std::vector<Vec3> removeSelfLoops(std::vector<Vec3> points, const PathDefinition& path, const char* side, bool wrapOpen,
                                  const std::vector<SelfIntersectionOverrideDefinition>& overrides,
                                  std::vector<SelfIntersection>* outCrossings) {
  const bool circular = path.closed || wrapOpen;
  const int n = static_cast<int>(points.size()), segmentCount = circular ? n : n - 1;
  if (segmentCount < 4) return points;
  auto next = [&](int i) { return circular ? (i + 1) % n : i + 1; };
  const Parts parts = split(path);
  auto controlId = [&](int frameIndex) {
    const int count = static_cast<int>(parts.cp.size());
    const double g = path.closed ? static_cast<double>(frameIndex) / n * count : static_cast<double>(frameIndex) / (n - 1) * (count - 1);
    int index = static_cast<int>(std::round(g));
    index = path.closed ? ((index % count) + count) % count : std::max(0, std::min(count - 1, index));
    return parts.cp[index]->id;
  };
  auto decision = [&](int i, int j, int span) {
    std::string a = controlId(i), b = controlId(j);
    if (a > b) std::swap(a, b);
    for (const auto& override : overrides) {
      std::string oa = override.a, ob = override.b;
      if (oa > ob) std::swap(oa, ob);
      if (override.side == side && oa == a && ob == b) return override.action == "collapse";
    }
    return span <= TrackCore::DEFAULT_SELF_INTERSECTION_SPAN;
  };
  // Full, UNBOUNDED pairwise scan on the RAW (pre-collapse) points: finds
  // every self-intersection this edge has, regardless of span, so the editor can show/cycle markers
  // for far ("auto-keep") crossings too, not just the near ones the bounded collapse pass below
  // actually acts on -- a separate, unbounded scan from removeLocalSelfIntersectionLoops's own
  // bounded one. Only run when
  // the caller wants it (`outCrossings != nullptr`) -- this is the expensive O(segmentCount^2) half
  // of this function; the editor skips it while a drag is in progress and reuses its last result
  // (see Track::fromJson's `detectSelfIntersections` parameter).
  if (outCrossings != nullptr) {
    for (int i = 0; i < segmentCount; ++i)
      for (int j = i + 2; j < segmentCount; ++j) {
        if (circular && i == 0 && j == segmentCount - 1) continue;
        if (!segmentsCross(points[i], points[next(i)], points[j], points[next(j)])) continue;
        const int forward = j - i, backward = circular ? segmentCount - forward : 1000000000;
        const int span = std::min(forward, backward);
        std::string a = controlId(i), b = controlId(j);
        if (a > b) std::swap(a, b);
        Vec3 point = lineX(points[i], points[next(i)], points[j], points[next(j)]).value_or(points[next(i)]);
        outCrossings->push_back(SelfIntersection{side, std::move(a), std::move(b), span, std::move(point)});
      }
  }
  const bool scanAll = std::any_of(overrides.begin(), overrides.end(), [&](const auto& override) {
    return override.side == side && override.action == "collapse";
  });
  for (int pass = 0; pass < segmentCount; ++pass) {
    bool found = false;
    int fi = 0, fj = 0;
    bool wrapped = false;
    for (int i = 0; i < segmentCount && !found; ++i)
      for (int j = i + 2; j < segmentCount; ++j) {
        if (circular && i == 0 && j == segmentCount - 1) continue;
        const int forward = j - i, backward = circular ? segmentCount - forward : 1000000000;
        const int span = std::min(forward, backward);
        if ((!scanAll && span > TrackCore::DEFAULT_SELF_INTERSECTION_SPAN) || !segmentsCross(points[i], points[next(i)], points[j], points[next(j)]) ||
            !decision(i, j, span))
          continue;
        found = true;
        fi = i;
        fj = j;
        wrapped = backward < forward;
        break;
      }
    if (!found) break;
    Vec3 x = lineX(points[fi], points[next(fi)], points[fj], points[next(fj)]).value_or(points[next(fi)]);
    int v = wrapped ? next(fj) : next(fi), stop = wrapped ? fi : fj;
    while (true) {
      points[v] = x;
      if (v == stop) break;
      v = next(v);
    }
  }
  return points;
}
Edges edges(const std::vector<Frame>& f, bool closed) {
  Edges e;
  for (auto& x : f) {
    e.left.push_back(x.pos.clone().addScaledVector(x.edgeRight, -x.halfW));
    e.right.push_back(x.pos.clone().addScaledVector(x.edgeRight, x.halfW));
  }
  e.left = trim(e.left, f, closed);
  e.right = trim(e.right, f, closed);
  return e;
}
void wallOffsets(std::vector<Frame>& f, const Edges& e) {
  for (size_t i = 0; i < f.size(); i++) {
    auto& x = f[i];
    x.sLeft = e.left[i].clone().sub(x.pos).dot(x.edgeRight);
    x.sRight = e.right[i].clone().sub(x.pos).dot(x.edgeRight);
  }
}
std::vector<double> crossBreak(double c, double k, double w) {
  std::set<double> b{0, 1};
  if (c == 0) return {0, 1};
  std::function<void(double, double, int)> split = [&](double a, double d, int depth) {
    double middle = (a + d) / 2;
    double actual = TrackCore::crossSectionHeight(c, k, middle, w);
    double chord = (TrackCore::crossSectionHeight(c, k, a, w) + TrackCore::crossSectionHeight(c, k, d, w)) / 2;
    if (depth < 5 && std::fabs(actual - chord) > 0.1) {
      split(a, middle, depth + 1);
      b.insert(middle);
      split(middle, d, depth + 1);
    }
  };
  split(0, .5, 0);
  b.insert(.5);
  split(.5, 1, 0);
  return {b.begin(), b.end()};
}
Vec3 surface(const Frame& f, const Vec3& l, const Vec3& r, double v) {
  Vec3 ch = r.clone().sub(l);
  double w = ch.length();
  return l.clone().addScaledVector(ch, v).addScaledVector(f.normal, TrackCore::crossSectionHeight(f.crossSectionCurvature, f.crossSectionTightness, v, w));
}
Vec3 triNormal(const Vec3& a, const Vec3& b, const Vec3& c) {
  Vec3 n;
  // cross(c-a, b-a), not cross(b-a, c-a) -- see TrackMesh.cpp's normalOf() for why this operand
  // order (not the naively expected one) is the one that actually points outward/up rather than
  // into the ground, for the vertex order Builder::tri()'s callers use.
  n.crossVectors(c.clone().sub(a), b.clone().sub(a)).normalize();
  if (n.lengthSq() == 0) n.set(0, 1, 0);
  return n;
}
struct RenderBake {
  std::vector<Frame> frames;
  Edges edges;
};
RenderBake adaptiveRenderBake(const PathDefinition& definition, const std::vector<Frame>& raw, const Edges& sourceEdges) {
  const int n = static_cast<int>(raw.size());
  if (n < 3) return {raw, sourceEdges};
  std::vector<bool> affected(n, false);
  for (int i = 0; i < n; ++i) {
    Vec3 left = raw[i].pos.clone().addScaledVector(raw[i].edgeRight, -raw[i].halfW);
    Vec3 right = raw[i].pos.clone().addScaledVector(raw[i].edgeRight, raw[i].halfW);
    affected[i] = left.distanceTo(sourceEdges.left[i]) > 1e-6 || right.distanceTo(sourceEdges.right[i]) > 1e-6;
  }
  Evaluator evaluator(definition);
  const double gmax = definition.closed ? evaluator.n : std::max(1, evaluator.n - 1);
  const double pathLength = length(definition);

  // A reservation's gap is invisible to the chord-tolerance `breaks()` below (it only looks at
  // centerline position, not cross-section carving), so left to itself a span gets rings wherever
  // the *centerline* needs them and none where the *gap boundary* does -- a straight reservation
  // collapses to its two raw endpoints with nothing in between to carve at all.
  //
  // Resolve each span on its own terms instead, into `forced`: parameters the walker below emits
  // in addition to whatever `breaks()` asks for. Subdivide until consecutive rings differ by at
  // most kGapStep in half-gap, and the lane-boundary curve is within kBoundaryTolerance of its
  // chord. The half-gap test is the one that shapes the hole: the surface strip emits sub-quads
  // spanning ring i to ring i+1 at fixed cross-section v, and skips one only where it is inside
  // the gap at BOTH rings, so the void's edge is a staircase whose tread is exactly the
  // per-segment half-gap change. kGapStep is therefore a direct bound, in metres, on how far
  // solid road juts into the void -- and on how blunt the lens's tips are, since that same
  // "BOTH rings" rule leaves the first segment inside [t0,t1] fully solid.
  //
  // Sampled at exact `t`, not snapped to the nearest raw physics sample as this once did: t0/t1
  // have to land on a ring exactly for the taper to close on a point, rather than starting at
  // whatever width the nearest interior sample happened to carry.
  std::vector<double> forced;
  {
    struct Anchor {
      Vec3 boundary;
      double halfGap;
    };
    auto anchorAt = [&](double t) {
      Frame f = frame(evaluator.eval(t * gmax));
      applyReservationGap(f, definition, t, pathLength);
      const Vec3 left = f.pos.clone().addScaledVector(f.edgeRight, -f.halfW);
      const Vec3 right = f.pos.clone().addScaledVector(f.edgeRight, f.halfW);
      const double w = std::max(1.0, f.width);
      return Anchor{surface(f, left, right, 0.5 + f.reservationHalfGap / w), f.reservationHalfGap};
    };
    constexpr double kGapStep = 0.25;
    // Matching breaks()'s own centerline deviation tolerance and maximum chord.
    constexpr double kBoundaryTolerance = 0.1, kMaxChord = 40.0;
    std::function<void(double, double, const Anchor&, const Anchor&, int)> splitSpan =
        [&](double ta, double tb, const Anchor& a, const Anchor& b, int depth) {
          if (depth >= 10) return;
          const double tm = (ta + tb) / 2;
          const Anchor m = anchorAt(tm);
          const Vec3 chordMiddle = a.boundary.clone().add(b.boundary).multiplyScalar(0.5);
          if (std::fabs(a.halfGap - b.halfGap) <= kGapStep &&
              m.boundary.distanceTo(chordMiddle) <= kBoundaryTolerance &&
              a.boundary.distanceTo(b.boundary) <= kMaxChord)
            return;
          splitSpan(ta, tm, a, m, depth + 1);
          forced.push_back(tm * gmax);
          splitSpan(tm, tb, m, b, depth + 1);
        };
    for (const auto& reservation : definition.reservations) {
      if (reservation.t1 - reservation.t0 <= 0 || reservation.width <= 0) continue;
      const Anchor a = anchorAt(reservation.t0), b = anchorAt(reservation.t1);
      forced.push_back(reservation.t0 * gmax);
      splitSpan(reservation.t0, reservation.t1, a, b, 0);
      forced.push_back(reservation.t1 * gmax);
      // An end that opens at a nonzero width (a Mitred cut, or a Rounded dome with no nose length)
      // is a hard discontinuity: the road is solid right up to t0 and the void opens abruptly there.
      // A triangle strip cannot express that between two rings, and the surface carve does not try
      // to -- in the strip running from the last ring *outside* the span to the ring at t0, every
      // sub-quad inside the void has both its t0-side corners in the gap band and both its outside
      // corners solid, which is exactly carveQuad's "<= 2 solid corners" drop. The whole band goes,
      // so the hole in the road ran a full ring spacing (11.9 m on the track that surfaced this)
      // PAST the cap wall: an open, uncapped slot metres long at the end of the reservation.
      //
      // Forcing a ring a hair outside the span pins solid road right up against t0, shrinking that
      // dropped band to kHardEdgeMetres. A Joined end needs none of this -- its gap is already zero
      // at the boundary, so there is no jump to smear -- which is why only capped ends showed it.
      constexpr double kHardEdgeMetres = 0.01;
      const double edgeT = kHardEdgeMetres / std::max(1.0, pathLength);
      if (a.halfGap > 0.0 && reservation.t0 - edgeT > 0.0) forced.push_back((reservation.t0 - edgeT) * gmax);
      if (b.halfGap > 0.0 && reservation.t1 + edgeT < 1.0) forced.push_back((reservation.t1 + edgeT) * gmax);
      // A Rounded end's dome can be far shorter than the reservation that carries it (a couple of
      // metres out of hundreds, at the circular default), so splitSpan's bisection would burn its
      // whole depth budget before it ever resolved one -- it would round off to the same blunt cut
      // as Mitred. Place rings across each dome explicitly instead.
      //
      // Spaced by the ellipse's own parameter rather than uniformly in distance: the dome turns
      // hardest at the tip (vertical tangent there), so d = noseLength * (1 - cos(theta)) puts most
      // of the rings exactly where the curvature is, and none are wasted along the near-flat run
      // where it meets the shelf.
      for (int end = 0; end < 2; ++end) {
        const ReservationEndCap& cap = end == 0 ? reservation.endCap0 : reservation.endCap1;
        if (cap.style != ReservationEndCapStyle::Rounded) continue;
        // The anchor at this end already carries the actual, final half-gap reservationHalfGapAt
        // computed there (cap-clamped, and Percent-aware if this reservation's width is a
        // percentage rather than metres) -- reusing it here instead of re-deriving `capWidth` from
        // `cap.width`/`reservation.width` directly avoids re-doing (and potentially getting wrong)
        // that same Fixed-vs-Percent, cap-vs-peak logic a second time.
        const double capWidth = 2.0 * (end == 0 ? a.halfGap : b.halfGap);
        const double noseT = roundedNoseLength(cap, capWidth) / std::max(1.0, pathLength);
        if (capWidth <= 0 || noseT <= 0) continue;
        constexpr int kNoseRings = 16;
        for (int k = 1; k <= kNoseRings; ++k) {
          const double offset = noseT * (1.0 - std::cos(PI / 2 * k / kNoseRings));
          const double t = end == 0 ? reservation.t0 + offset : reservation.t1 - offset;
          if (t > reservation.t0 && t < reservation.t1) forced.push_back(t * gmax);
        }
      }
    }
    // Authored order is not guaranteed to be ascending in t; the walker consumes this in order.
    std::sort(forced.begin(), forced.end());
  }

  auto gAt = [&](int i) {
    return definition.closed ? static_cast<double>(i) / n * evaluator.n
                             : static_cast<double>(i) / (n - 1) * (evaluator.n - 1);
  };
  auto breaks = [&](double g0, double g1) {
    std::set<double> values{g0, g1};
    std::function<void(double, double, int)> splitCell = [&](double a, double b, int depth) {
      double middle = (a + b) / 2;
      Vec3 pa = evaluator.eval(a).pos, pb = evaluator.eval(b).pos, pm = evaluator.eval(middle).pos;
      Vec3 chordMiddle = pa.clone().add(pb).multiplyScalar(0.5);
      if (depth < 10 && (pm.distanceTo(chordMiddle) > 0.1 || pa.distanceTo(pb) > 40)) {
        splitCell(a, middle, depth + 1);
        values.insert(middle);
        splitCell(middle, b, depth + 1);
      }
    };
    splitCell(g0, g1, 0);
    return std::vector<double>(values.begin(), values.end());
  };
  RenderBake out;
  double lastG = -INFINITY;
  auto pushExact = [&](int i) { out.frames.push_back(raw[i]); out.edges.left.push_back(sourceEdges.left[i]); out.edges.right.push_back(sourceEdges.right[i]); lastG = gAt(i); };
  auto pushAdaptive = [&](double g) {
    Frame f = frame(evaluator.eval(g));
    applyReservationGap(f, definition, g / gmax, pathLength);
    out.edges.left.push_back(f.pos.clone().addScaledVector(f.edgeRight, -f.halfW));
    out.edges.right.push_back(f.pos.clone().addScaledVector(f.edgeRight, f.halfW));
    out.frames.push_back(std::move(f));
    lastG = g;
  };
  // Reservation rings, merged into the walk so the output stays ascending in path parameter. One
  // that coincides with a ring the walk was going to emit anyway is dropped rather than
  // duplicated -- a zero-length segment would give the strip degenerate quads and a rail of
  // undefined normal.
  const double gEpsilon = 1e-9 * std::max(1.0, gmax);
  std::size_t nextForced = 0;
  auto flushForced = [&](double g) {
    for (; nextForced < forced.size() && forced[nextForced] < g - gEpsilon; ++nextForced)
      if (forced[nextForced] > lastG + gEpsilon) pushAdaptive(forced[nextForced]);
    for (; nextForced < forced.size() && forced[nextForced] <= g + gEpsilon; ++nextForced) {}
  };
  pushExact(0);
  int i = 0, last = n - 1;
  while (i < last) {
    if (affected[i] || affected[i + 1]) {
      flushForced(gAt(i + 1));
      pushExact(i + 1);
      ++i;
      continue;
    }
    int j = i + 1;
    while (j < last && !affected[j] && !affected[j + 1]) ++j;
    auto partition = breaks(gAt(i), gAt(j));
    for (std::size_t k = 1; k + 1 < partition.size(); ++k) {
      flushForced(partition[k]);
      pushAdaptive(partition[k]);
    }
    flushForced(gAt(j));
    pushExact(j);
    i = j;
  }
  // A closed path's strip wraps from the last raw ring back to ring 0, so a reservation running
  // past gAt(n-1) still has rings left after every pushExact above has run.
  flushForced(gmax);
  return out;
}

void reservationGeometry(Track& track, const PathDefinition& def, const std::vector<Frame>& frames, const Edges& e, int pi);
struct Builder {
  GeometryBatch b;
  void tri(const Vec3& a, const Vec3& c, const Vec3& d, Vec2d ua = {}, Vec2d uc = {}, Vec2d ud = {}) {
    Vec3 n = triNormal(a, c, d);
    for (auto& v : std::array<std::pair<Vec3, Vec2d>, 3>{{{a, ua}, {c, uc}, {d, ud}}}) {
      b.indices.push_back((uint32_t)b.vertices.size());
      b.vertices.push_back({v.first, n, v.second, {}});
    }
  }
};
// Culls a strip sub-quad's four corners -- `ringOf`/`vOf` in the positively-oriented cycle
// (i,a)(i,z)(j,z)(j,a) -- against each corner's own ring's reservation gap band, calling
// `emit(c0,c1,c2)` for each solid triangle that survives (0, 1, or 2 calls). Shared by the top
// surface's carve below and (M6, Uncapped only) the shell's matching bottom-face carve, so the two
// always agree exactly where the void's edge falls -- see the top surface's own call site for the
// full "why" of the corner-wise rule (kept there, once, rather than duplicated here).
void carveQuad(const int ringOf[4], const double vOf[4], const std::vector<std::pair<double, double>>& gapV,
               const std::function<void(int, int, int)>& emit) {
  int solidIdx[4], solidCount = 0;
  for (int c = 0; c < 4; ++c) {
    const auto& band = gapV[ringOf[c]];
    if (vOf[c] > band.first + 1e-9 && vOf[c] < band.second - 1e-9) continue;
    solidIdx[solidCount++] = c;
  }
  if (solidCount <= 2) return;
  if (solidCount == 3) {
    emit(solidIdx[0], solidIdx[1], solidIdx[2]);
  } else {
    emit(0, 1, 3);
    emit(1, 2, 3);
  }
}
void pathGeometry(Track& track, const PathDefinition& def, const Path& path, const Edges& physicsEdges, int pi) {
  RenderBake render = adaptiveRenderBake(def, path.centerline, physicsEdges);
  const auto& frames = render.frames;
  const auto& e = render.edges;
  int n = (int)frames.size(), sc = def.closed ? n : n - 1;
  std::vector<double> dist(n);
  for (int i = 1; i < n; i++) dist[i] = dist[i - 1] + frames[i].pos.distanceTo(frames[i - 1].pos);
  double avg = 0;
  for (auto& f : frames) avg += std::max(1.0, f.width);
  avg /= n;
  Builder top;
  top.b.id = "path-" + std::to_string(pi) + "-surface";
  top.b.kind = GeometryKind::PathSurface;
  // Falls back to the legacy "road" literal when no TrackMaterial was authored (see
  // PathDefinition::material's comment) -- preserves output for tracks without it.
  top.b.materialKey = def.material.empty() ? "road" : def.material;
  top.b.hasUv = true;
  if (def.texture) top.b.texture = TextureBinding{def.texture->assetId, def.texture->tile};
  std::vector<std::vector<double>> br;
  // Central-reservation gap band per ring, in cross-section v-space (0=left edge, 1=right edge,
  // 0.5=center) -- {0.5,0.5} (a zero-width band) where no reservation is active, matching
  // reservationHalfGap's own "0 outside a reservation's span" convention. Fed into `br` as extra
  // breakpoints so the strip below subdivides exactly at the gap's edges, then used per *corner*
  // (not per sub-quad) to cut each boundary quad along the gap edge itself -- see the strip loop.
  // A band stays degenerate at t0/t1, where the taper closes on a point; the strict-interior test
  // there is unsatisfiable, so those rings are wholly solid and the void starts as a true point.
  std::vector<std::pair<double, double>> gapV(n, {0.5, 0.5});
  for (int i = 0; i < n; i++) {
    br.push_back(crossBreak(frames[i].crossSectionCurvature, frames[i].crossSectionTightness, e.left[i].distanceTo(e.right[i])));
    if (frames[i].reservationHalfGap > 1e-9) {
      const double w = std::max(1.0, frames[i].width);
      gapV[i] = {0.5 - frames[i].reservationHalfGap / w, 0.5 + frames[i].reservationHalfGap / w};
      br.back().push_back(gapV[i].first);
      br.back().push_back(gapV[i].second);
      std::sort(br.back().begin(), br.back().end());
      br.back().erase(std::unique(br.back().begin(), br.back().end()), br.back().end());
    }
  }
  auto ringPoint = [&](int ring, double v) {
    const auto exact = std::find(br[ring].begin(), br[ring].end(), v);
    if (exact != br[ring].end()) return surface(frames[ring], e.left[ring], e.right[ring], v);
    auto upper = std::upper_bound(br[ring].begin(), br[ring].end(), v);
    double hi = *upper, lo = *(upper - 1), t = (v - lo) / (hi - lo);
    Vec3 a = surface(frames[ring], e.left[ring], e.right[ring], lo);
    return a.lerp(surface(frames[ring], e.left[ring], e.right[ring], hi), t);
  };
  for (int i = 0; i < sc; i++) {
    int j = def.closed ? (i + 1) % n : i + 1;
    std::set<double> u(br[i].begin(), br[i].end());
    u.insert(br[j].begin(), br[j].end());
    std::vector<double> v(u.begin(), u.end());
    double t0 = dist[i] / avg, t1 = (def.closed && j == 0 ? (dist[i] + frames[i].pos.distanceTo(frames[j].pos)) / avg : dist[j] / avg);
    for (size_t k = 0; k + 1 < v.size(); k++) {
      double a = v[k], z = v[k + 1];
      // The sub-quad's four corners, listed as a positively-oriented cycle -- the same winding the
      // two-triangle split below has always produced, so dropping one corner leaves the remaining
      // three already correctly wound.
      const int ringOf[4] = {i, i, j, j};
      const double vOf[4] = {a, z, z, a}, uvT[4] = {t0, t0, t1, t1};
      // A corner counts as void only when *strictly* inside its own ring's gap band: one sitting
      // exactly on a gap edge is a boundary vertex the solid triangle has to keep. Every gap edge
      // is itself a breakpoint in `v`, so no sub-quad ever straddles one -- which makes "exactly
      // one strictly-void corner" precisely the case where the gap boundary cuts this quad
      // diagonally, and the three surviving corners are the solid triangle either side of it.
      // Splitting there (rather than dropping the quad only when it was void at BOTH rings, which
      // is what this did) is what makes the void's edge the same per-ring polyline the road's own
      // outer edges are, instead of a staircase quantized to the ring spacing. (See `carveQuad`.)
      carveQuad(ringOf, vOf, gapV, [&](int c0, int c1, int c2) {
        top.tri(ringPoint(ringOf[c0], vOf[c0]), ringPoint(ringOf[c1], vOf[c1]), ringPoint(ringOf[c2], vOf[c2]),
                {vOf[c0], uvT[c0]}, {vOf[c1], uvT[c1]}, {vOf[c2], uvT[c2]});
      });
    }
  }
  track.geometry.push_back(std::move(top.b));
  if (std::any_of(frames.begin(), frames.end(), [](auto& f) { return f.crossSectionThickness > 1e-6; })) {
    Builder sh;
    sh.b.id = "path-" + std::to_string(pi) + "-shell";
    sh.b.kind = GeometryKind::PathShell;
    // Fixed material for every shell mesh, regardless of the path's own TrackMaterial -- must stay
    // in sync with cpp/tungsten-monoxide/resources/Resources.xml's Namespace="Tracks" Material
    // "DefaultShellMaterial", and with MaterialCatalog's startup existence check for it.
    sh.b.materialKey = "Tracks/DefaultShellMaterial";
    auto under = [](const Frame& f, Vec3 p) { return p.addScaledVector(f.normal, -f.crossSectionThickness); };
    auto ringUnderPoint = [&](int ring, double v) {
      const auto exact = std::find(br[ring].begin(), br[ring].end(), v);
      if (exact != br[ring].end()) return under(frames[ring], surface(frames[ring], e.left[ring], e.right[ring], v));
      auto upper = std::upper_bound(br[ring].begin(), br[ring].end(), v);
      const double hi = *upper, lo = *(upper - 1), t = (v - lo) / (hi - lo);
      Vec3 a = under(frames[ring], surface(frames[ring], e.left[ring], e.right[ring], lo));
      return a.lerp(under(frames[ring], surface(frames[ring], e.left[ring], e.right[ring], hi)), t);
    };
    for (int i = 0; i < sc; i++) {
      int j = def.closed ? (i + 1) % n : i + 1;
      for (int side = 0; side < 2; side++) {
        double v = side;
        Vec3 a = surface(frames[i], e.left[i], e.right[i], v), c = surface(frames[j], e.left[j], e.right[j], v), au = under(frames[i], a.clone()), cu = under(frames[j], c.clone());
        if (side == 0) {
          sh.tri(a, au, c);
          sh.tri(au, cu, c);
        } else {
          sh.tri(au, a, cu);
          sh.tri(a, c, cu);
        }
      }
      std::set<double> shellBreaks(br[i].begin(), br[i].end());
      shellBreaks.insert(br[j].begin(), br[j].end());
      std::vector<double> shellV(shellBreaks.begin(), shellBreaks.end());
      // M6: an Uncapped reservation carves a matching hole in the underside too (a genuine
      // through-shaft, deliberately non-manifold -- Capped leaves the shell solid here and instead
      // seals the pit's sides in reservationGeometry, using the shell's own unmodified surface as
      // the floor). A ring pair can straddle a reservation's own t0/t1 boundary, where only one of
      // i/j is actually tagged; look at whichever one is.
      const int resIdx = frames[i].reservationIndex >= 0 ? frames[i].reservationIndex : frames[j].reservationIndex;
      const bool uncappedHere = resIdx >= 0 && def.reservations[resIdx].interiorMode == ReservationInteriorMode::Uncapped;
      for (std::size_t k = 0; k + 1 < shellV.size(); ++k) {
        const double lo = shellV[k], hi = shellV[k + 1];
        if (uncappedHere) {
          const int ringOf[4] = {i, i, j, j};
          const double vOf[4] = {lo, hi, hi, lo};
          // Same corner-wise rule and the same `gapV` the top surface's carve uses, so the two
          // holes always agree exactly. Swapping each triangle's last two corners (vs. the top
          // surface's own emit) flips the winding to face downward, matching this loop's
          // un-carved case below.
          carveQuad(ringOf, vOf, gapV, [&](int c0, int c1, int c2) {
            auto pt = [&](int c) { return ringUnderPoint(ringOf[c], vOf[c]); };
            sh.tri(pt(c0), pt(c2), pt(c1));
          });
        } else {
          Vec3 a = ringUnderPoint(i, lo);
          Vec3 b = ringUnderPoint(i, hi);
          Vec3 c = ringUnderPoint(j, lo);
          Vec3 d = ringUnderPoint(j, hi);
          sh.tri(a, c, b);
          sh.tri(b, c, d);
        }
      }
    }
    if (!def.closed) {
      for (int end : {0, n - 1}) {
        for (std::size_t k = 0; k + 1 < br[end].size(); ++k) {
          Vec3 a = surface(frames[end], e.left[end], e.right[end], br[end][k]);
          Vec3 b = under(frames[end], a.clone());
          Vec3 c = surface(frames[end], e.left[end], e.right[end], br[end][k + 1]);
          Vec3 d = under(frames[end], c.clone());
          sh.tri(a, b, c);
          sh.tri(b, d, c);
        }
      }
    }
    track.geometry.push_back(std::move(sh.b));
  }
  for (auto side : std::array<std::pair<const char*, bool>, 2>{{{"left", false}, {"right", true}}}) {
    Builder r;
    r.b.id = "path-" + std::to_string(pi) + "-rail-" + side.first;
    r.b.kind = GeometryKind::PathRail;
    // Fixed material for every rail mesh, regardless of the path's own TrackMaterial -- must stay
    // in sync with cpp/tungsten-monoxide/resources/Resources.xml's Namespace="Tracks" Material
    // "DefaultRailMaterial", and with MaterialCatalog's startup existence check for it.
    r.b.materialKey = "Tracks/DefaultRailMaterial";
    const int railN = static_cast<int>(path.centerline.size());
    const int railSegments = def.closed ? railN : railN - 1;
    for (int i = 0; i < railSegments; i++) {
      int j = def.closed ? (i + 1) % railN : i + 1;
      double si = side.second ? path.centerline[i].sRight : path.centerline[i].sLeft, sj = side.second ? path.centerline[j].sRight : path.centerline[j].sLeft;
      auto fi = curvedSurfaceFrame(Sample{path.centerline[i].pos, path.centerline[i].tangent, path.centerline[i].edgeRight, path.centerline[i].normal, path.centerline[i].halfW, path.centerline[i].sLeft, path.centerline[i].sRight, path.centerline[i].crossSectionCurvature, path.centerline[i].crossSectionTightness}, si);
      auto fj = curvedSurfaceFrame(Sample{path.centerline[j].pos, path.centerline[j].tangent, path.centerline[j].edgeRight, path.centerline[j].normal, path.centerline[j].halfW, path.centerline[j].sLeft, path.centerline[j].sRight, path.centerline[j].crossSectionCurvature, path.centerline[j].crossSectionTightness}, sj);
      Vec3 a = fi.pos.clone().addScaledVector(fi.normal, .04), b = fj.pos.clone().addScaledVector(fj.normal, .04), at = a.clone().addScaledVector(fi.normal, 1.8), bt = b.clone().addScaledVector(fj.normal, 1.8);
      r.tri(a, b, at);
      r.tri(at, b, bt);
    }
    track.geometry.push_back(std::move(r.b));
  }
  reservationGeometry(track, def, frames, e, pi);
}

// Builds a reservation's synthetic MeshRegion plus its ReservationWall render geometry, from the
// two tapered inner-lane boundary curves. Mirrors PathRail's own triangle winding (a, b, at), (at,
// b, bt) exactly, at `reservation.wallHeight` (falling back to DEFAULT_RAIL_HEIGHT when unset) --
// the wall's *visual* height only; see `railClearanceHeight` below for the physics jump-clearance
// height, decoupled from it since CENTRAL_RESERVATION_PLAN.md M6.
//
// Deliberately built from the SAME `frames`/`e` arrays `pathGeometry` just carved the visible
// surface hole from (the adaptive render bake), not the fine physics centerline: a wall built from
// a finer or coarser sample set than the hole it's supposed to guard would leave slivers where the
// two disagree about where the void is -- physics would then see "solid ground" (via the corridor's
// analytical curvedSurfaceFrame, which knows nothing about the carve) directly behind a wall that
// doesn't quite reach that spot, and any exported collision mesh's hole (built from this exact
// surface batch) would likewise not line up with a wall built from a different sample set. Grouped
// by `frame.reservationIndex` rather than re-deriving each frame's `t` (adaptive frames aren't
// uniformly spaced, so index-based `t` recovery doesn't work here the way it does for the physics
// centerline).
//
// M6: Uncapped stays exactly as reservations have always behaved -- `polygons`/`triangles` empty,
// so meshRegionAt/surfaceOwnerAt never treat it as a standing surface (CENTRAL_RESERVATION_PLAN.md's
// original "true void" decision, now scoped to just this mode). Capped additionally gets a real,
// landable floor polygon and seals the shell's underside from the sides (see the two blocks below).
// Every reservation, either mode, gets `oneWayRails`: the boundary only blocks crossing from the
// track *into* the void, never the reverse, so a car that has landed on a Capped floor can still
// drive back off it -- bidirectional rails would trap it there permanently. Harmless for Uncapped,
// since nothing can ever be "inside" one to notice the asymmetry.
void reservationGeometry(Track& track, const PathDefinition& def, const std::vector<Frame>& frames, const Edges& e, int pi) {
  const int n = static_cast<int>(frames.size());
  if (n < 2) return;
  for (int ri = 0; ri < static_cast<int>(def.reservations.size()); ++ri) {
    const auto& reservation = def.reservations[ri];
    struct Bound {
      Vec3 left, right;
      Vec3 normal;
      double crossSectionThickness{0.0};
      // Retained so the Capped floor below can re-sample the cross-section *across* the void, not
      // just at its two rims -- the floor is the road's own curved underside, which dips between
      // them rather than running flat from rim to rim.
      int frame{-1};
      double vLeft{0.5}, vRight{0.5};
    };
    std::vector<Bound> bounds;
    for (int i = 0; i < n; i++) {
      if (frames[i].reservationIndex != ri) continue;
      const Frame& f = frames[i];
      const double halfGap = f.reservationHalfGap;
      const double w = std::max(1.0, f.width);
      const double vLeft = 0.5 - halfGap / w, vRight = 0.5 + halfGap / w;
      bounds.push_back({surface(f, e.left[i], e.right[i], vLeft), surface(f, e.left[i], e.right[i], vRight), f.normal,
                        f.crossSectionThickness, i, vLeft, vRight});
    }
    if (bounds.size() < 2) continue;

    MeshRegion region;
    region.id = "reservation-" + reservation.id + "-path-" + std::to_string(pi);
    region.elevation = bounds.front().left.y;
    region.railHeight = reservation.wallHeight > 0.0 ? reservation.wallHeight : TrackCore::DEFAULT_RAIL_HEIGHT;
    region.railClearanceHeight = reservation.railClearanceHeight > 0.0 ? reservation.railClearanceHeight : TrackCore::DEFAULT_RAIL_HEIGHT;
    region.oneWayRails = true;
    region.bounds = MeshBounds{INFINITY, -INFINITY, INFINITY, -INFINITY};
    auto extend = [&](const Vec3& p) {
      region.bounds.minX = std::min(region.bounds.minX, p.x);
      region.bounds.maxX = std::max(region.bounds.maxX, p.x);
      region.bounds.minZ = std::min(region.bounds.minZ, p.z);
      region.bounds.maxZ = std::max(region.bounds.maxZ, p.z);
    };
    // `outX`/`outZ` point from the void's interior toward the drivable road, i.e. the direction the
    // rail must face. Orienting the normal against that reference is load-bearing rather than
    // cosmetic: the one-way rail test (slideAlongRails -- M6) only blocks travel opposing a rail's
    // own normal, so an inward-facing normal makes that stretch of wall silently non-collidable.
    // A bare 90-degree rotation of the segment direction cannot get this right on its own -- both
    // flanks are emitted in the same along-path direction below, so the same rotation lands outward
    // on one flank and inward on the other (which left every left-flank rail non-collidable, and
    // likewise one of the two end caps). It was harmless only while rails still blocked both ways.
    auto addRail = [&](const Vec3& a, const Vec3& b, double outX, double outZ) {
      const double dx = b.x - a.x, dz = b.z - a.z, length = std::hypot(dx, dz);
      if (length < 1e-9) return;
      double nx = dz / length, nz = -dx / length;
      if (nx * outX + nz * outZ < 0) {
        nx = -nx;
        nz = -nz;
      }
      region.rails.push_back({static_cast<int>(region.rails.size()), {a.x, a.z}, {b.x, b.z}, nx, nz, length});
    };

    Builder wall;
    wall.b.id = region.id + "-wall";
    wall.b.kind = GeometryKind::ReservationWall;
    wall.b.materialKey = "Tracks/DefaultRailMaterial";
    wall.b.hasUv = true;
    // The wall is parameterized by `across` (normalized [0,1] bottom-to-top, so a barrier texture
    // maps once over the wall's height whatever `wallHeight` is authored as) and `along` (measured
    // in units of that same across-extent, so tiles come out square in world metres and never
    // stretch as the wall lengthens). `uvTile` is therefore the wall's own height: one texture
    // repeat per railHeight metres of run.
    //
    // `wallUv` then turns that pair three-quarters of a turn before it becomes the actual UV, so U
    // runs *against* the wall's run direction and V spans its height top-to-bottom -- 180 degrees
    // past the quarter turn this started from, which put U along the wall and V rim-to-top. It is
    // still a *rotation*, not a transpose: (across, along) -> (1 - along, across) is the quarter
    // turn's output run back through the same (u,v) -> (1-u, 1-v) half turn, so the determinant
    // stays positive and an asymmetric texture turns rather than mirroring. To pick a different
    // quarter turn, this one line is the only thing to change.
    const double uvTile = std::max(1e-6, region.railHeight);
    auto wallUv = [](double across, double along) { return Vec2d{1.0 - along, across}; };
    // Per-flank, because the two flanks of a tapered void are different lengths -- sharing one
    // accumulator would slide the texture out of step between them.
    double run[2] = {0.0, 0.0};

    for (std::size_t k = 0; k + 1 < bounds.size(); k++) {
      const Bound& bi = bounds[k];
      const Bound& bj = bounds[k + 1];
      extend(bi.left);
      extend(bi.right);
      // Across the void at this ring, left -> right; each flank faces the opposite way out of it.
      const double acrossX = bi.right.x - bi.left.x, acrossZ = bi.right.z - bi.left.z;
      addRail(bi.left, bj.left, -acrossX, -acrossZ);
      addRail(bi.right, bj.right, acrossX, acrossZ);
      for (const bool right : {false, true}) {
        const Vec3& a = right ? bi.right : bi.left;
        const Vec3& b = right ? bj.right : bj.left;
        Vec3 at = a.clone().addScaledVector(bi.normal, region.railHeight);
        Vec3 bt = b.clone().addScaledVector(bj.normal, region.railHeight);
        double& flankRun = run[right ? 1 : 0];
        const double u0 = flankRun / uvTile, u1 = (flankRun + a.distanceTo(b)) / uvTile;
        wall.tri(a, b, at, wallUv(0.0, u0), wallUv(0.0, u1), wallUv(1.0, u0));
        wall.tri(at, b, bt, wallUv(1.0, u0), wallUv(0.0, u1), wallUv(1.0, u1));
        flankRun += a.distanceTo(b);
      }
    }
    extend(bounds.back().left);
    extend(bounds.back().right);

    // Closes the void at any end whose left/right boundary points don't already coincide -- a
    // Joined end tapers to zero width and self-seals, but a Mitred/Rounded end leaves the void
    // open at a nonzero width unless capped here (CENTRAL_RESERVATION_PLAN.md end-cap closure).
    auto capWall = [&](const Bound& b, double outX, double outZ) {
      const double dx = b.right.x - b.left.x, dz = b.right.z - b.left.z;
      if (std::hypot(dx, dz) < 1e-9) return;
      addRail(b.left, b.right, outX, outZ);
      Vec3 lt = b.left.clone().addScaledVector(b.normal, region.railHeight);
      Vec3 rt = b.right.clone().addScaledVector(b.normal, region.railHeight);
      // Same across/along convention as the flanks, but a cap is its own face rather than a
      // continuation of either flank's run, so its `along` restarts at 0 and spans the void's width
      // here. Keeping the same units means the texture reads at the same scale across the join.
      const double capU = std::hypot(dx, dz) / uvTile;
      wall.tri(b.left, b.right, lt, wallUv(0.0, 0.0), wallUv(0.0, capU), wallUv(1.0, 0.0));
      wall.tri(lt, b.right, rt, wallUv(1.0, 0.0), wallUv(0.0, capU), wallUv(1.0, capU));
    };
    // A cap faces out along the path, away from the ring that neighbours it inside the span.
    auto midX = [](const Bound& b) { return (b.left.x + b.right.x) * 0.5; };
    auto midZ = [](const Bound& b) { return (b.left.z + b.right.z) * 0.5; };
    const std::size_t lastBound = bounds.size() - 1;
    capWall(bounds.front(), midX(bounds.front()) - midX(bounds[1]), midZ(bounds.front()) - midZ(bounds[1]));
    capWall(bounds[lastBound], midX(bounds[lastBound]) - midX(bounds[lastBound - 1]),
            midZ(bounds[lastBound]) - midZ(bounds[lastBound - 1]));

    // M6: Capped interior -- seals the pit's sides from the void's rim down to the shell's
    // underside depth (mirroring pathGeometry's own `under()`), using the shell's material rather
    // than the wall's. The shell's own underside surface is left unmodified by pathGeometry for
    // Capped reservations (only Uncapped carves it) -- it's already solid and already serves as
    // the floor once these sides close it off, so no new floor *render* geometry is needed here.
    if (reservation.interiorMode == ReservationInteriorMode::Capped) {
      auto under = [](const Bound& b, const Vec3& p) { return p.clone().addScaledVector(b.normal, -b.crossSectionThickness); };

      Builder seal;
      seal.b.id = region.id + "-interior-seal";
      seal.b.kind = GeometryKind::PathShell;
      // Same fixed material as PathShell (pathGeometry) -- must stay in sync with
      // Resources.xml's "DefaultShellMaterial" the same way that one does.
      seal.b.materialKey = "Tracks/DefaultShellMaterial";
      for (std::size_t k = 0; k + 1 < bounds.size(); k++) {
        const Bound& bi = bounds[k];
        const Bound& bj = bounds[k + 1];
        for (const bool right : {false, true}) {
          const Vec3& a = right ? bi.right : bi.left;
          const Vec3& b = right ? bj.right : bj.left;
          const Vec3 au = under(bi, a), bu = under(bj, b);
          seal.tri(a, au, b);
          seal.tri(au, bu, b);
        }
      }
      // Seals whichever end is actually open (Mitred/Rounded, nonzero width) -- a Joined end's
      // left/right already coincide, matching `capWall` above's own guard.
      auto sealEnd = [&](const Bound& b) {
        const double dx = b.right.x - b.left.x, dz = b.right.z - b.left.z;
        if (std::hypot(dx, dz) < 1e-9) return;
        const Vec3 lu = under(b, b.left), ru = under(b, b.right);
        seal.tri(b.left, lu, b.right);
        seal.tri(lu, ru, b.right);
      };
      sealEnd(bounds.front());
      sealEnd(bounds.back());
      track.geometry.push_back(std::move(seal.b));

      // Physics floor: the void's own tapered footprint (left boundary forward, right boundary
      // backward) for ownership, plus a real *curved* floor for height.
      //
      // A flat scalar cannot describe this floor. `crossSectionHeight` scales the trough's depth
      // with the road's local chord width, so wherever a track's authored width varies across a
      // reservation's span the underside beneath it rises and falls by the same proportion -- on the
      // track that surfaced this (width ramping 36 -> 138.6 -> 36 across one reservation, curvature
      // -0.5) the true floor runs -22.0 m at the t0 rim, -38.3 m at mid-span and -22.9 m at t1: a
      // 16.2 m swing. Any single `elevation` is then tens of metres wrong somewhere in the same
      // reservation -- which is exactly what the "one end sinks into the track floor" report was:
      // the physics floor sitting far below the visible one at the shallow (Mitred) end, so a car
      // driving in dropped straight through the rendered surface.
      //
      // So sample the true underside on a grid -- every ring along the span, kFloorSpan+1 stations
      // across the void at each -- and hand MeshRegion real triangles to interpolate (elevationAt).
      // Sampling *across* as well as along matters because the floor is the cross-section curve
      // itself: it dips between the two rims rather than running flat across them. `elevation`
      // stays set to the shallowest sample, purely as the out-of-footprint fallback elevationAt
      // returns and as the reference meshRegionAt scores candidate regions by.
      constexpr int kFloorSpan = 4;
      auto floorPoint = [&](const Bound& b, double v) {
        const Frame& f = frames[b.frame];
        return under(b, surface(f, e.left[b.frame], e.right[b.frame], v));
      };
      double elevation = -std::numeric_limits<double>::infinity();
      for (const Bound& b : bounds) elevation = std::max(elevation, floorPoint(b, b.vLeft).y);
      region.elevation = elevation;
      auto addFloorTri = [&](const Vec3& a, const Vec3& b, const Vec3& c) {
        MeshFloorTriangle tri;
        tri.points = {Vec2d{a.x, a.z}, Vec2d{b.x, b.z}, Vec2d{c.x, c.z}};
        tri.heights = {a.y, b.y, c.y};
        tri.bounds = {std::min({a.x, b.x, c.x}), std::max({a.x, b.x, c.x}), std::min({a.z, b.z, c.z}),
                      std::max({a.z, b.z, c.z})};
        region.floor.push_back(std::move(tri));
      };
      for (std::size_t k = 0; k + 1 < bounds.size(); k++) {
        const Bound &bi = bounds[k], &bj = bounds[k + 1];
        for (int s = 0; s < kFloorSpan; ++s) {
          const double f0 = static_cast<double>(s) / kFloorSpan, f1 = static_cast<double>(s + 1) / kFloorSpan;
          const Vec3 a = floorPoint(bi, bi.vLeft + (bi.vRight - bi.vLeft) * f0);
          const Vec3 b = floorPoint(bi, bi.vLeft + (bi.vRight - bi.vLeft) * f1);
          const Vec3 c = floorPoint(bj, bj.vLeft + (bj.vRight - bj.vLeft) * f0);
          const Vec3 d = floorPoint(bj, bj.vLeft + (bj.vRight - bj.vLeft) * f1);
          addFloorTri(a, b, c);
          addFloorTri(b, d, c);
        }
      }
      MeshPolygon floor;
      floor.polygonId = 0;
      for (const auto& b : bounds) floor.outer.push_back({b.left.x, b.left.z});
      for (auto it = bounds.rbegin(); it != bounds.rend(); ++it) floor.outer.push_back({it->right.x, it->right.z});
      region.polygons.push_back(std::move(floor));
    }

    track.meshRegions.push_back(std::move(region));
    track.geometry.push_back(std::move(wall.b));
  }
}
}  // namespace

bool bakeTrack(Track& track, std::vector<TrackWarning>& warnings, std::string& error, bool detectSelfIntersections) {
  try {
    track.paths.clear();
    track.zones.clear();
    track.triggers.clear();
    track.geometry.clear();
    track.selfIntersections.clear();
    track.connectedEndpointIds.clear();
    for (auto& c : track.definition.disjointSeams)
      if (!c.pointId.empty()) track.connectedEndpointIds.insert(c.pointId);
    std::set<std::string> branchPointIds;
    for (auto& c : track.definition.junctions) {
      if (!c.pointId.empty()) track.connectedEndpointIds.insert(c.pointId);
      if (!c.pointId.empty()) branchPointIds.insert(c.pointId);
    }
    struct PointStats {
      int endpoints{0}, interior{0}, closed{0};
    };
    std::map<std::string, PointStats> pointStats;
    for (const auto& definition : track.definition.paths) {
      const Parts parts = split(definition);
      for (int i = 0; i < static_cast<int>(parts.cp.size()); ++i) {
        auto& stats = pointStats[parts.cp[i]->id];
        if (definition.closed)
          ++stats.closed;
        else if (i == 0 || i == static_cast<int>(parts.cp.size()) - 1)
          ++stats.endpoints;
        else
          ++stats.interior;
      }
    }
    for (const auto& [id, stats] : pointStats)
      if (stats.endpoints >= 3 || (stats.endpoints >= 1 && (stats.closed > 0 || stats.interior > 0))) branchPointIds.insert(id);
    std::vector<Edges> bakedEdges;
    for (size_t i = 0; i < track.definition.paths.size(); i++) {
      const auto& definition = track.definition.paths[i];
      Parts parts = split(definition);
      Path path;
      path.closed = definition.closed;
      path.endpointIds.start = parts.cp.front()->id;
      path.endpointIds.end = parts.cp.back()->id;
      path.endpointIds.hasStart = path.endpointIds.hasEnd = true;
      for (auto* point : parts.cp) path.anchors.push_back(point->pos);
      path.centerline = center(definition, sampleCount(definition));
      bakedEdges.push_back(edges(path.centerline, definition.closed));
      track.paths.push_back(std::move(path));
    }

    // Disjoint seams override both incident endpoint normals and edge corners.
    struct Incident {
      int path, index, neighbor;
      bool start;
    };
    for (const auto& seam : track.definition.disjointSeams) {
      std::vector<Incident> incidents;
      for (int pi = 0; pi < static_cast<int>(track.paths.size()); ++pi) {
        const Path& path = track.paths[pi];
        if (path.closed || path.centerline.size() < 2) continue;
        if (path.endpointIds.start == seam.pointId) incidents.push_back({pi, 0, 1, true});
        if (path.endpointIds.end == seam.pointId) incidents.push_back({pi, static_cast<int>(path.centerline.size()) - 1,
                                                                       static_cast<int>(path.centerline.size()) - 2, false});
      }
      if (incidents.size() != 2) continue;
      Vec3 average = track.paths[incidents[0].path].centerline[incidents[0].index].normal.clone().add(track.paths[incidents[1].path].centerline[incidents[1].index].normal).normalize();
      for (const auto& incident : incidents) track.paths[incident.path].centerline[incident.index].normal = average;
      const Incident& a = incidents[0];
      const Incident& b = incidents[1];
      const Frame& af = track.paths[a.path].centerline[a.index];
      const Frame& bf = track.paths[b.path].centerline[b.index];
      const bool flipped = a.start == b.start && af.edgeRight.dot(bf.edgeRight) < 0;
      const Vec3 centerPoint = af.pos;
      const double maxHalfWidth = std::max(af.halfW, bf.halfW);
      auto cut = [&](bool right) {
        bool bRight = flipped ? !right : right;
        const auto& ae = right ? bakedEdges[a.path].right : bakedEdges[a.path].left;
        const auto& be = bRight ? bakedEdges[b.path].right : bakedEdges[b.path].left;
        Vec3 point = lineX(ae[a.index], ae[a.neighbor], be[b.index], be[b.neighbor]).value_or(centerPoint);
        if (std::hypot(point.x - centerPoint.x, point.z - centerPoint.z) > 6 * maxHalfWidth) point = centerPoint;
        return point;
      };
      const Vec3 left = cut(false), right = cut(true);
      bakedEdges[a.path].left[a.index] = left;
      bakedEdges[a.path].right[a.index] = right;
      bakedEdges[b.path].left[b.index] = flipped ? right : left;
      bakedEdges[b.path].right[b.index] = flipped ? left : right;
    }

    // Compiled before the per-path loop below (it depends only on definition.meshes/meshAssets, not
    // on any per-path baked data) so that pathGeometry -> reservationGeometry can append each
    // reservation's synthetic MeshRegion directly to the already-compiled list, instead of racing
    // compileTrackMeshes's own track.meshRegions.clear(). Reservation regions are deliberately
    // excluded from the `low`/trackFloorY computation below (which only sees the regions compiled
    // here) -- they carry no floor of their own (CENTRAL_RESERVATION_PLAN.md's "true void"
    // decision), so their `elevation` (the road surface height, not a fall-through depth) must not
    // raise the respawn floor.
    compileTrackMeshes(track, warnings);
    double low = INFINITY;
    for (const auto& region : track.meshRegions) low = std::min(low, region.elevation);
    for (size_t i = 0; i < track.paths.size(); ++i) {
      const auto& definition = track.definition.paths[i];
      Parts parts = split(definition);
      const bool hasBranch = std::any_of(parts.cp.begin(), parts.cp.end(), [&](const auto* point) {
        return branchPointIds.count(point->id) != 0;
      });
      if (!hasBranch) {
        const bool wrapsAtSeam = !definition.closed && parts.cp.front()->id == parts.cp.back()->id &&
                                 track.connectedEndpointIds.count(parts.cp.front()->id);
        // Branch-connected paths (the `hasBranch` guard above) intentionally skip detection too --
        // their authored geometry is deliberately left unaltered by self-intersection handling.
        bakedEdges[i].left = removeSelfLoops(std::move(bakedEdges[i].left), definition, "left", wrapsAtSeam,
                                             track.definition.selfIntersectionOverrides,
                                             detectSelfIntersections ? &track.selfIntersections : nullptr);
        bakedEdges[i].right = removeSelfLoops(std::move(bakedEdges[i].right), definition, "right", wrapsAtSeam,
                                              track.definition.selfIntersectionOverrides,
                                              detectSelfIntersections ? &track.selfIntersections : nullptr);
      }
      wallOffsets(track.paths[i].centerline, bakedEdges[i]);
      for (auto& frame : track.paths[i].centerline) low = std::min(low, frame.pos.y);
      pathGeometry(track, definition, track.paths[i], bakedEdges[i], static_cast<int>(i));
    }
    std::map<std::string, int> endpointCounts;
    for (const auto& path : track.paths) {
      if (path.closed) continue;
      if (path.endpointIds.hasStart) ++endpointCounts[path.endpointIds.start];
      if (path.endpointIds.hasEnd) ++endpointCounts[path.endpointIds.end];
    }
    for (const auto& [id, count] : endpointCounts)
      if (count >= 2) track.connectedEndpointIds.insert(id);

    std::map<std::string, int> regionIds;
    for (std::size_t i = 0; i < track.meshRegions.size(); ++i)
      regionIds.emplace(track.meshRegions[i].id, static_cast<int>(i));

    std::map<std::string, int> pathIds;
    for (size_t i = 0; i < track.definition.paths.size(); i++)
      pathIds.emplace(track.definition.paths[i].id, static_cast<int>(i));
    for (auto& z : track.definition.zones) {
      if (z.host.kind == "mesh") {
        const auto found = regionIds.find(z.host.meshId);
        if (found == regionIds.end()) continue;
        Zone zone;
        zone.id = z.id;
        zone.kind = "mesh";
        zone.effect = z.effect;
        zone.factor = z.factor;
        zone.duration = z.duration;
        zone.hostRegionIndex = found->second;
        zone.x = z.host.x;
        zone.z = z.host.z;
        zone.rotation = z.host.rotation * DEG2RAD;
        zone.halfLength = std::max(0.25, z.length / 2);
        zone.halfWidth = std::max(0.25, z.width / 2);
        track.zones.push_back(zone);

        const double cosine = std::cos(zone.rotation), sine = std::sin(zone.rotation);
        auto corner = [&](double x, double z) {
          return Vec3(zone.x + x * cosine - z * sine,
                      track.meshRegions[zone.hostRegionIndex].elevation + 0.15,
                      zone.z + x * sine + z * cosine);
        };
        const Vec3 a = corner(-zone.halfLength, -zone.halfWidth);
        const Vec3 b = corner(zone.halfLength, -zone.halfWidth);
        const Vec3 c = corner(zone.halfLength, zone.halfWidth);
        const Vec3 d = corner(-zone.halfLength, zone.halfWidth);
        constexpr double uvScale = 1.0 / 6.0;
        Builder geometry;
        geometry.b.id = "zone-" + z.id;
        geometry.b.kind = GeometryKind::ZoneSurface;
        // Fixed material for every zone surface, regardless of effect -- must stay in sync with
        // cpp/tungsten-monoxide/resources/Resources.xml's Namespace="Tracks" Material
        // "DefaultZoneMaterial", and with MaterialCatalog's startup existence check for it.
        geometry.b.materialKey = "Tracks/DefaultZoneMaterial";
        geometry.b.hasUv = true;
        geometry.tri(a, b, c, {-zone.halfLength * uvScale, -zone.halfWidth * uvScale},
                     {zone.halfLength * uvScale, -zone.halfWidth * uvScale},
                     {zone.halfLength * uvScale, zone.halfWidth * uvScale});
        geometry.tri(a, c, d, {-zone.halfLength * uvScale, -zone.halfWidth * uvScale},
                     {zone.halfLength * uvScale, zone.halfWidth * uvScale},
                     {-zone.halfLength * uvScale, zone.halfWidth * uvScale});
        track.geometry.push_back(std::move(geometry.b));
        continue;
      }
      if (z.host.kind == "path") {
        int pi = pathIds[z.host.pathId];
        Evaluator e(track.definition.paths[pi]);
        double gm = (e.closed ? e.n : e.n - 1);
        double gc = z.host.t * gm, half = z.length / 2, step = gm / std::max(600, e.n * 60);
        auto walk = [&](int direction) {
          double g = gc, accumulated = 0;
          Vec3 previous = e.eval(g).pos;
          for (int i = 0; i < 40000; ++i) {
            double nextG = g + direction * step;
            double clampedG = e.closed ? nextG : std::max(0.0, std::min(gm, nextG));
            Vec3 point = e.eval(clampedG).pos;
            double distance = point.distanceTo(previous);
            if (accumulated + distance >= half)
              return g + direction * step * (distance > 1e-9 ? (half - accumulated) / distance : 0);
            accumulated += distance;
            previous = point;
            g = nextG;
            if (!e.closed && (g <= 0 || g >= gm)) return std::max(0.0, std::min(gm, g));
          }
          return g;
        };
        Zone o;
        o.id = z.id;
        o.kind = "path";
        o.effect = z.effect;
        o.factor = z.factor;
        o.duration = z.duration;
        o.hostPathIndex = pi;
        o.gLo = walk(-1);
        o.gHi = walk(1);
        o.gMax = gm;
        o.closed = e.closed;
        o.lateral = z.host.lateral;
        o.halfWidth = z.width / 2;
        track.zones.push_back(o);

        const int rows = std::max(2, std::min(96, static_cast<int>(std::round(z.length / 6.0))));
        constexpr int acrossSteps = 4;
        std::vector<std::array<Vec3, acrossSteps + 1>> points(rows + 1);
        for (int ri = 0; ri <= rows; ++ri) {
          const double g = o.gLo + (o.gHi - o.gLo) * (static_cast<double>(ri) / rows);
          Frame f = frame(e.eval(e.closed ? g : std::max(0.0, std::min(gm, g))));
          for (int ai = 0; ai <= acrossSteps; ++ai) {
            const double lateral = z.host.lateral - o.halfWidth + 2.0 * o.halfWidth * ai / acrossSteps;
            const double vv = (lateral + f.width / 2.0) / f.width;
            const double lift = TrackCore::crossSectionHeight(f.crossSectionCurvature, f.crossSectionTightness, vv, f.width);
            const double dh = TrackCore::crossSectionHeightDerivative(f.crossSectionCurvature, f.crossSectionTightness, vv, f.width);
            Vec3 crossT = f.edgeRight.clone().multiplyScalar(f.width).addScaledVector(f.normal, dh);
            Vec3 normal;
            normal.crossVectors(f.tangent, crossT).normalize();
            if (normal.dot(f.normal) < 0) normal.negate();
            points[ri][ai] = f.pos.clone().addScaledVector(f.edgeRight, lateral).addScaledVector(f.normal, lift).addScaledVector(normal, 0.15);
          }
        }
        Builder zone;
        zone.b.id = "zone-" + z.id;
        zone.b.kind = GeometryKind::ZoneSurface;
        // Fixed material for every zone surface, regardless of effect -- must stay in sync with
        // cpp/tungsten-monoxide/resources/Resources.xml's Namespace="Tracks" Material
        // "DefaultZoneMaterial", and with MaterialCatalog's startup existence check for it.
        zone.b.materialKey = "Tracks/DefaultZoneMaterial";
        zone.b.hasUv = true;
        const double uvWidth = z.width / 6.0;
        std::vector<double> rowDistances(rows + 1);
        for (int ri = 1; ri <= rows; ++ri) rowDistances[ri] = rowDistances[ri - 1] + points[ri][0].distanceTo(points[ri - 1][0]);
        for (int ri = 0; ri < rows; ++ri) {
          const double u0 = rowDistances[ri] / 6.0;
          const double u1 = rowDistances[ri + 1] / 6.0;
          for (int ai = 0; ai < acrossSteps; ++ai) {
            const double v0 = uvWidth * ai / acrossSteps, v1 = uvWidth * (ai + 1) / acrossSteps;
            zone.tri(points[ri][ai], points[ri][ai + 1], points[ri + 1][ai], {u0, v0}, {u0, v1}, {u1, v0});
            zone.tri(points[ri][ai + 1], points[ri + 1][ai + 1], points[ri + 1][ai], {u0, v1}, {u1, v1}, {u1, v0});
          }
        }
        track.geometry.push_back(std::move(zone.b));
      }
    }
    std::vector<TriggerDefinition> triggerDefinitions = track.definition.triggers;
    const bool hasFinish = std::any_of(triggerDefinitions.begin(), triggerDefinitions.end(), [](const auto& t) {
      return t.type == "checkpoint" && t.role == "finish";
    });
    if (!hasFinish && !track.paths.empty()) {
      const int pi = track.definition.start.path;
      const Path& path = track.paths[pi];
      const PathDefinition& definition = track.definition.paths[pi];
      const Vec3& anchor = path.anchors[track.definition.start.point];
      int index = 0;
      double best = INFINITY;
      for (int i = 0; i < static_cast<int>(path.centerline.size()); ++i) {
        const double d = path.centerline[i].pos.distanceToSquared(anchor);
        if (d < best) {
          best = d;
          index = i;
        }
      }
      int stepDirection = track.definition.start.reverse ? -1 : 1;
      bool repaired = false;
      auto canStep = [&](int step) { return definition.closed || (index + step >= 0 && index + step < static_cast<int>(path.centerline.size())); };
      if (!canStep(stepDirection)) {
        stepDirection = -stepDirection;
        repaired = true;
      }
      int at = index;
      double travelled = 0, fraction = 0;
      const int maxSteps = definition.closed ? static_cast<int>(path.centerline.size()) : static_cast<int>(path.centerline.size()) - 1;
      for (int n = 0; n < maxSteps && (definition.closed || (at + stepDirection >= 0 && at + stepDirection < static_cast<int>(path.centerline.size()))); ++n) {
        const int next = definition.closed ? (at + stepDirection + static_cast<int>(path.centerline.size())) % static_cast<int>(path.centerline.size()) : at + stepDirection;
        const double segment = path.centerline[at].pos.distanceTo(path.centerline[next].pos);
        if (travelled + segment >= 20 && segment > 0) {
          fraction = (20 - travelled) / segment;
          break;
        }
        travelled += segment;
        at = next;
        fraction = 0;
      }
      const int next = definition.closed ? (at + stepDirection + static_cast<int>(path.centerline.size())) % static_cast<int>(path.centerline.size()) : std::max(0, std::min(static_cast<int>(path.centerline.size()) - 1, at + stepDirection));
      const double tAt = definition.closed ? static_cast<double>(at) / path.centerline.size() : static_cast<double>(at) / (path.centerline.size() - 1);
      double tNext = definition.closed ? static_cast<double>(next) / path.centerline.size() : static_cast<double>(next) / (path.centerline.size() - 1);
      if (definition.closed && stepDirection > 0 && next < at) tNext += 1;
      if (definition.closed && stepDirection < 0 && next > at) tNext -= 1;
      double t = tAt + (tNext - tAt) * fraction;
      t = definition.closed ? std::fmod(std::fmod(t, 1.0) + 1.0, 1.0) : std::max(0.0, std::min(1.0, t));
      Evaluator evaluator(definition);
      TriggerDefinition finish;
      finish.id = "checkpoint-finish";
      std::set<std::string> ids;
      for (const auto& trigger : triggerDefinitions) ids.insert(trigger.id);
      for (int suffix = 2; ids.count(finish.id); ++suffix) finish.id = "checkpoint-finish-" + std::to_string(suffix);
      finish.type = "checkpoint";
      finish.role = "finish";
      finish.direction = (repaired ? !track.definition.start.reverse : track.definition.start.reverse) ? "backward" : "forward";
      finish.host.kind = "path";
      finish.host.pathId = definition.id;
      finish.host.t = t;
      finish.width = evaluator.eval(t * (evaluator.closed ? evaluator.n : evaluator.n - 1)).width;
      finish.height = 12;
      triggerDefinitions.push_back(std::move(finish));
    }
    for (auto& t : triggerDefinitions) {
      Trigger trigger;
      trigger.id = t.id;
      trigger.type = t.type;
      trigger.role = t.role;
      trigger.direction = t.direction;
      trigger.halfWidth = std::max(0.25, t.width / 2);
      trigger.height = std::max(0.25, t.height);
      if (t.host.kind == "mesh") {
        const auto found = regionIds.find(t.host.meshId);
        if (found == regionIds.end()) continue;
        const double angle = t.rotation * DEG2RAD, cosine = std::cos(angle), sine = std::sin(angle);
        trigger.center = {t.host.x, track.meshRegions[found->second].elevation, t.host.z};
        trigger.fwd = {sine, 0, cosine};
        trigger.right = {cosine, 0, -sine};
        trigger.up = UP;
      } else if (t.host.kind == "path") {
        int pi = pathIds[t.host.pathId];
        Evaluator evaluator(track.definition.paths[pi]);
        double gMax = evaluator.closed ? evaluator.n : evaluator.n - 1;
        Frame frameAtTrigger = frame(evaluator.eval(t.host.t * gMax));
        // Lateral offset from the centerline (mirrors ZoneHostDefinition::lateral's own
        // edgeRight-offset + cross-section-aware lift, above): vv=0.5 (dead center) when
        // lateral=0, same as the previous hardcoded `.5`.
        const double vv = frameAtTrigger.width > 0.0 ? (t.host.lateral + frameAtTrigger.width / 2.0) / frameAtTrigger.width : 0.5;
        double lift = TrackCore::crossSectionHeight(frameAtTrigger.crossSectionCurvature,
                                                    frameAtTrigger.crossSectionTightness, vv,
                                                    frameAtTrigger.width);
        double angle = t.rotation * DEG2RAD, cosine = std::cos(angle), sine = std::sin(angle);
        trigger.center =
            frameAtTrigger.pos.clone().addScaledVector(frameAtTrigger.edgeRight, t.host.lateral).addScaledVector(frameAtTrigger.normal, lift);
        trigger.fwd = frameAtTrigger.tangent.clone().multiplyScalar(cosine).addScaledVector(frameAtTrigger.edgeRight, sine).normalize();
        trigger.right = frameAtTrigger.edgeRight.clone().multiplyScalar(cosine).addScaledVector(frameAtTrigger.tangent, -sine).normalize();
        trigger.up = frameAtTrigger.normal;
      } else {
        continue;
      }
      {
        // Gate quad corners (c0=(-1,0), c1=(1,0), c2=(1,1), c3=(-1,1) in right/up space, same
        // two-triangle split) match the game's own trigger debug-overlay quad exactly.
        Builder trig;
        trig.b.id = "trigger-" + t.id;
        trig.b.kind = GeometryKind::TriggerSurface;
        // Fixed material for every trigger gate, regardless of trigger type -- must stay in sync
        // with cpp/tungsten-monoxide/resources/Resources.xml's Namespace="Tracks" Material
        // "DefaultTriggerMaterial", and with MaterialCatalog's startup existence check for it.
        trig.b.materialKey = "Tracks/DefaultTriggerMaterial";
        trig.b.hasUv = true;
        auto corner = [&](double sr, double su) {
          return trigger.center.clone().addScaledVector(trigger.right, sr * trigger.halfWidth).addScaledVector(trigger.up, su * trigger.height);
        };
        Vec3 c0 = corner(-1, 0), c1 = corner(1, 0), c2 = corner(1, 1), c3 = corner(-1, 1);
        // In the editor's XZ track convention +right is the driver's left-hand side when looking
        // along fwd. Start U there so trigger textures read left-to-right in the direction of travel.
        trig.tri(c0, c1, c2, {1, 0}, {0, 0}, {0, 1});
        trig.tri(c0, c2, c3, {1, 0}, {0, 1}, {1, 1});
        track.geometry.push_back(std::move(trig.b));
      }
      track.triggers.push_back(std::move(trigger));
    }
    track.trackFloorY = (std::isfinite(low) ? low : 0) - Consts::RESPAWN_FALL_DEPTH;
    return true;
  } catch (const std::exception& e) {
    error = e.what();
    warnings.push_back({"path-bake-failed", error, {}});
    return false;
  }
}
}  // namespace tox
