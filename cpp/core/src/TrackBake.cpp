// TrackBake.cpp — current-schema authored spline paths to world-space physics
// frames and graphics-API-agnostic triangle batches. The operation order mirrors
// web/track-core.js, web/js/track-bake.js and web/js/track-render-geometry.js for parity.
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
std::vector<Frame> center(const PathDefinition& p, int N) {
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
  auto f = center(p, 200);
  double n = 0;
  for (size_t i = 1; i < f.size(); i++) n += f[i].pos.distanceTo(f[i - 1].pos);
  if (p.closed) n += f.front().pos.distanceTo(f.back().pos);
  return n;
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
  // Full, UNBOUNDED pairwise scan on the RAW (pre-collapse) points -- EDITOR_PARITY_GAPS.md gap 1:
  // every self-intersection this edge has, regardless of span, so the editor can show/cycle markers
  // for far ("auto-keep") crossings too, not just the near ones the bounded collapse pass below
  // actually acts on. Mirrors web/web/track-core.js's findSelfIntersections, which is likewise a
  // separate, unbounded scan from removeLocalSelfIntersectionLoops's own bounded one. Only run when
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
  n.crossVectors(b.clone().sub(a), c.clone().sub(a)).normalize();
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
  auto pushExact = [&](int i) { out.frames.push_back(raw[i]); out.edges.left.push_back(sourceEdges.left[i]); out.edges.right.push_back(sourceEdges.right[i]); };
  auto pushAdaptive = [&](double g) { Frame f = frame(evaluator.eval(g)); out.edges.left.push_back(f.pos.clone().addScaledVector(f.edgeRight,-f.halfW)); out.edges.right.push_back(f.pos.clone().addScaledVector(f.edgeRight,f.halfW)); out.frames.push_back(std::move(f)); };
  pushExact(0);
  int i = 0, last = n - 1;
  while (i < last) {
    if (affected[i] || affected[i + 1]) {
      pushExact(i + 1);
      ++i;
      continue;
    }
    int j = i + 1;
    while (j < last && !affected[j] && !affected[j + 1]) ++j;
    auto partition = breaks(gAt(i), gAt(j));
    for (std::size_t k = 1; k + 1 < partition.size(); ++k) pushAdaptive(partition[k]);
    pushExact(j);
    i = j;
  }
  return out;
}

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
  // PathDefinition::material's comment) -- preserves JS<->C++ parity for tracks without it.
  top.b.materialKey = def.material.empty() ? "road" : def.material;
  top.b.hasUv = true;
  if (def.texture) top.b.texture = TextureBinding{def.texture->assetId, def.texture->tile};
  std::vector<std::vector<double>> br;
  for (int i = 0; i < n; i++) br.push_back(crossBreak(frames[i].crossSectionCurvature, frames[i].crossSectionTightness, e.left[i].distanceTo(e.right[i])));
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
      Vec3 p0 = ringPoint(i, a), p1 = ringPoint(i, z), q0 = ringPoint(j, a), q1 = ringPoint(j, z);
      top.tri(p0, p1, q0, {a, t0}, {z, t0}, {a, t1});
      top.tri(p1, q1, q0, {z, t0}, {z, t1}, {a, t1});
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
      for (std::size_t k = 0; k + 1 < shellV.size(); ++k) {
        Vec3 a = ringUnderPoint(i, shellV[k]);
        Vec3 b = ringUnderPoint(i, shellV[k + 1]);
        Vec3 c = ringUnderPoint(j, shellV[k]);
        Vec3 d = ringUnderPoint(j, shellV[k + 1]);
        sh.tri(a, c, b);
        sh.tri(b, c, d);
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

    double low = INFINITY;
    for (size_t i = 0; i < track.paths.size(); ++i) {
      const auto& definition = track.definition.paths[i];
      Parts parts = split(definition);
      const bool hasBranch = std::any_of(parts.cp.begin(), parts.cp.end(), [&](const auto* point) {
        return branchPointIds.count(point->id) != 0;
      });
      if (!hasBranch) {
        const bool wrapsAtSeam = !definition.closed && parts.cp.front()->id == parts.cp.back()->id &&
                                 track.connectedEndpointIds.count(parts.cp.front()->id);
        // Branch-connected paths (the `hasBranch` guard above) intentionally skip detection too,
        // same as JS's `bp.hasBranchConnection ? [] : detectPathCrossings(...)` -- their authored
        // geometry is deliberately left unaltered by self-intersection handling.
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

    compileTrackMeshes(track, warnings);
    for (const auto& region : track.meshRegions) low = std::min(low, region.elevation);
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
        // Gate quad matching track-game.js's buildTriggerDebugMesh corners exactly (c0=(-1,0),
        // c1=(1,0), c2=(1,1), c3=(-1,1) in right/up space, same two-triangle split) -- this is the
        // renderer-neutral counterpart of that debug-only three.js quad, not a new shape.
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
        trig.tri(c0, c1, c2, {0, 0}, {1, 0}, {1, 1});
        trig.tri(c0, c2, c3, {0, 0}, {1, 1}, {0, 1});
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
