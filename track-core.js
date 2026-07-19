/* track-core.js — shared track math for the game (track.html) and the editor
 * (editor.html). Dependency-free (no three.js) so the 2D editor can use it too.
 *
 * A track is composed of one or more PATHS. Each path is either a closed loop
 * or an open curve, and holds a single ordered array of TYPED control points:
 *
 *   points: [
 *     { type: 'position', id: 'p1', pos: [x, y, z], weight: <NURBS weight> },
 *     { type: 'roll',     t: 0..1, roll: <degrees> },
 *     { type: 'width',    t: 0..1, width: <full width> },
 *     ...
 *   ]
 *
 * Each type is independent (its own count, its own spacing) and only
 * interacts with points of its own type: 'position' points interpolate with
 * a rational, uniformly-knotted cubic B-spline (NURBS) and their order in the
 * array (relative to other 'position' points) IS the path's shape sequence;
 * 'roll'/'width' points each interpolate with their own non-uniform
 * Catmull-Rom/Hermite spline over their own `t` (a fraction of the path's
 * parameter domain, independent of array order). All wrap for closed paths,
 * clamp at the ends for open ones. +roll lifts the LEFT edge (banks into a
 * right-hand turn). Use splitPoints(path.points) to get the three filtered,
 * t-sorted arrays the math functions below actually consume.
 *
 * track = { version, name, samples, paths: [ { id, closed, points } ],
 *            disjointSeams: [{ id, pointId, kind, ... }],
 *            start: { path, point, reverse } }
 * Position point IDs are stable editor identities. If the same position ID
 * appears in multiple path occurrences, parseTrack() makes them the same
 * in-memory object so editing that point moves every occurrence. The editor
 * uses disjointSeams metadata to reverse hard-corner split/open operations;
 * the game only needs point IDs plus the seam pointIds to cut disjoint edges.
 * `start` picks which position control point the player begins at (nearest
 * baked sample to it) and whether they face along the path's natural
 * (parametric) direction or the reverse of it.
 *
 * Public API (window.TrackCore):
 *   basis(u), basisDeriv(u)          - uniform cubic B-spline basis + derivative
 *   splitPoints(points)              - { controlPoints, rollPoints, widthPoints }
 *   makeEvaluator(cps, closed)       - { evalTrack(g), CP_N, closed }
 *                                      open endpoints evaluate exactly at the
 *                                      first/last position control point
 *   buildCenterline(cps, N, closed)  - array of baked frames (plain {x,y,z})
 *   buildEdges(frames, closed)       - trimmed { left, right } edge polylines
 *   parseTrack(text)                 - JSON string -> validated track object
 *   serializeTrack(track)            - track object -> pretty JSON string
 *   DEFAULT_TRACK, STARTER_TRACK     - built-in tracks
 *   N_DEFAULT                        - default sample count
 */
