/* track-core.js — shared track math for the game (track.html) and the editor
 * (editor.html). Dependency-free (no three.js) so the 2D editor can use it too.
 *
 * A track is composed of one or more PATHS. Each path is either a closed loop
 * or an open curve, and holds an ordered array of control points. Each control
 * point carries a 3D position AND the roll (banking) and width of the track
 * there:
 *
 *   { pos: [x, y, z], roll: <degrees>, width: <full width>, weight: <NURBS w> }
 *
 * A rational, uniformly-knotted cubic B-spline (NURBS) interpolates all of
 * these along each path (wrapping for closed paths, clamped at the ends for
 * open ones). +roll lifts the LEFT edge (banks into a right-hand turn).
 *
 * track = { version, name, samples, paths: [ { closed, controlPoints } ],
 *            start: { path, point, reverse } }
 * `start` picks which control point the player begins at (nearest baked
 * sample to it) and whether they face along the path's natural (parametric)
 * direction or the reverse of it.
 *
 * Public API (window.TrackCore):
 *   basis(u), basisDeriv(u)          - uniform cubic B-spline basis + derivative
 *   makeEvaluator(cps, closed)       - { evalTrack(g), CP_N, closed }
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
  function makeEvaluator(controlPoints, closed) {
    closed = closed !== false;
    const CP_N = controlPoints.length;
    const cpVec = controlPoints.map(c => ({ x: c.pos[0], y: c.pos[1], z: c.pos[2] }));
    const cpRoll = controlPoints.map(c => (c.roll || 0) * DEG2RAD);
    const cpWidth = controlPoints.map(c => c.width);
    const cpW = controlPoints.map(c => (c.weight == null ? 1 : c.weight));
    const wrap = closed
      ? i => ((i % CP_N) + CP_N) % CP_N
      : i => Math.max(0, Math.min(CP_N - 1, i));

    function evalTrack(g) {
      const seg = Math.floor(g);
      const u = g - seg;
      const b = basis(u), db = basisDeriv(u);
      const idx = [wrap(seg - 1), wrap(seg), wrap(seg + 1), wrap(seg + 2)];

      let num = { x: 0, y: 0, z: 0 }, dnum = { x: 0, y: 0, z: 0 };
      let den = 0, dden = 0, rollNum = 0, widthNum = 0;
      for (let k = 0; k < 4; k++) {
        const j = idx[k];
        const w = cpW[j];
        const bw = b[k] * w, dbw = db[k] * w;
        num = vaddScaled(num, cpVec[j], bw);
        dnum = vaddScaled(dnum, cpVec[j], dbw);
        den += bw; dden += dbw;
        rollNum += bw * cpRoll[j];
        widthNum += bw * cpWidth[j];
      }
      const pos = vscale(num, 1 / den);
      // rational derivative via quotient rule: (N'D - N D') / D^2, normalized
      const tangent = vnorm(vscale(vaddScaled(vscale(dnum, den), num, -dden), 1 / (den * den)));
      return { pos, tangent, roll: rollNum / den, width: widthNum / den };
    }
    return { evalTrack, CP_N, closed };
  }

  // --- bake N frames along a path ---------------------------------------------
  // Each frame: { pos, tangent, h, roll, width, halfW, edgeRight, normal }, all
  // vectors plain {x,y,z}. Consumers wrap these in their own vector type.
  // Closed paths bake N samples spanning the full loop [0, CP_N); open paths
  // bake N samples spanning [0, CP_N-1] inclusive of both endpoints.
  function buildCenterline(controlPoints, N, closed) {
    closed = closed !== false;
    N = N || N_DEFAULT;
    const { evalTrack, CP_N } = makeEvaluator(controlPoints, closed);
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

  // --- JSON schema: parse / validate / serialize -----------------------------
  function normalizePoint(p, i) {
    if (!p || !Array.isArray(p.pos) || p.pos.length !== 3 || p.pos.some(n => typeof n !== 'number')) {
      throw new Error('control point ' + i + ': pos must be [x,y,z] numbers');
    }
    const num = (v, d) => (typeof v === 'number' && isFinite(v) ? v : d);
    return {
      pos: [p.pos[0], p.pos[1], p.pos[2]],
      roll: num(p.roll, 0),
      width: Math.max(1, num(p.width, 12)),
      weight: Math.max(0.01, num(p.weight, 1))
    };
  }

  function normalizePath(rawPath, i) {
    const rawPoints = Array.isArray(rawPath) ? rawPath : rawPath && rawPath.controlPoints;
    if (!Array.isArray(rawPoints)) throw new Error('path ' + i + ': no controlPoints array found');
    if (rawPoints.length < 4) throw new Error('path ' + i + ': a track path needs at least 4 control points');
    return {
      closed: !(rawPath && rawPath.closed === false),
      controlPoints: rawPoints.map(normalizePoint)
    };
  }

  // Clamp a start descriptor to valid path/point indices for the given paths.
  function normalizeStart(rawStart, paths) {
    let path = (rawStart && Number.isInteger(rawStart.path)) ? rawStart.path : 0;
    path = Math.max(0, Math.min(paths.length - 1, path));
    let point = (rawStart && Number.isInteger(rawStart.point)) ? rawStart.point : 0;
    point = Math.max(0, Math.min(paths[path].controlPoints.length - 1, point));
    const reverse = !!(rawStart && rawStart.reverse);
    return { path, point, reverse };
  }

  // Accepts either the current { paths: [{closed, controlPoints}, ...] } schema
  // or the legacy single-closed-loop { controlPoints: [...] } schema.
  function parseTrack(text) {
    const data = JSON.parse(text);
    let rawPaths;
    if (Array.isArray(data)) rawPaths = [{ closed: true, controlPoints: data }];
    else if (Array.isArray(data.paths)) rawPaths = data.paths;
    else if (Array.isArray(data.controlPoints)) rawPaths = [{ closed: true, controlPoints: data.controlPoints }];
    else throw new Error('no paths or controlPoints array found');
    if (rawPaths.length < 1) throw new Error('a track needs at least one path');
    const paths = rawPaths.map(normalizePath);
    return {
      version: (data && data.version) || 2,
      name: (data && data.name) || 'Untitled Track',
      samples: (data && data.samples) || N_DEFAULT,
      paths,
      start: normalizeStart(data && data.start, paths)
    };
  }

  function serializeTrack(track) {
    // Compact one-line-per-point formatting for readability.
    const pathsJson = track.paths.map(path => {
      const pts = path.controlPoints.map(p =>
        '      { "pos": [' + p.pos.join(', ') + '], "roll": ' + p.roll +
        ', "width": ' + p.width + ', "weight": ' + p.weight + ' }'
      ).join(',\n');
      return '    { "closed": ' + (path.closed !== false) + ', "controlPoints": [\n' + pts + '\n    ] }';
    }).join(',\n');
    const start = normalizeStart(track.start, track.paths);
    return '{\n' +
      '  "version": 2,\n' +
      '  "name": ' + JSON.stringify(track.name || 'Untitled Track') + ',\n' +
      '  "start": { "path": ' + start.path + ', "point": ' + start.point + ', "reverse": ' + start.reverse + ' },\n' +
      '  "paths": [\n' + pathsJson + '\n  ]\n}\n';
  }

  // --- built-in tracks --------------------------------------------------------
  const DEFAULT_TRACK = {
    version: 2,
    name: 'Default Circuit',
    start: { path: 0, point: 0, reverse: false },
    paths: [{
      closed: true,
      controlPoints: [
        { pos: [90, 0, 0], roll: 0, width: 22, weight: 1 },
        { pos: [70, 4, 46], roll: -14, width: 18, weight: 1 },
        { pos: [18, 8, 60], roll: -22, width: 14, weight: 1 },
        { pos: [-34, 5, 54], roll: -18, width: 13, weight: 1 },
        { pos: [-74, 0, 30], roll: -10, width: 16, weight: 1 },
        { pos: [-92, -4, -6], roll: 16, width: 12, weight: 1 },
        { pos: [-66, -3, -40], roll: 20, width: 12, weight: 1 },
        { pos: [-16, 1, -56], roll: 8, width: 20, weight: 1 },
        { pos: [40, 3, -48], roll: -12, width: 24, weight: 1 },
        { pos: [80, 1, -22], roll: -6, width: 22, weight: 1 }
      ]
    }]
  };

  const STARTER_TRACK = {
    version: 2,
    name: 'New Track',
    start: { path: 0, point: 0, reverse: false },
    paths: [{
      closed: true,
      controlPoints: [
        { pos: [40, 0, 0], roll: 0, width: 18, weight: 1 },
        { pos: [0, 0, 40], roll: 0, width: 18, weight: 1 },
        { pos: [-40, 0, 0], roll: 0, width: 18, weight: 1 },
        { pos: [0, 0, -40], roll: 0, width: 18, weight: 1 }
      ]
    }]
  };

  global.TrackCore = {
    basis, basisDeriv, makeEvaluator, buildCenterline, buildEdges,
    parseTrack, serializeTrack, normalizeStart,
    DEFAULT_TRACK, STARTER_TRACK, N_DEFAULT,
    // expose a deep-clone helper so callers never share point references
    cloneTrack: t => JSON.parse(JSON.stringify(t))
  };
})(typeof window !== 'undefined' ? window : globalThis);
