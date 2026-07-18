/* track-core.js — shared track math for the game (track.html) and the editor
 * (editor.html). Dependency-free (no three.js) so the 2D editor can use it too.
 *
 * A track is a closed loop of control points. Each control point carries a 3D
 * position AND the roll (banking) and width of the track there:
 *
 *   { pos: [x, y, z], roll: <degrees>, width: <full width>, weight: <NURBS w> }
 *
 * A rational, uniformly-knotted, closed cubic B-spline (NURBS) interpolates all
 * of these. +roll lifts the LEFT edge (banks into a right-hand turn).
 *
 * Public API (window.TrackCore):
 *   basis(u), basisDeriv(u)          - uniform cubic B-spline basis + derivative
 *   makeEvaluator(controlPoints)     - { evalTrack(g), CP_N }
 *   buildCenterline(cps, N)          - array of baked frames (plain {x,y,z})
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

  // --- rational closed cubic B-spline evaluator ------------------------------
  // Returns evalTrack(g), g in [0, CP_N): { pos, tangent(normalized), roll, width }.
  // pos/tangent are plain {x,y,z}; roll is in RADIANS; the whole cross-section
  // (pos, roll, width) shares the same rational basis (weights included).
  function makeEvaluator(controlPoints) {
    const CP_N = controlPoints.length;
    const cpVec = controlPoints.map(c => ({ x: c.pos[0], y: c.pos[1], z: c.pos[2] }));
    const cpRoll = controlPoints.map(c => (c.roll || 0) * DEG2RAD);
    const cpWidth = controlPoints.map(c => c.width);
    const cpW = controlPoints.map(c => (c.weight == null ? 1 : c.weight));
    const wrap = i => ((i % CP_N) + CP_N) % CP_N;

    function evalTrack(g) {
      const seg = Math.floor(g) % CP_N;
      const u = g - Math.floor(g);
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
    return { evalTrack, CP_N };
  }

  // --- bake N frames around the loop -----------------------------------------
  // Each frame: { pos, tangent, h, roll, width, halfW, edgeRight, normal }, all
  // vectors plain {x,y,z}. Consumers wrap these in their own vector type.
  function buildCenterline(controlPoints, N) {
    N = N || N_DEFAULT;
    const { evalTrack, CP_N } = makeEvaluator(controlPoints);
    const UP = { x: 0, y: 1, z: 0 };
    const out = [];
    for (let i = 0; i < N; i++) {
      const g = (i / N) * CP_N;
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

  // Collapse each folded run of a closed edge polyline to a sharp miter point.
  function trimEdge(pts, frames) {
    const N = pts.length;
    const fwd = new Array(N);
    for (let i = 0; i < N; i++) fwd[i] = segForward(pts[i], pts[(i + 1) % N], frames[i].tangent);
    const out = pts.map(p => ({ x: p.x, y: p.y, z: p.z }));

    const startFwd = fwd.indexOf(true);
    if (startFwd < 0) return out; // no forward segment at all (degenerate) -> leave

    let i = 0;
    while (i < N) {
      const seg = (startFwd + i) % N;
      if (fwd[seg]) { i++; continue; }

      // maximal run of backward (folded) segments starting at `seg`
      let len = 0;
      while (len < N && !fwd[(startFwd + i + len) % N]) len++;
      const s = seg;                          // first folded segment (vertex s -> s+1)
      const e = (startFwd + i + len - 1) % N;  // last folded segment

      // sharp corner = intersection of the segment entering the fold and the one leaving it
      const enterA = pts[(s - 1 + N) % N], enterB = pts[s];
      const leaveA = pts[(e + 1) % N], leaveB = pts[(e + 2) % N];
      const mid = {
        x: (enterB.x + leaveA.x) / 2, y: (enterB.y + leaveA.y) / 2, z: (enterB.z + leaveA.z) / 2
      };
      let X = lineIntersectXZ(enterA, enterB, leaveA, leaveB) || mid;
      // guard against runaway miters from near-parallel edges
      if (Math.hypot(X.x - mid.x, X.z - mid.z) > 6 * frames[s].halfW) X = mid;

      // collapse vertices s .. e+1 onto the miter point (zero-area strip there)
      let v = s; const last = (e + 1) % N;
      while (true) { out[v] = { x: X.x, y: X.y, z: X.z }; if (v === last) break; v = (v + 1) % N; }
      i += len;
    }
    return out;
  }

  // Build both trimmed edges from baked centerline frames.
  // Returns { left: [{x,y,z}...], right: [...] }, each length frames.length.
  function buildEdges(frames) {
    const left = [], right = [];
    for (let i = 0; i < frames.length; i++) {
      const c = frames[i];
      left.push(vaddScaled(c.pos, c.edgeRight, -c.halfW));
      right.push(vaddScaled(c.pos, c.edgeRight, c.halfW));
    }
    return { left: trimEdge(left, frames), right: trimEdge(right, frames) };
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

  function parseTrack(text) {
    const data = JSON.parse(text);
    const rawPoints = Array.isArray(data) ? data : data.controlPoints;
    if (!Array.isArray(rawPoints)) throw new Error('no controlPoints array found');
    if (rawPoints.length < 4) throw new Error('a closed cubic track needs at least 4 control points');
    const controlPoints = rawPoints.map(normalizePoint);
    return {
      version: (data && data.version) || 1,
      name: (data && data.name) || 'Untitled Track',
      samples: (data && data.samples) || N_DEFAULT,
      controlPoints
    };
  }

  function serializeTrack(track) {
    // Compact one-line-per-point formatting for readability.
    const pts = track.controlPoints.map(p =>
      '    { "pos": [' + p.pos.join(', ') + '], "roll": ' + p.roll +
      ', "width": ' + p.width + ', "weight": ' + p.weight + ' }'
    ).join(',\n');
    return '{\n' +
      '  "version": 1,\n' +
      '  "name": ' + JSON.stringify(track.name || 'Untitled Track') + ',\n' +
      '  "controlPoints": [\n' + pts + '\n  ]\n}\n';
  }

  // --- built-in tracks --------------------------------------------------------
  const DEFAULT_TRACK = {
    version: 1,
    name: 'Default Circuit',
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
  };

  const STARTER_TRACK = {
    version: 1,
    name: 'New Track',
    controlPoints: [
      { pos: [40, 0, 0], roll: 0, width: 18, weight: 1 },
      { pos: [0, 0, 40], roll: 0, width: 18, weight: 1 },
      { pos: [-40, 0, 0], roll: 0, width: 18, weight: 1 },
      { pos: [0, 0, -40], roll: 0, width: 18, weight: 1 }
    ]
  };

  global.TrackCore = {
    basis, basisDeriv, makeEvaluator, buildCenterline, buildEdges,
    parseTrack, serializeTrack,
    DEFAULT_TRACK, STARTER_TRACK, N_DEFAULT,
    // expose a deep-clone helper so callers never share point references
    cloneTrack: t => JSON.parse(JSON.stringify(t))
  };
})(typeof window !== 'undefined' ? window : globalThis);