(function (global) {
  'use strict';

  const N_DEFAULT = 400;
  const DEG2RAD = Math.PI / 180;

  // --- tiny plain-object vector helpers ({x,y,z}) ----------------------------
  const vsub = (a, b) => ({ x: a.x - b.x, y: a.y - b.y, z: a.z - b.z });
  const vadd = (a, b) => ({ x: a.x + b.x, y: a.y + b.y, z: a.z + b.z });
  const vscale = (a, s) => ({ x: a.x * s, y: a.y * s, z: a.z * s });
  const vaddScaled = (a, b, s) => ({ x: a.x + b.x * s, y: a.y + b.y * s, z: a.z + b.z * s });
  const vcross = (a, b) => ({
    x: a.y * b.z - a.z * b.y,
    y: a.z * b.x - a.x * b.z,
    z: a.x * b.y - a.y * b.x
  });
  const vlen = a => Math.hypot(a.x, a.y, a.z);
  const vnorm = a => { const l = vlen(a) || 1; return { x: a.x / l, y: a.y / l, z: a.z / l }; };

  // Split a path's unified typed `points` array into the three plain arrays
  // the math functions below consume. These are FILTERED views, not copies --
  // each entry is the exact same object that lives in `points` (extra fields
  // like `type` are simply ignored by the math) -- so callers can hold onto
  // one (e.g. a UI selection) and mutate or splice it out of `points` later.
  // Position points keep their array-order (that order IS the path shape);
  // roll/width points are extracted and sorted by their own `t` (their array
  // position doesn't matter).
  function splitPoints(points) {
    const controlPoints = [], rollPoints = [], widthPoints = [];
    for (const p of points) {
      if (p.type === 'roll') rollPoints.push(p);
      else if (p.type === 'width') widthPoints.push(p);
      else controlPoints.push(p);
    }
    rollPoints.sort((a, b) => a.t - b.t);
    widthPoints.sort((a, b) => a.t - b.t);
    return { controlPoints, rollPoints, widthPoints };
  }

  // --- uniform cubic B-spline basis (1/6 matrix) and its derivative ----------
  function basis(u) {
    const u2 = u * u, u3 = u2 * u;
    return [
      (1 - 3 * u + 3 * u2 - u3) / 6,
      (4 - 6 * u2 + 3 * u3) / 6,
      (1 + 3 * u + 3 * u2 - 3 * u3) / 6,
      u3 / 6
    ];
  }
  function basisDeriv(u) {
    const u2 = u * u;
    return [
      (-3 + 6 * u - 3 * u2) / 6,
      (-12 * u + 9 * u2) / 6,
      (3 + 6 * u - 9 * u2) / 6,
      (3 * u2) / 6
    ];
  }

  // --- generic scalar spline (used by both roll and width) -------------------
  // Both roll (banking) and width have their OWN set of control points,
  // independent of the position control points: points = [{ t, <key> }], t in
  // [0,1] is a fraction of the path's own parameter domain (0 = start, 1 =
  // end/wrap-back-to-start). Interpolated with a non-uniform Catmull-Rom/
  // Hermite spline over the real t spacing (so unevenly-placed points still
  // behave sensibly), circular for closed paths, clamped at the ends for open
  // ones.
  function evalScalarSpline(points, closed, tQuery, key) {
    const m = points.length;
    if (m === 1) return points[0][key];
    let t = tQuery;
    if (closed) t = ((t % 1) + 1) % 1;
    else t = Math.max(points[0].t, Math.min(points[m - 1].t, t));

    // idxT(i): the (t, value) of point i, extended outside [0, m) by wrapping
    // (closed) or clamping (open). For closed, wrapping index by m shifts t
    // by a whole cycle (+-1), since i - (i mod m) is always an exact multiple
    // of m.
    const idxT = i => {
      if (closed) {
        const k = ((i % m) + m) % m;
        const cyc = (i - k) / m;
        return { t: points[k].t + cyc, v: points[k][key] };
      }
      const k = Math.max(0, Math.min(m - 1, i));
      return { t: points[k].t, v: points[k][key] };
    };

    let i = closed ? m - 1 : m - 2; // default: wraparound segment (closed) or last segment (open)
    for (let k = 0; k < m - 1; k++) {
      if (t >= points[k].t && t < points[k + 1].t) { i = k; break; }
    }

    const p1 = idxT(i), p2 = idxT(i + 1);
    let tt = t;
    if (tt < p1.t) tt += 1; // query fell just after the wrap point
    const dt = (p2.t - p1.t) || 1e-6;
    const u = (tt - p1.t) / dt;

    const p0 = idxT(i - 1), p3 = idxT(i + 2);
    const m1 = ((p2.v - p0.v) / ((p2.t - p0.t) || 1e-6)) * dt;
    const m2 = ((p3.v - p1.v) / ((p3.t - p1.t) || 1e-6)) * dt;

    const u2 = u * u, u3 = u2 * u;
    const h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
    return h00 * p1.v + h10 * m1 + h01 * p2.v + h11 * m2;
  }
  // roll in degrees in, radians out; width is left as-is (floor applied by callers)
  function evalRollSpline(rollPoints, closed, tQuery) {
    return evalScalarSpline(rollPoints, closed, tQuery, 'roll') * DEG2RAD;
  }
  function evalWidthSpline(widthPoints, closed, tQuery) {
    return Math.max(1, evalScalarSpline(widthPoints, closed, tQuery, 'width'));
  }

  // --- rational cubic B-spline evaluator --------------------------------------
  // Returns evalTrack(g), g in [0, CP_N) for closed paths or [0, CP_N-1] for
  // open ones: { pos, tangent(normalized), roll, width }. pos/tangent are
  // plain {x,y,z}; roll is in RADIANS; the whole cross-section (pos, roll,
  // width) shares the same rational basis (weights included).
  //
  // Open paths use the same uniform basis matrix but CLAMP the control-point
  // index at each end instead of wrapping. That isn't a textbook clamped
  // B-spline (it doesn't pass exactly through the endpoints), but it keeps a
  // single code path for both cases and the curve still starts/ends right at
  // the first/last control point's neighbourhood, consistent with how this
  // engine already treats control points as approximate, not interpolated.
  function makeEvaluator(controlPoints, closed, rollPoints, widthPoints) {
    closed = closed !== false;
    const CP_N = controlPoints.length;
    const cpVec = controlPoints.map(c => ({ x: c.pos[0], y: c.pos[1], z: c.pos[2] }));
    const cpW = controlPoints.map(c => (c.weight == null ? 1 : c.weight));
    const rp = (rollPoints && rollPoints.length >= 1) ? rollPoints : [{ t: 0, roll: 0 }, { t: 1, roll: 0 }];
    const wp = (widthPoints && widthPoints.length >= 1) ? widthPoints : [{ t: 0, width: 12 }, { t: 1, width: 12 }];
    const gMax = (closed ? CP_N : CP_N - 1) || 1;
    const wrap = closed
      ? i => ((i % CP_N) + CP_N) % CP_N
      : i => Math.max(0, Math.min(CP_N - 1, i));

    function evalTrack(g) {
      if (!closed && CP_N > 0) {
        if (g <= 0) {
          const pos = cpVec[0];
          const tangent = vnorm(CP_N > 1 ? vsub(cpVec[1], cpVec[0]) : { x: 0, y: 0, z: 1 });
          return { pos, tangent, roll: evalRollSpline(rp, closed, 0), width: evalWidthSpline(wp, closed, 0) };
        }
        if (g >= CP_N - 1) {
          const pos = cpVec[CP_N - 1];
          const tangent = vnorm(CP_N > 1 ? vsub(cpVec[CP_N - 1], cpVec[CP_N - 2]) : { x: 0, y: 0, z: 1 });
          return { pos, tangent, roll: evalRollSpline(rp, closed, 1), width: evalWidthSpline(wp, closed, 1) };
        }
      }
      const seg = Math.floor(g);
      const u = g - seg;
      const b = basis(u), db = basisDeriv(u);
      const idx = [wrap(seg - 1), wrap(seg), wrap(seg + 1), wrap(seg + 2)];

      let num = { x: 0, y: 0, z: 0 }, dnum = { x: 0, y: 0, z: 0 };
      let den = 0, dden = 0;
      for (let k = 0; k < 4; k++) {
        const j = idx[k];
        const w = cpW[j];
        const bw = b[k] * w, dbw = db[k] * w;
        num = vaddScaled(num, cpVec[j], bw);
        dnum = vaddScaled(dnum, cpVec[j], dbw);
        den += bw; dden += dbw;
      }
      const pos = vscale(num, 1 / den);
      // rational derivative via quotient rule: (N'D - N D') / D^2, normalized
      const tangent = vnorm(vscale(vaddScaled(vscale(dnum, den), num, -dden), 1 / (den * den)));
      const t = g / gMax;
      const roll = evalRollSpline(rp, closed, t);
      const width = evalWidthSpline(wp, closed, t);
      return { pos, tangent, roll, width };
    }
    return { evalTrack, CP_N, closed };
  }

  // --- bake N frames along a path ---------------------------------------------
  // Each frame: { pos, tangent, h, roll, width, halfW, edgeRight, normal }, all
  // vectors plain {x,y,z}. Consumers wrap these in their own vector type.
  // Closed paths bake N samples spanning the full loop [0, CP_N); open paths
  // bake N samples spanning [0, CP_N-1] inclusive of both endpoints.
  function buildCenterline(controlPoints, N, closed, rollPoints, widthPoints) {
    closed = closed !== false;
    N = N || N_DEFAULT;
    const { evalTrack, CP_N } = makeEvaluator(controlPoints, closed, rollPoints, widthPoints);
    const UP = { x: 0, y: 1, z: 0 };
    const out = [];
    for (let i = 0; i < N; i++) {
      const g = closed ? (i / N) * CP_N : (N > 1 ? (i / (N - 1)) * (CP_N - 1) : 0);
      const { pos, tangent, roll, width } = evalTrack(g);

      const h = vnorm(vcross(UP, tangent));
      let baseNormal = vnorm(vcross(tangent, h));
      if (baseNormal.y < 0) baseNormal = vscale(baseNormal, -1);

      // +roll lifts the LEFT edge -> roll the cross-section by -roll about tangent
      const cosR = Math.cos(-roll), sinR = Math.sin(-roll);
      const edgeRight = vadd(vscale(h, cosR), vscale(baseNormal, sinR));
      const normal = vnorm(vaddScaled(vscale(baseNormal, cosR), h, -sinR));

      out.push({ pos, tangent, h, roll, width, halfW: width / 2, edgeRight, normal });
    }
    return out;
  }

  // --- edge offsetting with self-intersection trimming -----------------------
  // Offsetting the centerline by +/- halfW gives the two track edges. On a tight
  // inner corner (radius < halfW) the inner edge folds back on itself into a
  // little loop. We detect those folds and collapse them to a single sharp miter
  // corner (the intersection of the edge lines entering and leaving the fold).

  // Does edge segment a->b travel roughly forward (same way as the centerline)?
  function segForward(a, b, t) { return ((b.x - a.x) * t.x + (b.z - a.z) * t.z) > 0; }

  // Intersection of infinite lines (p1,p2) and (p3,p4) in the XZ plane; y averaged.
  function lineIntersectXZ(p1, p2, p3, p4) {
    const den = (p1.x - p2.x) * (p3.z - p4.z) - (p1.z - p2.z) * (p3.x - p4.x);
    if (Math.abs(den) < 1e-9) return null; // parallel
    const t = ((p1.x - p3.x) * (p3.z - p4.z) - (p1.z - p3.z) * (p3.x - p4.x)) / den;
    return { x: p1.x + t * (p2.x - p1.x), y: (p2.y + p3.y) / 2, z: p1.z + t * (p2.z - p1.z) };
  }

  // Collapse each folded run of an edge polyline to a sharp miter point.
  // For a closed path the polyline wraps (segment N-1 -> 0 exists and the scan
  // may start anywhere); for an open path it doesn't (only N-1 segments, and
  // the scan must start at index 0).
  function trimEdge(pts, frames, closed) {
    closed = closed !== false;
    const N = pts.length;
    const segCount = closed ? N : N - 1;
    if (segCount <= 0) return pts.map(p => ({ x: p.x, y: p.y, z: p.z }));
    const nextIdx = i => closed ? (i + 1) % N : i + 1;

    const fwd = new Array(segCount);
    for (let i = 0; i < segCount; i++) fwd[i] = segForward(pts[i], pts[nextIdx(i)], frames[i].tangent);
    const out = pts.map(p => ({ x: p.x, y: p.y, z: p.z }));

    let startFwd = closed ? fwd.indexOf(true) : 0;
    if (startFwd < 0) return out; // no forward segment at all (degenerate) -> leave

    let i = 0;
    while (i < segCount) {
      const seg = closed ? (startFwd + i) % segCount : i;
      if (fwd[seg]) { i++; continue; }

      // maximal run of backward (folded) segments starting at `seg`
      let len = 0;
      while (len < segCount && !fwd[closed ? (startFwd + i + len) % segCount : i + len]) len++;
      const s = seg;                                            // first folded segment (vertex s -> s+1)
      const e = closed ? (startFwd + i + len - 1) % segCount : i + len - 1; // last folded segment

      // sharp corner = intersection of the segment entering the fold and the one leaving it
      const prevIdx = closed ? (s - 1 + N) % N : Math.max(0, s - 1);
      const afterIdx = closed ? (e + 2) % N : Math.min(N - 1, e + 2);
      const enterA = pts[prevIdx], enterB = pts[s];
      const leaveA = pts[nextIdx(e)], leaveB = pts[afterIdx];
      const mid = {
        x: (enterB.x + leaveA.x) / 2, y: (enterB.y + leaveA.y) / 2, z: (enterB.z + leaveA.z) / 2
      };
      let X = lineIntersectXZ(enterA, enterB, leaveA, leaveB) || mid;
      // guard against runaway miters from near-parallel (or degenerate, open-end) edges
      if (Math.hypot(X.x - mid.x, X.z - mid.z) > 6 * frames[s].halfW) X = mid;

      // collapse vertices s .. e+1 onto the miter point (zero-area strip there)
      const last = nextIdx(e);
      let v = s;
      while (true) { out[v] = { x: X.x, y: X.y, z: X.z }; if (v === last) break; v = closed ? (v + 1) % N : v + 1; }
      i += len;
    }
    return out;
  }

  // Build both trimmed edges from baked centerline frames.
  // Returns { left: [{x,y,z}...], right: [...] }, each length frames.length.
  function buildEdges(frames, closed) {
    closed = closed !== false;
    const left = [], right = [];
    for (let i = 0; i < frames.length; i++) {
      const c = frames[i];
      left.push(vaddScaled(c.pos, c.edgeRight, -c.halfW));
      right.push(vaddScaled(c.pos, c.edgeRight, c.halfW));
    }
    return { left: trimEdge(left, frames, closed), right: trimEdge(right, frames, closed) };
  }

  // Same as buildEdges, but offsets by the UNROLLED horizontal direction (`h`,
  // i.e. as if roll were always 0) instead of the banked `edgeRight`. Used by
  // preview/editor views that want the track's plan-view footprint (width
  // only) without banking distorting the top-down shape.
  function buildFlatEdges(frames, closed) {
    closed = closed !== false;
    const left = [], right = [];
    for (let i = 0; i < frames.length; i++) {
      const c = frames[i];
      left.push(vaddScaled(c.pos, c.h, -c.halfW));
      right.push(vaddScaled(c.pos, c.h, c.halfW));
    }
    return { left: trimEdge(left, frames, closed), right: trimEdge(right, frames, closed) };
  }

  // Proper segment-segment crossing test in the XZ plane (strict: sharing an
  // endpoint does NOT count, so adjacent polyline segments never false-positive).
  function segmentsCrossXZ(a1, a2, b1, b2) {
    const cross = (o, a, p) => (a.x - o.x) * (p.z - o.z) - (a.z - o.z) * (p.x - o.x);
    const d1 = cross(a1, a2, b1), d2 = cross(a1, a2, b2);
    const d3 = cross(b1, b2, a1), d4 = cross(b1, b2, a2);
    return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
  }
  // Where two (already known to cross) segments meet, in XZ; y averaged.
  function segCrossPointXZ(a1, a2, b1, b2) {
    const den = (a1.x - a2.x) * (b1.z - b2.z) - (a1.z - a2.z) * (b1.x - b2.x);
    if (Math.abs(den) < 1e-12) return { x: (a1.x + a2.x) / 2, y: (a1.y + a2.y) / 2, z: (a1.z + a2.z) / 2 };
    const t = ((a1.x - b1.x) * (b1.z - b2.z) - (a1.z - b1.z) * (b1.x - b2.x)) / den;
    return { x: a1.x + t * (a2.x - a1.x), y: (a1.y + a2.y) / 2, z: a1.z + t * (a2.z - a1.z) };
  }
  // Collapse LOCAL self-intersections of a single polyline (typically a wall/
  // rail's edge line) -- segments that aren't adjacent (so trimEdge's
  // consecutive-backward-segment fold detection above misses them) but are
  // still geometrically nearby, e.g. a wiggly chicane whose offset briefly
  // crosses back over an earlier/later part of itself. Each crossing's loop
  // is collapsed to the crossing point (same technique as trimEdge), so it
  // renders as zero-area there instead of visibly intersecting geometry.
  // XZ-plane only, matching trimEdge's convention -- a genuine elevated
  // crossover (same XZ footprint, different height, e.g. a bridge) would
  // false-positive here; not expected on an ordinary track.
  //
  // The search is deliberately bounded to a local window (MAX_LOCAL_SPAN
  // segments), NOT a full pairwise scan: a track that deliberately crosses
  // itself far away in parameter space (a genuine figure-8 course, where the
  // two lobes cross at one point but are each a legitimate, separate stretch
  // of boundary) must keep both lobes intact -- collapsing "everything
  // between" two crossings 100+ segments apart would silently delete an
  // entire lobe instead of just the small overlapping area.
  function collapseSelfIntersections(pts, closed) {
    closed = closed !== false;
    const N = pts.length;
    const segCount = closed ? N : N - 1;
    if (segCount < 4) return pts.map(p => ({ x: p.x, y: p.y, z: p.z })); // need 2 non-adjacent segments to cross
    const nextIdx = i => closed ? (i + 1) % N : i + 1;
    const out = pts.map(p => ({ x: p.x, y: p.y, z: p.z }));
    const MAX_LOCAL_SPAN = 40;
    const MAX_PASSES = segCount; // generous cap; each pass resolves at least one crossing
    for (let pass = 0; pass < MAX_PASSES; pass++) {
      let found = null;
      for (let i = 0; i < segCount && !found; i++) {
        const a1 = out[i], a2 = out[nextIdx(i)];
        const jMax = Math.min(segCount, i + MAX_LOCAL_SPAN);
        for (let j = i + 2; j < jMax; j++) {
          if (closed && i === 0 && j === segCount - 1) continue; // adjacent via wraparound
          if (segmentsCrossXZ(a1, a2, out[j], out[nextIdx(j)])) { found = { i, j }; break; }
        }
      }
      if (!found) break;
      const { i, j } = found;
      const X = segCrossPointXZ(out[i], out[nextIdx(i)], out[j], out[nextIdx(j)]);
      let v = nextIdx(i);
      while (true) { out[v] = { x: X.x, y: X.y, z: X.z }; if (v === j) break; v = nextIdx(v); }
    }
    return out;
  }

  // Compute hard-corner edge cuts for DISJOINT seams specifically (editor-
  // authored hard corners, kind 'opened-closed' or 'split-open' -- always
  // exactly two incident open-path ends by construction, never a 3+-way
  // branch). `bakedPaths` entries are { id, closed, controlPoints, frames,
  // edges }. Returns an array parallel to bakedPaths: each entry may contain
  // { start:{left,right}, end:{left,right} } endpoint overrides. The two
  // incident ends' boundary lines are intersected in XZ; a far/parallel
  // intersection falls back to the shared center point, producing a
  // deliberate hard mitre instead of the two ribbons just meeting/overlapping
  // at the raw shared point.
  function computeDisjointEdgeCuts(bakedPaths, disjointSeams) {
    const cuts = bakedPaths.map(() => ({}));
    const centerOf = inc => inc.frames[inc.idx].pos;
    const fallback = inc => { const p = centerOf(inc); return { x: p.x, y: p.y, z: p.z }; };
    const line = (inc, side) => ({ p: inc.edges[side][inc.idx], q: inc.edges[side][inc.neighbor] });
    for (const seam of disjointSeams || []) {
      const incs = [];
      bakedPaths.forEach((bp, pathIndex) => {
        if (bp.closed || !bp.controlPoints.length || bp.frames.length < 2) return;
        const last = bp.controlPoints.length - 1;
        if (bp.controlPoints[0] && bp.controlPoints[0].id === seam.pointId) {
          incs.push({ pathIndex, end: 'start', idx: 0, neighbor: 1, frames: bp.frames, edges: bp.edges });
        }
        if (bp.controlPoints[last] && bp.controlPoints[last].id === seam.pointId) {
          incs.push({ pathIndex, end: 'end', idx: bp.frames.length - 1, neighbor: bp.frames.length - 2, frames: bp.frames, edges: bp.edges });
        }
      });
      // A disjoint seam is always exactly 2-incident (opened-closed: one
      // path's own start+end; split-open: one path's end + another's start).
      // Anything else means the seam record is stale/malformed -- skip it
      // rather than guess.
      if (incs.length !== 2) continue;
      const a = incs[0], b = incs[1];
      const center = fallback(a);
      const maxHalfW = Math.max(a.frames[a.idx].halfW || 1, b.frames[b.idx].halfW || 1);
      // Only a SAME-end-type join (both 'start' or both 'end', e.g. two curves
      // welded at both ends to close a loop) can meet with opposed
      // orientation: there, each end's `edgeRight` -- hence its left/right
      // edge labelling -- may be flipped relative to the other, and the
      // edgeRight dot product correctly tells left-with-left from
      // left-with-right (see below).
      //
      // An end<->start join (a.end !== b.end) is instead ALWAYS the same
      // continuous curve turning a corner -- there is no "other end" to have
      // an independent orientation, so it must NEVER flip, no matter how
      // sharp the corner is (even a near-total hairpin reversal): the LEFT
      // side of the road stays the LEFT side through the turn. Using the raw
      // edgeRight dot product here was wrong -- rotating both tangents by the
      // same corner angle rotates edgeRight by that angle too, so the sign
      // flips as soon as the turn exceeds 90 degrees, incorrectly swapping
      // left<->right and breaking the mitre past that point.
      const erA = a.frames[a.idx].edgeRight, erB = b.frames[b.idx].edgeRight;
      const flipped = a.end === b.end && (erA.x * erB.x + erA.y * erB.y + erA.z * erB.z) < 0;
      const bSide = side => flipped ? (side === 'left' ? 'right' : 'left') : side;
      // Miter for a's `side` = intersection of a's edge on that side with b's edge
      // on the matching physical side. Named from a's perspective.
      const sideCut = side => {
        const la = line(a, side), lb = line(b, bSide(side));
        let x = lineIntersectXZ(la.p, la.q, lb.p, lb.q) || center;
        if (Math.hypot(x.x - center.x, x.z - center.z) > 6 * maxHalfW) x = center;
        return x;
      };
      const left = sideCut('left'), right = sideCut('right');
      for (const inc of [a, b]) {
        if (!cuts[inc.pathIndex][inc.end]) cuts[inc.pathIndex][inc.end] = {};
        // `left`/`right` are named from a's side. b's own left edge is whichever
        // of the two corners lies on b's left -- swapped when the ends are flipped.
        const useSwapped = inc === b && flipped;
        const myLeft = useSwapped ? right : left;
        const myRight = useSwapped ? left : right;
        cuts[inc.pathIndex][inc.end].left = { x: myLeft.x, y: myLeft.y, z: myLeft.z };
        cuts[inc.pathIndex][inc.end].right = { x: myRight.x, y: myRight.y, z: myRight.z };
      }
    }
    return cuts;
  }

  // --- JSON schema: parse / validate / serialize -----------------------------
  function normalizePoint(p, i) {
    if (!p || !Array.isArray(p.pos) || p.pos.length !== 3 || p.pos.some(n => typeof n !== 'number')) {
      throw new Error('control point ' + i + ': pos must be [x,y,z] numbers');
    }
    const num = (v, d) => (typeof v === 'number' && isFinite(v) ? v : d);
    return {
      type: 'position',
      id: (p.id && typeof p.id === 'string') ? p.id : null,
      pos: [p.pos[0], p.pos[1], p.pos[2]],
      weight: Math.max(0.01, num(p.weight, 1))
    };
  }

  function normalizeRollPoint(rp) {
    const num = (v, d) => (typeof v === 'number' && isFinite(v) ? v : d);
    return { type: 'roll', t: Math.max(0, Math.min(1, num(rp && rp.t, 0))), roll: Math.max(-180, Math.min(180, num(rp && rp.roll, 0))) };
  }
  function normalizeWidthPoint(wp) {
    const num = (v, d) => (typeof v === 'number' && isFinite(v) ? v : d);
    return { type: 'width', t: Math.max(0, Math.min(1, num(wp && wp.t, 0))), width: Math.max(1, num(wp && wp.width, 12)) };
  }

  // Legacy per-point `roll`/`width` migration: evenly spaces one point per
  // control point across the path's parameter domain.
  function defaultRollPoints(rawPoints, closed) {
    const n = rawPoints.length;
    if (n === 0) return [{ type: 'roll', t: 0, roll: 0 }, { type: 'roll', t: 1, roll: 0 }];
    const denom = closed ? n : Math.max(1, n - 1);
    return rawPoints.map((p, i) => ({
      type: 'roll',
      t: closed ? i / n : i / denom,
      roll: (p && typeof p.roll === 'number') ? Math.max(-180, Math.min(180, p.roll)) : 0
    }));
  }
  function defaultWidthPoints(rawPoints, closed) {
    const n = rawPoints.length;
    if (n === 0) return [{ type: 'width', t: 0, width: 12 }, { type: 'width', t: 1, width: 12 }];
    const denom = closed ? n : Math.max(1, n - 1);
    return rawPoints.map((p, i) => ({
      type: 'width',
      t: closed ? i / n : i / denom,
      width: (p && typeof p.width === 'number') ? Math.max(1, p.width) : 12
    }));
  }

  // Accepts three input shapes per path, all normalized to the current
  // unified { closed, points: [{type, ...}, ...] } schema:
  //   1. current:  { closed, points: [{type, ...}, ...] }
  //   2. pre-refactor: { closed, controlPoints, rollPoints, widthPoints }
  //   3. legacy:   [{pos, roll, width, weight}, ...] or { controlPoints: [...] }
  //                (per-point roll/width), migrated via defaultRoll/WidthPoints
  function normalizePath(rawPath, i) {
    if (rawPath && !Array.isArray(rawPath) && Array.isArray(rawPath.points)) {
      const closed = !(rawPath.closed === false);
      const points = rawPath.points.map(p => {
        if (p && p.type === 'roll') return normalizeRollPoint(p);
        if (p && p.type === 'width') return normalizeWidthPoint(p);
        return normalizePoint(p, i);
      });
      const posCount = points.filter(p => p.type === 'position').length;
      if (posCount < 4) throw new Error('path ' + i + ': a track path needs at least 4 position control points');
      if (!points.some(p => p.type === 'roll')) points.push({ type: 'roll', t: 0, roll: 0 }, { type: 'roll', t: 1, roll: 0 });
      if (!points.some(p => p.type === 'width')) points.push({ type: 'width', t: 0, width: 12 }, { type: 'width', t: 1, width: 12 });
      return { id: rawPath.id || null, closed, points };
    }

    const rawPoints = Array.isArray(rawPath) ? rawPath : rawPath && rawPath.controlPoints;
    if (!Array.isArray(rawPoints)) throw new Error('path ' + i + ': no points/controlPoints array found');
    if (rawPoints.length < 4) throw new Error('path ' + i + ': a track path needs at least 4 control points');
    const closed = !(rawPath && rawPath.closed === false);
    const rawRoll = rawPath && Array.isArray(rawPath.rollPoints) ? rawPath.rollPoints : null;
    const rollPoints = (rawRoll && rawRoll.length >= 1)
      ? rawRoll.map(normalizeRollPoint)
      : defaultRollPoints(rawPoints, closed);
    const rawWidth = rawPath && Array.isArray(rawPath.widthPoints) ? rawPath.widthPoints : null;
    const widthPoints = (rawWidth && rawWidth.length >= 1)
      ? rawWidth.map(normalizeWidthPoint)
      : defaultWidthPoints(rawPoints, closed);
    return {
      id: rawPath && rawPath.id || null,
      closed,
      points: rawPoints.map(normalizePoint).concat(rollPoints, widthPoints)
    };
  }

  // Clamp a start descriptor to valid path/point indices for the given paths.
  function normalizeStart(rawStart, paths) {
    let path = (rawStart && Number.isInteger(rawStart.path)) ? rawStart.path : 0;
    path = Math.max(0, Math.min(paths.length - 1, path));
    const posCount = splitPoints(paths[path].points).controlPoints.length;
    let point = (rawStart && Number.isInteger(rawStart.point)) ? rawStart.point : 0;
    point = Math.max(0, Math.min(posCount - 1, point));
    const reverse = !!(rawStart && rawStart.reverse);
    return { path, point, reverse };
  }

  // Accepts either the current { paths: [{closed, points}, ...] } schema, the
  // pre-refactor three-array schema, or the legacy single-closed-loop
  // { controlPoints: [...] } schema (see normalizePath).
  function parseTrack(text) {
    const data = JSON.parse(text);
    let rawPaths;
    if (Array.isArray(data)) rawPaths = [{ closed: true, controlPoints: data }];
    else if (Array.isArray(data.paths)) rawPaths = data.paths;
    else if (Array.isArray(data.controlPoints)) rawPaths = [{ closed: true, controlPoints: data.controlPoints }];
    else throw new Error('no paths or controlPoints array found');
    if (rawPaths.length < 1) throw new Error('a track needs at least one path');
    const paths = rawPaths.map(normalizePath);
    // Assign/stabilize position-point identities and make duplicate IDs share
    // the same object reference in memory. Old tracks without IDs get fresh IDs.
    const byId = new Map();
    let nextPointId = 1;
    for (const path of paths) {
      for (let i = 0; i < path.points.length; i++) {
        const p = path.points[i];
        if (p.type !== 'position') continue;
        if (!p.id) {
          do { p.id = 'p' + (nextPointId++); } while (byId.has(p.id));
        }
        if (byId.has(p.id)) path.points[i] = byId.get(p.id);
        else byId.set(p.id, p);
      }
    }
    return {
      version: (data && data.version) || 3,
      name: (data && data.name) || 'Untitled Track',
      samples: (data && data.samples) || N_DEFAULT,
      paths,
      disjointSeams: Array.isArray(data && data.disjointSeams) ? data.disjointSeams : [],
      junctions: Array.isArray(data && data.junctions) ? data.junctions : [],
      start: normalizeStart(data && data.start, paths)
    };
  }

  function serializeTrack(track) {
    // Compact one-line-per-point formatting for readability.
    const pathsJson = track.paths.map(path => {
      const lines = path.points.map(p => {
        if (p.type === 'roll') return '      { "type": "roll", "t": ' + p.t + ', "roll": ' + p.roll + ' }';
        if (p.type === 'width') return '      { "type": "width", "t": ' + p.t + ', "width": ' + p.width + ' }';
        return '      { "type": "position", "id": ' + JSON.stringify(p.id || '') + ', "pos": [' + p.pos.join(', ') + '], "weight": ' + p.weight + ' }';
      }).join(',\n');
      return '    { "id": ' + JSON.stringify(path.id || '') + ', "closed": ' + (path.closed !== false) + ', "points": [\n' + lines + '\n    ] }';
    }).join(',\n');
    const start = normalizeStart(track.start, track.paths);
    return '{\n' +
      '  "version": 3,\n' +
      '  "name": ' + JSON.stringify(track.name || 'Untitled Track') + ',\n' +
      '  "start": { "path": ' + start.path + ', "point": ' + start.point + ', "reverse": ' + start.reverse + ' },\n' +
      '  "disjointSeams": ' + JSON.stringify(track.disjointSeams || []) + ',\n' +
      '  "junctions": ' + JSON.stringify(track.junctions || []) + ',\n' +
      '  "paths": [\n' + pathsJson + '\n  ]\n}\n';
  }

  // --- built-in tracks --------------------------------------------------------
  const DEFAULT_TRACK = {
    version: 2,
    name: 'Default Circuit',
    start: { path: 0, point: 0, reverse: false },
    disjointSeams: [],
    junctions: [],
    paths: [{
      closed: true,
      points: [
        { type: 'position', pos: [90, 0, 0], weight: 1 },
        { type: 'position', pos: [70, 4, 46], weight: 1 },
        { type: 'position', pos: [18, 8, 60], weight: 1 },
        { type: 'position', pos: [-34, 5, 54], weight: 1 },
        { type: 'position', pos: [-74, 0, 30], weight: 1 },
        { type: 'position', pos: [-92, -4, -6], weight: 1 },
        { type: 'position', pos: [-66, -3, -40], weight: 1 },
        { type: 'position', pos: [-16, 1, -56], weight: 1 },
        { type: 'position', pos: [40, 3, -48], weight: 1 },
        { type: 'position', pos: [80, 1, -22], weight: 1 },
        { type: 'roll', t: 0.0, roll: 0 },
        { type: 'roll', t: 0.1, roll: -14 },
        { type: 'roll', t: 0.2, roll: -22 },
        { type: 'roll', t: 0.3, roll: -18 },
        { type: 'roll', t: 0.4, roll: -10 },
        { type: 'roll', t: 0.5, roll: 16 },
        { type: 'roll', t: 0.6, roll: 20 },
        { type: 'roll', t: 0.7, roll: 8 },
        { type: 'roll', t: 0.8, roll: -12 },
        { type: 'roll', t: 0.9, roll: -6 },
        { type: 'width', t: 0.0, width: 22 },
        { type: 'width', t: 0.1, width: 18 },
        { type: 'width', t: 0.2, width: 14 },
        { type: 'width', t: 0.3, width: 13 },
        { type: 'width', t: 0.4, width: 16 },
        { type: 'width', t: 0.5, width: 12 },
        { type: 'width', t: 0.6, width: 12 },
        { type: 'width', t: 0.7, width: 20 },
        { type: 'width', t: 0.8, width: 24 },
        { type: 'width', t: 0.9, width: 22 }
      ]
    }]
  };

  const STARTER_TRACK = {
    version: 2,
    name: 'New Track',
    start: { path: 0, point: 0, reverse: false },
    disjointSeams: [],
    junctions: [],
    paths: [{
      closed: true,
      points: [
        { type: 'position', pos: [40, 0, 0], weight: 1 },
        { type: 'position', pos: [0, 0, 40], weight: 1 },
        { type: 'position', pos: [-40, 0, 0], weight: 1 },
        { type: 'position', pos: [0, 0, -40], weight: 1 },
        { type: 'roll', t: 0, roll: 0 },
        { type: 'roll', t: 1, roll: 0 },
        { type: 'width', t: 0, width: 18 },
        { type: 'width', t: 1, width: 18 }
      ]
    }]
  };

  global.TrackCore = {
    basis, basisDeriv, splitPoints, makeEvaluator, buildCenterline, buildEdges, buildFlatEdges,
    computeDisjointEdgeCuts, collapseSelfIntersections,
    evalRoll: evalRollSpline, evalWidth: evalWidthSpline,
    parseTrack, serializeTrack, normalizeStart,
    DEFAULT_TRACK, STARTER_TRACK, N_DEFAULT,
    // expose a deep-clone helper so callers never share point references
    cloneTrack: t => JSON.parse(JSON.stringify(t))
  };
})(typeof window !== 'undefined' ? window : globalThis);
