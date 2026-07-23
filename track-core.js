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
 *     { type: 'crossSection', t: 0..1, curvature, tightness, thickness },
 *     ...
 *   ]
 *
 * A crossSection point shapes the road's profile across its width: `curvature`
 * (-1..1) crowns or dishes it, `tightness` is the exponent on that arc, and
 * `thickness` extrudes the whole profile DOWNWARD into a shell with an
 * underside and side walls (0 = the old zero-thickness sheet). curvature and
 * tightness are scale-invariant; thickness is a length and scales with the
 * world's units.
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
 *            meshAssets: { <assetId>: { name, railHeight, mesh } },
 *            meshes: [ { id, asset, x, z, rotation, elevation } ],
 *            handling: { maxSpeed, accel, turnSpeed, weight },
 *            zones: [{ id, effect, width, length, host, ...effectParams }],
 *            start: { path, point, reverse } }
 * `zones` are flat rectangular areas floating on a surface that fire an effect
 * when driven over. Each is hosted on a path (host.kind 'path', by t + lateral
 * offset, oriented along the track) or a mesh region (host.kind 'mesh', by
 * world x/z + yaw at the region's elevation). effect 'velocityChange' carries
 * { factor, duration }; 'startGrid' is a visual marker. See normalizeZones.
 * `handling` is the per-track ship config the game reads (m/s, m/s^2, deg/s,
 * kg); a track without it drives on DEFAULT_HANDLING. See normalizeHandling.
 * meshAssets/meshes carry flat drivable MESH REGIONS imported from the
 * geometry-js editor (schema 4+; older tracks simply have none). track-core.js
 * only validates and carries them -- js/track-mesh.js owns the geometry and the
 * geometry-js dependency, keeping this file dependency-free.
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
 *   crossSectionHeight(curvature, tightness, v, chordWidth)
 *                                    - road-surface rise above the flat chord
 *                                      (v: 0 left edge .. 1 right edge), plus
 *                                      crossSectionHeightDerivative for normals
 *   crossSectionBreakpoints(curvature, tightness, chordWidth)
 *                                    - adaptive v breakpoints (0..1) for one
 *                                      cross-section ring's mesh, denser where
 *                                      the profile curves more sharply
 *   crossSectionStitchPoint(ownBreaks, v, pointAt)
 *                                    - a ring's point at v, constrained to its
 *                                      OWN polyline (ownBreaks); required
 *                                      whenever a neighboring ring's finer
 *                                      breakpoints force this ring to be
 *                                      sampled at a v it didn't pick itself,
 *                                      or two differently-adaptive neighbors
 *                                      leave a crack at the shared ring
 *   buildAdaptiveMeshFrames(...)     - mesh-only (never physics) adaptive
 *                                      longitudinal ring spacing: denser on
 *                                      sharp turns/hills, sparser on straights,
 *                                      exactly preserving every physics frame
 *                                      a self-intersection fold touched
 *   parseTrack(text)                 - JSON string -> validated track object
 *   serializeTrack(track)            - track object -> pretty JSON string
 *   DEFAULT_TRACK, STARTER_TRACK     - built-in tracks
 *   N_DEFAULT                        - default sample count
 */
(function (global) {
  'use strict';

  const N_DEFAULT = 400;
  const DEG2RAD = Math.PI / 180;
  const DEFAULT_CROSS_SECTION_CURVATURE = 0;
  const DEFAULT_CROSS_SECTION_TIGHTNESS = 1;
  // How far the road is extruded DOWN from its driving surface, turning the
  // ribbon from an infinitely thin sheet into a shell with a visible underside
  // and side walls. Unlike curvature (dimensionless) and tightness (an
  // exponent), this is a LENGTH -- it scales with the world's unit scale, and
  // scaleRawTrackData has to know about it. 0 means the old zero-thickness
  // sheet, which is the escape hatch for anyone who wants the previous look.
  const DEFAULT_CROSS_SECTION_THICKNESS = 4;
  const COLLISION_WALL_MARGIN = 1.8;
  const DEFAULT_RAIL_HEIGHT = 6;
  const DEFAULT_WIDTH = 36;
  // Per-track ship handling. Configurable in the editor, saved in the track
  // JSON, read by the game; a track without a `handling` section drives with
  // exactly these values. maxSpeed/accel are m/s and m/s^2 (1 unit = 1 metre),
  // turnSpeed is DEGREES per second (friendlier to author than rad/s; the game
  // converts), weight is kilograms. weight 1000 is the neutral point the
  // collision reaction is tuned around -- heavier bounces less, lighter more.
  // Brake/friction/reverse are deliberately NOT here: they stay fixed engine
  // constants (see js/track-game.js).
  const DEFAULT_HANDLING = { maxSpeed: 140, accel: 71, turnSpeed: 137.5, weight: 1000 };
  // Schema 5 doubled the world's unit scale: every length in a track (control
  // point positions, widths, elevations, mesh geometry) is twice what the same
  // track measured under schema 4, and the game's ship, speeds and gravity were
  // scaled to match. Nothing about how a track looks or drives changed -- only
  // the absolute units. Older files are converted once, on load.
  const TRACK_SCHEMA_VERSION = 7;
  const LEGACY_UNIT_SCALE = 2;
  // The unit doubling happened at schema 5 and only there, so the migration is
  // keyed to that version and NOT to TRACK_SCHEMA_VERSION. Those were the same
  // number while 5 was current, which made `sourceVersion < TRACK_SCHEMA_VERSION`
  // look right -- but it meant the next bump, whatever it was for, would silently
  // re-double every schema-5 track ever saved. Schema 6 only adds cross-section
  // thickness; it changes no units.
  const UNIT_SCALE_SCHEMA_VERSION = 5;
  const finiteOr = (n, fallback, min) => {
    if (typeof n !== 'number' || !isFinite(n)) return fallback;
    return typeof min === 'number' ? Math.max(min, n) : n;
  };
  const clampRange = (v, lo, hi, fallback) => {
    v = Number(v);
    return isFinite(v) ? Math.max(lo, Math.min(hi, v)) : fallback;
  };
  // Fill in and clamp a track's handling section. A missing/partial/garbage
  // section falls back field-by-field to DEFAULT_HANDLING, which is how "no
  // handling in the JSON -> defaults" is realised: parseTrack always runs this,
  // so every downstream consumer sees a complete, sane object.
  function normalizeHandling(raw) {
    const r = (raw && typeof raw === 'object') ? raw : {};
    return {
      maxSpeed: clampRange(r.maxSpeed, 10, 1000, DEFAULT_HANDLING.maxSpeed),
      accel: clampRange(r.accel, 5, 1000, DEFAULT_HANDLING.accel),
      turnSpeed: clampRange(r.turnSpeed, 10, 720, DEFAULT_HANDLING.turnSpeed),
      weight: clampRange(r.weight, 50, 100000, DEFAULT_HANDLING.weight)
    };
  }
  const clampSignedUnit = n => (typeof n === 'number' && isFinite(n) ? Math.max(-1, Math.min(1, n)) : 0);
  const clampTightness = n => (typeof n === 'number' && isFinite(n) ? Math.max(0.2, Math.min(4, n)) : DEFAULT_CROSS_SECTION_TIGHTNESS);
  // No upper bound: a deliberately deep shell is a legitimate look, and unlike
  // tightness there is no exponent to blow up. Negative is meaningless, though --
  // it would extrude the shell up through the road surface.
  const clampThickness = n => (typeof n === 'number' && isFinite(n) ? Math.max(0, n) : DEFAULT_CROSS_SECTION_THICKNESS);

  // --- cross-section profile -------------------------------------------------
  // How far the road surface rises above the flat chord between its two edges,
  // as a function of `v` (0 = left edge, 0.5 = centre, 1 = right edge). At
  // curvature 1 and tightness 1 this is a semicircle spanning the chord;
  // tightness is an exponent on that arc (>1 flattens the middle and steepens
  // the edges, <1 does the reverse), and negative curvature dishes the road
  // instead of crowning it. Both edges stay put at every setting, so changing
  // the profile never changes the road's width or where its walls are.
  //
  // This lives here, in the shared core, because THREE consumers draw this same
  // surface -- the game's ribbon mesh and physics, the editor's cross-section
  // preview, and the USD exporter -- and they must agree exactly or an exported
  // track no longer matches the one that was authored and driven.
  function crossSectionHeight(curvature, tightness, v, chordWidth) {
    const c = clampSignedUnit(curvature);
    if (!c) return 0;
    const u = 2 * Math.max(0, Math.min(1, v)) - 1;   // -1 left edge, 0 centre, 1 right edge
    const base = Math.sqrt(Math.max(0, 1 - u * u));
    return c * (chordWidth / 2) * Math.pow(base, clampTightness(tightness));
  }

  // d(height)/dv, used to build the surface normal across the road. The edges
  // are nudged off 0/1 because the arc's slope is infinite exactly there.
  function crossSectionHeightDerivative(curvature, tightness, v, chordWidth) {
    const c = clampSignedUnit(curvature), k = clampTightness(tightness);
    if (!c) return 0;
    const u = 2 * Math.max(0.001, Math.min(0.999, v)) - 1;
    const base = Math.sqrt(Math.max(0.000001, 1 - u * u));
    return c * (chordWidth / 2) * k * (-2 * u) * Math.pow(base, k - 2);
  }

  // How far off (in world units) the mesh is allowed to deviate from the true
  // crossSectionHeight curve before a cell gets split. Fixed engine constant,
  // not authored per-track -- it is a mesh-fidelity knob like
  // CROSS_SECTION_SEGMENTS used to be, not a track-shape knob like curvature.
  const CROSS_SECTION_SAGITTA_TOLERANCE = 0.1;
  // Hard cap so a steep edge (slope approaches infinite at v=0/1 for some
  // tightness values) can't blow up the triangle budget regardless of how
  // large the sagitta error reports.
  const CROSS_SECTION_MAX_DEPTH = 5;

  // Adaptive v-partition for one cross-section ring: recursively bisects any
  // cell whose true profile deviates (at its midpoint) from the straight line
  // between its two ends by more than CROSS_SECTION_SAGITTA_TOLERANCE, so flat
  // stretches of road stay coarse and tightly-curved ones get subdivided where
  // they actually need it. Returns a sorted array of v breakpoints from 0 to 1
  // (always including both ends), shared by the top surface, the shell
  // underside and the shell's end caps for this ring so they all agree.
  function crossSectionBreakpoints(curvature, tightness, chordWidth) {
    const c = clampSignedUnit(curvature);
    const breaks = new Set([0, 1]);
    if (!c) return [0, 1];
    const k = clampTightness(tightness);
    const height = v => crossSectionHeight(c, k, v, chordWidth);
    const split = (v0, v1, depth) => {
      const vMid = (v0 + v1) / 2;
      const hMid = height(vMid);
      const hChord = (height(v0) + height(v1)) / 2;
      if (depth < CROSS_SECTION_MAX_DEPTH && Math.abs(hMid - hChord) > CROSS_SECTION_SAGITTA_TOLERANCE) {
        split(v0, vMid, depth + 1);
        breaks.add(vMid);
        split(vMid, v1, depth + 1);
      }
    };
    // Base partition of 2 cells: [0, 0.5], [0.5, 1].
    split(0, 0.5, 0);
    breaks.add(0.5);
    split(0.5, 1, 0);
    return Array.from(breaks).sort((a, b) => a - b);
  }

  // A ring shared between two neighboring strips is asked for a DIFFERENT set
  // of "foreign" v's by each neighbor (whichever neighbor is finer), since
  // adaptivity is decided per ring independently. Evaluating the true
  // analytic surface at a foreign v (as if it were just another sample of the
  // same continuous curve) is WRONG despite being exact: it silently moves the
  // ring's rendered edge depending on which neighbor asked, because the ring's
  // OWN edge, wherever it's actually drawn, is the polyline through its own
  // fixed breakpoints (ownBreaks) -- not the smooth curve itself. Two
  // different sets of "extra, exact" points define two different polylines
  // for the same edge, which is exactly the crack this function exists to
  // prevent.
  //
  // The fix: for v already in ownBreaks, return the true point (pointAt(v)).
  // For a foreign v, linearly interpolate between the ring's own two REAL
  // neighboring vertices instead of sampling the curve directly. A lerped
  // point is collinear with (not a deviation from) the straight edge segment
  // already there, so inserting it does not change the ring's rendered shape
  // at all -- meaning the ring's edge is bit-for-bit identical no matter which
  // neighboring strip, or how many foreign v's, ask for it.
  function crossSectionStitchPoint(ownBreaks, v, pointAt) {
    if (ownBreaks.indexOf(v) !== -1) return pointAt(v);
    let lo = ownBreaks[0], hi = ownBreaks[ownBreaks.length - 1];
    for (let i = 0; i < ownBreaks.length - 1; i++) {
      if (ownBreaks[i] <= v && v <= ownBreaks[i + 1]) { lo = ownBreaks[i]; hi = ownBreaks[i + 1]; break; }
    }
    if (lo === hi) return pointAt(lo);
    const a = pointAt(lo), b = pointAt(hi);
    const t = (v - lo) / (hi - lo);
    return [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
  }

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
    const controlPoints = [], rollPoints = [], widthPoints = [], crossSectionPoints = [];
    for (const p of points) {
      if (p.type === 'roll') rollPoints.push(p);
      else if (p.type === 'width') widthPoints.push(p);
      else if (p.type === 'crossSection') crossSectionPoints.push(p);
      else controlPoints.push(p);
    }
    rollPoints.sort((a, b) => a.t - b.t);
    widthPoints.sort((a, b) => a.t - b.t);
    crossSectionPoints.sort((a, b) => a.t - b.t);
    return { controlPoints, rollPoints, widthPoints, crossSectionPoints };
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
  function evalCrossSectionSpline(crossSectionPoints, closed, tQuery) {
    return clampSignedUnit(evalScalarSpline(crossSectionPoints, closed, tQuery, 'curvature'));
  }
  function evalCrossSectionTightnessSpline(crossSectionPoints, closed, tQuery) {
    return clampTightness(evalScalarSpline(crossSectionPoints, closed, tQuery, 'tightness'));
  }
  function evalCrossSectionThicknessSpline(crossSectionPoints, closed, tQuery) {
    return clampThickness(evalScalarSpline(crossSectionPoints, closed, tQuery, 'thickness'));
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
  function makeEvaluator(controlPoints, closed, rollPoints, widthPoints, crossSectionPoints) {
    closed = closed !== false;
    const CP_N = controlPoints.length;
    const cpVec = controlPoints.map(c => ({ x: c.pos[0], y: c.pos[1], z: c.pos[2] }));
    const cpW = controlPoints.map(c => (c.weight == null ? 1 : c.weight));
    const rp = (rollPoints && rollPoints.length >= 1) ? rollPoints : [{ t: 0, roll: 0 }, { t: 1, roll: 0 }];
    const wp = (widthPoints && widthPoints.length >= 1) ? widthPoints : [{ t: 0, width: DEFAULT_WIDTH }, { t: 1, width: DEFAULT_WIDTH }];
    const defaultCrossSection = {
      t: 0, curvature: DEFAULT_CROSS_SECTION_CURVATURE,
      tightness: DEFAULT_CROSS_SECTION_TIGHTNESS, thickness: DEFAULT_CROSS_SECTION_THICKNESS
    };
    const xp = (crossSectionPoints && crossSectionPoints.length >= 1)
      ? crossSectionPoints
      : [{ ...defaultCrossSection, t: 0 }, { ...defaultCrossSection, t: 1 }];
    const gMax = (closed ? CP_N : CP_N - 1) || 1;
    const wrap = closed
      ? i => ((i % CP_N) + CP_N) % CP_N
      : i => Math.max(0, Math.min(CP_N - 1, i));

    // Every cross-section value sampled at one t. Factored out because all three
    // exits below need the same set, and the set grows: it was curvature alone,
    // then curvature + tightness, now + thickness.
    const crossSectionAt = t => ({
      crossSectionCurvature: evalCrossSectionSpline(xp, closed, t),
      crossSectionTightness: evalCrossSectionTightnessSpline(xp, closed, t),
      crossSectionThickness: evalCrossSectionThicknessSpline(xp, closed, t)
    });

    function evalTrack(g) {
      if (!closed && CP_N > 0) {
        if (g <= 0) {
          const pos = cpVec[0];
          const tangent = vnorm(CP_N > 1 ? vsub(cpVec[1], cpVec[0]) : { x: 0, y: 0, z: 1 });
          return { pos, tangent, roll: evalRollSpline(rp, closed, 0), width: evalWidthSpline(wp, closed, 0), ...crossSectionAt(0) };
        }
        if (g >= CP_N - 1) {
          const pos = cpVec[CP_N - 1];
          const tangent = vnorm(CP_N > 1 ? vsub(cpVec[CP_N - 1], cpVec[CP_N - 2]) : { x: 0, y: 0, z: 1 });
          return { pos, tangent, roll: evalRollSpline(rp, closed, 1), width: evalWidthSpline(wp, closed, 1), ...crossSectionAt(1) };
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
      return { pos, tangent, roll, width, ...crossSectionAt(t) };
    }
    return { evalTrack, CP_N, closed };
  }

  // --- bake N frames along a path ---------------------------------------------
  // Each frame: { pos, tangent, h, roll, width, halfW, edgeRight, normal }, all
  // vectors plain {x,y,z}. Consumers wrap these in their own vector type.
  // Closed paths bake N samples spanning the full loop [0, CP_N); open paths
  // bake N samples spanning [0, CP_N-1] inclusive of both endpoints.
  //
  // Factored out of buildCenterline's loop so js/track-game.js's adaptive
  // longitudinal mesh sampling (buildAdaptiveMeshFrames below) can build the
  // exact same frame shape at arbitrary extra g values, without a second,
  // divergence-prone copy of this math.
  function frameFromSample(sample) {
    const { pos, tangent, roll, width } = sample;
    const UP = { x: 0, y: 1, z: 0 };
    const h = vnorm(vcross(UP, tangent));
    let baseNormal = vnorm(vcross(tangent, h));
    if (baseNormal.y < 0) baseNormal = vscale(baseNormal, -1);

    // +roll lifts the LEFT edge -> roll the cross-section by -roll about tangent
    const cosR = Math.cos(-roll), sinR = Math.sin(-roll);
    const edgeRight = vadd(vscale(h, cosR), vscale(baseNormal, sinR));
    const normal = vnorm(vaddScaled(vscale(baseNormal, cosR), h, -sinR));

    return { ...sample, h, halfW: width / 2, edgeRight, normal };
  }

  function buildCenterline(controlPoints, N, closed, rollPoints, widthPoints, crossSectionPoints) {
    closed = closed !== false;
    N = N || N_DEFAULT;
    const { evalTrack, CP_N } = makeEvaluator(controlPoints, closed, rollPoints, widthPoints, crossSectionPoints);
    const out = [];
    for (let i = 0; i < N; i++) {
      const g = closed ? (i / N) * CP_N : (N > 1 ? (i / (N - 1)) * (CP_N - 1) : 0);
      out.push(frameFromSample(evalTrack(g)));
    }
    return out;
  }

  // --- adaptive physics sample count -----------------------------------------
  // How many centerline frames PHYSICS/collision rides on is chosen per path
  // from its DRIVEN length, holding sample spacing roughly constant so a longer
  // track keeps the same corridor fidelity -- and the along-track collision
  // tolerances (tuned against that spacing) stay valid untouched. This is
  // deliberately separate from two other counts it is easy to conflate:
  //   * the RENDER mesh, which is resampled adaptively and independently
  //     (buildAdaptiveMeshFrames), so a coarse physics count never shows as a
  //     blocky road; and
  //   * N_DEFAULT / track.samples, which stay the USD-export ring count and the
  //     floor here -- the exporter is untouched by this.
  // 1 world unit = 1 metre (see CONTEXT.md), so SAMPLE_SPACING reads as metres.
  const SAMPLE_SPACING = 6;           // target metres between physics samples
  const SAMPLE_COUNT_MIN = N_DEFAULT; // never coarser than the legacy fixed bake
  const SAMPLE_COUNT_MAX = 2000;      // caps per-frame sampleTrack cost on huge tracks

  function estimateCenterlineLength(controlPoints, closed, rollPoints, widthPoints, crossSectionPoints) {
    const frames = buildCenterline(controlPoints, 200, closed, rollPoints, widthPoints, crossSectionPoints);
    let len = 0;
    for (let i = 1; i < frames.length; i++) len += vlen(vsub(frames[i].pos, frames[i - 1].pos));
    if (closed && frames.length) len += vlen(vsub(frames[0].pos, frames[frames.length - 1].pos));
    return len;
  }

  // Per-path physics sample count for the current geometry, clamped to
  // [N_DEFAULT, SAMPLE_COUNT_MAX]. Deterministic (depends only on the control
  // geometry), so any two consumers that call it agree.
  function adaptiveSampleCount(controlPoints, closed, rollPoints, widthPoints, crossSectionPoints) {
    const len = estimateCenterlineLength(controlPoints, closed !== false, rollPoints, widthPoints, crossSectionPoints);
    const n = Math.round(len / SAMPLE_SPACING);
    return Math.max(SAMPLE_COUNT_MIN, Math.min(SAMPLE_COUNT_MAX, n));
  }

  // --- adaptive longitudinal mesh sampling (rendering only) -------------------
  // Physics/collision always rides on buildCenterline's fixed, uniform-in-
  // parameter N_DEFAULT frames -- untouched by any of this. This section is
  // consumed ONLY by the game's mesh builder, to place MESH rings more densely
  // on sharp turns/hills and more sparsely on long straights, mirroring
  // crossSectionBreakpoints but along the path instead of across it.
  const LONGITUDINAL_SAGITTA_TOLERANCE = 0.1;
  // Even a perfectly straight run (zero sagitta forever) never gets a gap
  // wider than this -- bounds triangle stretching/UV distortion on long
  // straights, and implicitly bounds how much roll/width/thickness can drift
  // between two samples even where position itself is dead straight.
  const LONGITUDINAL_MAX_DISTANCE = 40;
  // Defensive cap, same philosophy as CROSS_SECTION_MAX_DEPTH: the max-distance
  // rule already guarantees termination, but this bounds it further against a
  // pathological near-cusp where sagitta might not shrink as fast as expected.
  const LONGITUDINAL_MAX_DEPTH = 10;

  // Adaptive g-partition for one fold-free run of the centerline: recursively
  // bisects any cell whose true 3D position (at its midpoint) deviates from a
  // straight line between its two ends by more than
  // LONGITUDINAL_SAGITTA_TOLERANCE, or whose chord length exceeds
  // LONGITUDINAL_MAX_DISTANCE. Unlike crossSectionBreakpoints there is no
  // forced base partition: a run that is both straight and short collapses to
  // just its two ends, which is what lets a long straight use FEWER samples
  // than the fixed baking used to produce.
  function longitudinalBreakpoints(g0, g1, posAt) {
    const breaks = new Set([g0, g1]);
    const split = (a, b, depth) => {
      const mid = (a + b) / 2;
      const pa = posAt(a), pb = posAt(b), pm = posAt(mid);
      const chordMid = { x: (pa.x + pb.x) / 2, y: (pa.y + pb.y) / 2, z: (pa.z + pb.z) / 2 };
      const sagitta = vlen(vsub(pm, chordMid));
      const chordLen = vlen(vsub(pb, pa));
      if (depth < LONGITUDINAL_MAX_DEPTH && (sagitta > LONGITUDINAL_SAGITTA_TOLERANCE || chordLen > LONGITUDINAL_MAX_DISTANCE)) {
        split(a, mid, depth + 1);
        breaks.add(mid);
        split(mid, b, depth + 1);
      }
    };
    split(g0, g1, 0);
    return Array.from(breaks).sort((a, b) => a - b);
  }

  // Which of buildCenterline's frames had their edge actually repositioned by
  // self-intersection trimming (trimEdge/removeLocalEdgeSelfIntersections),
  // by comparing the real (possibly-trimmed) edge against the plain untrimmed
  // half-width offset. Trimming snaps a whole folded run to one shared mitre
  // point, so any real change is well outside float noise. Every affected
  // frame must be preserved exactly, verbatim -- these are the ONLY frames
  // where the game's road mesh and the physics corridor must agree exactly,
  // and adaptive resampling never touches them.
  function foldAffectedIndices(raw, edges) {
    const EPS = 1e-6;
    const affected = new Array(raw.length).fill(false);
    for (let i = 0; i < raw.length; i++) {
      const f = raw[i];
      const untrimmedLeft = vaddScaled(f.pos, f.edgeRight, -f.halfW);
      const untrimmedRight = vaddScaled(f.pos, f.edgeRight, f.halfW);
      if (vlen(vsub(edges.left[i], untrimmedLeft)) > EPS || vlen(vsub(edges.right[i], untrimmedRight)) > EPS) {
        affected[i] = true;
      }
    }
    return affected;
  }

  // Builds a MESH-ONLY frame/edge array from the already-baked physics
  // raw/edges: every fold-affected frame (see foldAffectedIndices) is carried
  // through byte-for-byte, so the rendered road/shell match the physics
  // corridor exactly at every mitred corner; every fold-free run of frames in
  // between is freely re-sampled by longitudinalBreakpoints, which can place
  // MORE frames there (sharp bends/hills) or FEWER (long straights) than the
  // original fixed baking. The very first and last original frame are always
  // kept as fixed endpoints; a closed path's wrap-around join (last frame back
  // to the first) is deliberately never merged into an adaptive run, so this
  // never has to reason about g wrapping past CP_N.
  function buildAdaptiveMeshFrames(controlPoints, closed, rollPoints, widthPoints, crossSectionPoints, raw, edges) {
    const n = raw.length;
    if (n < 3) return { frames: raw.slice(), edges: { left: edges.left.slice(), right: edges.right.slice() } };
    const affected = foldAffectedIndices(raw, edges);
    const { evalTrack, CP_N } = makeEvaluator(controlPoints, closed, rollPoints, widthPoints, crossSectionPoints);
    const gAt = i => closed ? (i / n) * CP_N : (n > 1 ? (i / (n - 1)) * (CP_N - 1) : 0);
    const posAt = g => evalTrack(g).pos;

    const frames = [], left = [], right = [];
    const pushExact = i => { frames.push(raw[i]); left.push(edges.left[i]); right.push(edges.right[i]); };
    const pushAdaptive = g => {
      const frame = frameFromSample(evalTrack(g));
      frames.push(frame);
      left.push(vaddScaled(frame.pos, frame.edgeRight, -frame.halfW));
      right.push(vaddScaled(frame.pos, frame.edgeRight, frame.halfW));
    };

    pushExact(0);
    let i = 0;
    const last = n - 1;
    while (i < last) {
      if (affected[i] || affected[i + 1]) {
        pushExact(i + 1);
        i++;
        continue;
      }
      let j = i + 1;
      while (j < last && !affected[j] && !affected[j + 1]) j++;
      const breaks = longitudinalBreakpoints(gAt(i), gAt(j), posAt);
      for (let k = 1; k < breaks.length - 1; k++) pushAdaptive(breaks[k]);
      pushExact(j);
      i = j;
    }
    return { frames, edges: { left, right } };
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

      // Maximal run of backward (folded) segments starting at `seg`.
      //
      // The bound differs by case because the INDEX differs. Closed paths read
      // fwd[] modulo segCount, so any `len` is a valid index and `len` itself is
      // the right thing to cap (one full lap). Open paths read fwd[i + len]
      // straight, so the cap has to be on `i + len`: past the end fwd[] yields
      // undefined, `!undefined` is true, and the scan would run on to
      // len === segCount. `e` then overshoots the last segment and pts[nextIdx(e)]
      // is undefined, which threw out of buildEdges. That fires whenever an open
      // curve's inner edge is still folded at its FINAL segment -- e.g. a wide
      // road whose last two control points nearly coincide -- and it took the
      // game's startup and the editor's draw loop down with it.
      let len = 0;
      while ((closed ? len < segCount : i + len < segCount) &&
             !fwd[closed ? (startFwd + i + len) % segCount : i + len]) len++;
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

  // Convert cleaned left/right edge polylines into the signed lateral offsets
  // used by runtime collision. This is the game's physical wall representation:
  // each wall is stored per centerline sample as distance along that frame's
  // banked edgeRight axis, so collision follows trimmed/mitred edges while the
  // ship remains projected on the curve's cross-section frame.
  function computePhysicalWallOffsets(frames, edges) {
    return frames.map((f, i) => {
      const er = f.edgeRight, p = f.pos;
      const off = e => (e.x - p.x) * er.x + (e.y - p.y) * er.y + (e.z - p.z) * er.z;
      return { sLeft: off(edges.left[i]), sRight: off(edges.right[i]) };
    });
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
  // Default local window (in segments): a self-intersection whose two crossing
  // segments are within this many segments of each other is collapsed; farther
  // ones are left alone. See the figure-8 rationale on findSelfIntersections.
  // Per-crossing overrides (see makeSelfIntersectionDeciders) can flip either
  // way; this is only the fallback when no override matches.
  const DEFAULT_SELF_INTERSECTION_SPAN = 100;

  // For crossing segments i < j on a polyline of `segCount` segments: the span
  // (how far apart, in segments) and whether the SHORTER arc between them wraps
  // around the end. On a circular polyline the two segments split the ring into
  // two arcs; the shorter is the accidental loop we collapse (collapsing the
  // longer would delete the bulk of the track).
  function crossingSpan(i, j, segCount, circular) {
    const forwardSpan = j - i;
    const wrappedSpan = circular ? segCount - forwardSpan : Infinity;
    return { span: Math.min(forwardSpan, wrappedSpan), useWrappedInterval: wrappedSpan < forwardSpan };
  }

  // Find ALL self-intersections of a polyline in XZ -- a full pairwise scan
  // with NO span bound (build-time only, not per-frame). Returns
  // [{ i, j, span, useWrappedInterval, point }] on the RAW polyline, so callers
  // can present/decide crossings individually (the editor draws a marker per
  // crossing; the user forces keep/collapse on specific ones). XZ-plane only,
  // matching trimEdge's convention -- a genuine elevated crossover (same XZ
  // footprint, different height, e.g. a bridge) would false-positive.
  //
  // The default collapse rule is deliberately LOCAL (see
  // DEFAULT_SELF_INTERSECTION_SPAN): a track that crosses itself far away in
  // parameter space (a genuine figure-8, where two lobes cross at one point but
  // are each a legitimate separate stretch) must keep both lobes intact --
  // collapsing "everything between" two crossings 100+ segments apart would
  // silently delete an entire lobe instead of the small overlapping area.
  function findSelfIntersections(pts, closed, wrapOpen) {
    closed = closed !== false;
    const circular = closed || !!wrapOpen;
    const N = pts.length;
    const segCount = circular ? N : N - 1;
    const out = [];
    if (segCount < 4) return out;
    const nextIdx = i => circular ? (i + 1) % N : i + 1;
    for (let i = 0; i < segCount; i++) {
      const a1 = pts[i], a2 = pts[nextIdx(i)];
      for (let j = i + 2; j < segCount; j++) {
        if (circular && i === 0 && j === segCount - 1) continue;
        const b1 = pts[j], b2 = pts[nextIdx(j)];
        if (!segmentsCrossXZ(a1, a2, b1, b2)) continue;
        const { span, useWrappedInterval } = crossingSpan(i, j, segCount, circular);
        out.push({ i, j, span, useWrappedInterval, point: segCrossPointXZ(a1, a2, b1, b2) });
      }
    }
    return out;
  }

  // Collapse self-intersections of a single polyline (a track edge / wall line)
  // to the crossing point, so each accidental loop renders as zero-area instead
  // of visibly intersecting geometry. Iterates (MAX_PASSES) so multiple
  // crossings resolve one at a time, and guards runaway crossing points
  // (near-parallel/degenerate segments) back to the loop midpoint. `wrapOpen`
  // treats an open polyline as circular (segment N-1 -> 0 exists), used when an
  // open path's two ends actually meet -- e.g. an 'opened-closed' disjoint
  // seam, where a fold straddling the seam must still be caught.
  //
  //   decide(crossing) -> collapse?   Per-crossing policy. Default: collapse
  //     iff crossing.span <= DEFAULT_SELF_INTERSECTION_SPAN (the local window).
  //     A per-crossing override predicate (see makeSelfIntersectionDeciders)
  //     can force a nearby crossing to be KEPT or a far one COLLAPSED.
  //   scanSpan   Perf bound: pairs farther apart than this are never even
  //     tested for crossing. Defaults to the local window (so the common,
  //     no-override case stays O(N * window)). Raise (or Infinity) only when an
  //     override may force-collapse a far crossing, so it can be found.
  function removeLocalSelfIntersectionLoops(pts, closed, wrapOpen, decide, scanSpan) {
    closed = closed !== false;
    const circular = closed || !!wrapOpen;
    const N = pts.length;
    const segCount = circular ? N : N - 1;
    if (segCount < 4) return pts.map(p => ({ x: p.x, y: p.y, z: p.z }));
    const nextIdx = i => circular ? (i + 1) % N : i + 1;
    const out = pts.map(p => ({ x: p.x, y: p.y, z: p.z }));
    const maxScan = (scanSpan == null) ? DEFAULT_SELF_INTERSECTION_SPAN : scanSpan;
    const shouldCollapse = decide || (c => c.span <= DEFAULT_SELF_INTERSECTION_SPAN);
    const MAX_PASSES = segCount;
    for (let pass = 0; pass < MAX_PASSES; pass++) {
      let found = null;
      for (let i = 0; i < segCount && !found; i++) {
        const a1 = out[i], a2 = out[nextIdx(i)];
        for (let j = i + 2; j < segCount; j++) {
          if (circular && i === 0 && j === segCount - 1) continue;
          const { span, useWrappedInterval } = crossingSpan(i, j, segCount, circular);
          if (span > maxScan) continue;
          const b1 = out[j], b2 = out[nextIdx(j)];
          if (!segmentsCrossXZ(a1, a2, b1, b2)) continue;
          const crossing = { i, j, span, useWrappedInterval, point: segCrossPointXZ(a1, a2, b1, b2) };
          if (!shouldCollapse(crossing)) continue; // force-kept: leave the loop
          found = crossing;
          break;
        }
      }
      if (!found) break;
      const { i, j, useWrappedInterval } = found;
      const a1 = out[i], a2 = out[nextIdx(i)], b1 = out[j], b2 = out[nextIdx(j)];
      const mid = {
        x: (a2.x + b1.x) / 2,
        y: (a2.y + b1.y) / 2,
        z: (a2.z + b1.z) / 2
      };
      let X = segCrossPointXZ(a1, a2, b1, b2);
      const localScale = Math.max(
        Math.hypot(a2.x - a1.x, a2.z - a1.z),
        Math.hypot(b2.x - b1.x, b2.z - b1.z),
        1
      );
      if (Math.hypot(X.x - mid.x, X.z - mid.z) > 8 * localScale) X = mid;
      let v = useWrappedInterval ? nextIdx(j) : nextIdx(i);
      const stop = useWrappedInterval ? i : j;
      while (true) {
        out[v] = { x: X.x, y: X.y, z: X.z };
        if (v === stop) break;
        v = nextIdx(v);
      }
    }
    return out;
  }

  function removeLocalEdgeSelfIntersections(edges, closed, wrapOpen, decideLeft, decideRight, scanSpan) {
    return {
      left: removeLocalSelfIntersectionLoops(edges.left, closed, wrapOpen, decideLeft, scanSpan),
      right: removeLocalSelfIntersectionLoops(edges.right, closed, wrapOpen, decideRight, scanSpan)
    };
  }

  // --- per-crossing keep/collapse overrides ----------------------------------
  // A self-intersection is identified STABLY (across edits, resampling, and
  // different N between editor and game) by the pair of control-point ids
  // nearest its two branches, plus which edge side it is on. Segment indices
  // are NOT stored -- they shift on any edit.

  // Id of the control point nearest frame `i` (of N). Resolution-independent:
  // depends only on the control-point parameter g the frame sits at, so the
  // editor (one N) and the game (another N) resolve the same crossing to the
  // same id.
  function controlIdAtFrame(controlPoints, closed, N, i) {
    const CP_N = controlPoints.length;
    if (!CP_N) return null;
    const g = closed ? (i / N) * CP_N : (N > 1 ? (i / (N - 1)) * (CP_N - 1) : 0);
    let idx = Math.round(g);
    idx = closed ? (((idx % CP_N) + CP_N) % CP_N) : Math.max(0, Math.min(CP_N - 1, idx));
    const cp = controlPoints[idx];
    return (cp && cp.id) || null;
  }

  // Sorted [a, b] control-point-id key for a crossing's two branches.
  function crossingKey(controlPoints, closed, N, crossing) {
    let a = controlIdAtFrame(controlPoints, closed, N, crossing.i);
    let b = controlIdAtFrame(controlPoints, closed, N, crossing.j);
    if (a != null && b != null && a > b) { const t = a; a = b; b = t; }
    return [a, b];
  }

  function crossingMatchesOverride(key, o) {
    return (o.a === key[0] && o.b === key[1]) || (o.a === key[1] && o.b === key[0]);
  }

  // Build per-side decide predicates + a scanSpan for ONE path from the whole
  // track's overrides. Overrides are self-scoping: since control-point ids are
  // globally unique, only entries whose BOTH ids belong to this path can match,
  // so we keep just those. Returns null when none apply (callers then use the
  // default local-window rule with no extra work). `allOverrides` entries are
  // { side, a, b, action }.
  function makeSelfIntersectionDeciders(controlPoints, closed, N, allOverrides) {
    const ids = new Set((controlPoints || []).map(c => c && c.id).filter(Boolean));
    const list = (allOverrides || []).filter(o => ids.has(o.a) && ids.has(o.b));
    if (!list.length) return null;
    const mk = side => {
      const relevant = list.filter(o => o.side === side);
      if (!relevant.length) return undefined; // default rule for this side
      return crossing => {
        const key = crossingKey(controlPoints, closed, N, crossing);
        const o = relevant.find(r => crossingMatchesOverride(key, r));
        if (o) return o.action === 'collapse';
        return crossing.span <= DEFAULT_SELF_INTERSECTION_SPAN;
      };
    };
    // If any override forces a collapse, far pairs must be tested too.
    const scanSpan = list.some(o => o.action === 'collapse') ? Infinity : undefined;
    return { decideLeft: mk('left'), decideRight: mk('right'), scanSpan };
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
    return { type: 'width', t: Math.max(0, Math.min(1, num(wp && wp.t, 0))), width: Math.max(1, num(wp && wp.width, DEFAULT_WIDTH)) };
  }
  function normalizeCrossSectionPoint(xp) {
    const num = (v, d) => (typeof v === 'number' && isFinite(v) ? v : d);
    return {
      type: 'crossSection',
      t: Math.max(0, Math.min(1, num(xp && xp.t, 0))),
      curvature: clampSignedUnit(num(xp && xp.curvature, 0)),
      tightness: clampTightness(num(xp && xp.tightness, DEFAULT_CROSS_SECTION_TIGHTNESS)),
      thickness: clampThickness(num(xp && xp.thickness, DEFAULT_CROSS_SECTION_THICKNESS))
    };
  }

  // The cross-section defaults injected when a path authors none of its own.
  // One helper so the three sites that need them cannot drift apart.
  function defaultCrossSectionPoints(closed, curvature) {
    const make = t => ({
      type: 'crossSection', t,
      curvature: clampSignedUnit(curvature),
      tightness: DEFAULT_CROSS_SECTION_TIGHTNESS,
      thickness: DEFAULT_CROSS_SECTION_THICKNESS
    });
    return [make(0), make(closed ? 0.5 : 1)];
  }

  // Legacy per-point `roll`/`width` migration: evenly spaces one point per
  // control point across the path's parameter domain.
  function defaultRollPoints(rawPoints, closed) {
    const n = rawPoints.length;
    if (n === 0) return [{ type: 'roll', t: 0, roll: 0 }, { type: 'roll', t: closed ? 0.5 : 1, roll: 0 }];
    const denom = closed ? n : Math.max(1, n - 1);
    return rawPoints.map((p, i) => ({
      type: 'roll',
      t: closed ? i / n : i / denom,
      roll: (p && typeof p.roll === 'number') ? Math.max(-180, Math.min(180, p.roll)) : 0
    }));
  }
  function defaultWidthPoints(rawPoints, closed) {
    const n = rawPoints.length;
    if (n === 0) return [{ type: 'width', t: 0, width: DEFAULT_WIDTH }, { type: 'width', t: closed ? 0.5 : 1, width: DEFAULT_WIDTH }];
    const denom = closed ? n : Math.max(1, n - 1);
    return rawPoints.map((p, i) => ({
      type: 'width',
      t: closed ? i / n : i / denom,
      width: (p && typeof p.width === 'number') ? Math.max(1, p.width) : DEFAULT_WIDTH
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
        if (p && p.type === 'crossSection') return normalizeCrossSectionPoint(p);
        return normalizePoint(p, i);
      });
      const posCount = points.filter(p => p.type === 'position').length;
      if (posCount < 4) throw new Error('path ' + i + ': a track path needs at least 4 position control points');
      const endT = closed ? 0.5 : 1;
      if (!points.some(p => p.type === 'roll')) points.push({ type: 'roll', t: 0, roll: 0 }, { type: 'roll', t: endT, roll: 0 });
      if (!points.some(p => p.type === 'width')) points.push({ type: 'width', t: 0, width: DEFAULT_WIDTH }, { type: 'width', t: endT, width: DEFAULT_WIDTH });
      if (!points.some(p => p.type === 'crossSection')) points.push(...defaultCrossSectionPoints(closed, rawPath.crossSectionCurvature));
      return { id: rawPath.id || null, closed, points, texture: normalizePathTexture(rawPath.texture) };
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
    const rawCross = rawPath && Array.isArray(rawPath.crossSectionPoints) ? rawPath.crossSectionPoints : null;
    const crossSectionPoints = (rawCross && rawCross.length >= 1)
      ? rawCross.map(normalizeCrossSectionPoint)
      : defaultCrossSectionPoints(closed, rawPath && rawPath.crossSectionCurvature);
    return {
      id: rawPath && rawPath.id || null,
      closed,
      points: rawPoints.map(normalizePoint).concat(rollPoints, widthPoints, crossSectionPoints),
      texture: normalizePathTexture(rawPath && rawPath.texture)
    };
  }

  function normalizePathTexture(raw) {
    if (!raw || typeof raw !== 'object' || typeof raw.asset !== 'string' || !raw.asset) return null;
    return { asset: raw.asset, tile: Number.isInteger(raw.tile) && raw.tile >= 0 ? raw.tile : 0 };
  }

  // Per-crossing keep/collapse override: { side:'left'|'right', a, b, action }.
  // a/b are control-point ids (the crossing's two branches, order-insensitive);
  // action 'keep' preserves the loop, 'collapse' removes it, overriding the
  // default local-window rule. Anything malformed is dropped.
  function normalizeSelfIntersectionOverride(o) {
    if (!o || typeof o.a !== 'string' || typeof o.b !== 'string' || !o.a || !o.b) return null;
    return {
      side: o.side === 'right' ? 'right' : 'left',
      a: o.a,
      b: o.b,
      action: o.action === 'collapse' ? 'collapse' : 'keep'
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

  // --- mesh regions -----------------------------------------------------------
  // Flat drivable areas imported from the geometry-js editor. track-core.js
  // stays dependency-free, so it only validates and carries this data; all the
  // geometry lives in js/track-mesh.js, which owns the geometry-js dependency.
  //
  // An asset record wraps the pristine geometry-js mesh JSON alongside settings
  // that are not part of that format (geometry-js's toJSON() does not serialize
  // a mesh's root attributes, so per-asset settings need their own home). A
  // bare mesh JSON is also accepted and treated as an asset with defaults.
  function normalizeMeshAssets(raw) {
    const out = {};
    if (!raw || typeof raw !== 'object') return out;
    for (const [id, entry] of Object.entries(raw)) {
      if (!id || !entry || typeof entry !== 'object') continue;
      const mesh = entry.mesh && typeof entry.mesh === 'object' ? entry.mesh : entry;
      if (!Array.isArray(mesh.polygons) || !Array.isArray(mesh.vertices)) continue;
      out[id] = {
        name: typeof entry.name === 'string' ? entry.name : id,
        railHeight: finiteOr(entry.railHeight, DEFAULT_RAIL_HEIGHT, 0),
        mesh: {
          vertices: mesh.vertices,
          edges: Array.isArray(mesh.edges) ? mesh.edges : [],
          polygons: mesh.polygons
        }
      };
    }
    return out;
  }

  function normalizeTextureAssets(raw) {
    const out = {};
    if (!raw || typeof raw !== 'object') return out;
    for (const [id, entry] of Object.entries(raw)) {
      if (!id || !entry || typeof entry !== 'object' || typeof entry.dataUrl !== 'string' || !entry.dataUrl) continue;
      const width = Math.max(1, Math.floor(finiteOr(entry.width, 1, 1)));
      const height = Math.max(1, Math.floor(finiteOr(entry.height, 1, 1)));
      out[id] = {
        name: typeof entry.name === 'string' && entry.name ? entry.name : id,
        dataUrl: entry.dataUrl,
        width,
        height,
        tileWidth: Math.max(1, Math.min(width, Math.floor(finiteOr(entry.tileWidth, width, 1)))),
        tileHeight: Math.max(1, Math.min(height, Math.floor(finiteOr(entry.tileHeight, height, 1))))
      };
    }
    return out;
  }

  function textureTileCount(asset) {
    if (!asset) return 0;
    return Math.floor(asset.width / asset.tileWidth) * Math.floor(asset.height / asset.tileHeight);
  }

  function pruneInvalidPathTextures(paths, textureAssets) {
    for (const path of paths) {
      if (!path.texture) continue;
      const asset = textureAssets[path.texture.asset];
      if (!asset || path.texture.tile >= textureTileCount(asset)) path.texture = null;
    }
  }

  function normalizeMeshPlacement(raw, i) {
    if (!raw || typeof raw !== 'object' || typeof raw.asset !== 'string' || !raw.asset) return null;
    return {
      id: typeof raw.id === 'string' && raw.id ? raw.id : 'm' + (i + 1),
      asset: raw.asset,
      x: finiteOr(raw.x, 0),
      z: finiteOr(raw.z, 0),
      rotation: finiteOr(raw.rotation, 0),
      elevation: finiteOr(raw.elevation, 0)
    };
  }

  // Assets nothing refers to are dropped on the way out, so deleting the last
  // placement of a shape eventually removes its geometry from the file too.
  function referencedMeshAssets(track) {
    const assets = track.meshAssets || {};
    const used = new Set((track.meshes || []).map(m => m.asset));
    const out = {};
    for (const id of Object.keys(assets)) if (used.has(id)) out[id] = assets[id];
    return out;
  }

  /* Multiply every LENGTH in a raw (just-parsed) track by `k`, in place.
   *
   * Only lengths scale. Angles (roll), curve parameters (t), NURBS weights,
   * cross-section curvature (dimensionless) and tightness (an exponent) are
   * scale-invariant -- scaling any of them would change a track's shape rather
   * than its size. Cross-section THICKNESS is not in that company: it is an
   * extrusion distance in world units, so it scales like width does.
   *
   * This runs on the RAW data, before normalization, on purpose. Normalization
   * injects defaults (widths, rail heights) that are already expressed in
   * current units; scaling afterwards would double those too and silently widen
   * every old track that never authored an explicit width.
   *
   * Every schema variant stores a control point as { pos: [x, y, z] }, so one
   * point-shaped scaler covers the modern `points` array and all the legacy
   * controlPoints/widthPoints/bare-array forms.
   */
  function scaleRawTrackData(data, k) {
    if (!data || k === 1) return data;
    const scalePoint = p => {
      if (!p || typeof p !== 'object') return;
      if (Array.isArray(p.pos) && p.pos.length === 3 && p.pos.every(n => typeof n === 'number')) {
        p.pos = [p.pos[0] * k, p.pos[1] * k, p.pos[2] * k];
      }
      if (typeof p.width === 'number') p.width *= k;
      if (typeof p.thickness === 'number') p.thickness *= k;
    };
    const scaleList = list => { if (Array.isArray(list)) list.forEach(scalePoint); };

    if (Array.isArray(data)) { scaleList(data); return data; }   // bare legacy array of points
    scaleList(data.controlPoints);
    scaleList(data.widthPoints);
    for (const rawPath of (Array.isArray(data.paths) ? data.paths : [])) {
      if (Array.isArray(rawPath)) { scaleList(rawPath); continue; }
      if (!rawPath || typeof rawPath !== 'object') continue;
      scaleList(rawPath.points);
      scaleList(rawPath.controlPoints);
      scaleList(rawPath.widthPoints);
      scaleList(rawPath.crossSectionPoints);   // pre-refactor three-array schema
    }
    for (const entry of Object.values(data.meshAssets || {})) {
      if (!entry || typeof entry !== 'object') continue;
      if (typeof entry.railHeight === 'number') entry.railHeight *= k;
      const mesh = entry.mesh && typeof entry.mesh === 'object' ? entry.mesh : entry;
      for (const v of (Array.isArray(mesh.vertices) ? mesh.vertices : [])) {
        if (!v || !v.position) continue;
        v.position = { x: (v.position.x || 0) * k, y: (v.position.y || 0) * k };
      }
    }
    for (const m of (Array.isArray(data.meshes) ? data.meshes : [])) {
      if (!m || typeof m !== 'object') continue;
      if (typeof m.x === 'number') m.x *= k;
      if (typeof m.z === 'number') m.z *= k;
      if (typeof m.elevation === 'number') m.elevation *= k;
      // rotation is an angle -- unchanged.
    }
    return data;
  }

  // --- zones -----------------------------------------------------------------
  // Flat rectangular areas that float just above a surface and fire an effect
  // when the ship drives onto them (schema 7). A zone is hosted EITHER on a
  // spline path -- positioned by t (0..1 along the path) and a signed lateral
  // offset in units, oriented to follow the track -- OR on a mesh region --
  // positioned by world x/z with its own yaw, sitting at that region's
  // elevation. `width` is the across-track extent, `length` the along-track
  // extent, both world units. effect 'velocityChange' boosts the ship (factor x
  // maxSpeed for `duration` s, then a smooth release); 'startGrid' is a purely
  // visual marker for now. Zones only ever exist in schema >= 7 files, so they
  // are never touched by the pre-schema-5 unit migration -- every length here is
  // already in current units.
  const DEFAULT_ZONE_WIDTH = 24;
  const DEFAULT_ZONE_LENGTH = 40;
  const DEFAULT_BOOST_FACTOR = 1.5;
  const DEFAULT_BOOST_DURATION = 2;

  function normalizeZone(raw, i, pathIds, meshIds) {
    if (!raw || typeof raw !== 'object') return null;
    const host = raw.host && typeof raw.host === 'object' ? raw.host : null;
    if (!host) return null;
    const effect = raw.effect === 'startGrid' ? 'startGrid' : 'velocityChange';
    const width = Math.max(0.5, finiteOr(raw.width, DEFAULT_ZONE_WIDTH, 0.5));
    const length = Math.max(0.5, finiteOr(raw.length, DEFAULT_ZONE_LENGTH, 0.5));
    let normHost;
    if (host.kind === 'mesh') {
      if (typeof host.meshId !== 'string' || !meshIds.has(host.meshId)) return null;
      normHost = { kind: 'mesh', meshId: host.meshId, x: finiteOr(host.x, 0), z: finiteOr(host.z, 0), rotation: finiteOr(host.rotation, 0) };
    } else {
      if (typeof host.pathId !== 'string' || !pathIds.has(host.pathId)) return null;
      normHost = { kind: 'path', pathId: host.pathId, t: clampRange(host.t, 0, 1, 0.5), lateral: finiteOr(host.lateral, 0) };
    }
    const zone = {
      id: (typeof raw.id === 'string' && raw.id) ? raw.id : 'z' + (i + 1),
      effect, width, length, host: normHost
    };
    if (effect === 'velocityChange') {
      zone.factor = clampRange(raw.factor, 0.1, 5, DEFAULT_BOOST_FACTOR);
      zone.duration = clampRange(raw.duration, 0.1, 30, DEFAULT_BOOST_DURATION);
    }
    return zone;
  }

  // Validate zones against the track's paths and mesh placements, dropping any
  // whose host id no longer exists (same policy as a dangling mesh placement).
  function normalizeZones(rawZones, paths, meshes) {
    const pathIds = new Set((paths || []).map(p => p && p.id).filter(Boolean));
    const meshIds = new Set((meshes || []).map(m => m && m.id).filter(Boolean));
    const out = [];
    (Array.isArray(rawZones) ? rawZones : []).forEach((z, i) => {
      const n = normalizeZone(z, i, pathIds, meshIds);
      if (n) out.push(n);
    });
    return out;
  }

  // Build a surface-conforming strip (two edge polylines, world space) for a
  // PATH-hosted zone, plus the evaluator g-window [gLo, gHi] the game reuses for
  // trigger detection. The strip follows the road's curve and banking over the
  // zone's LENGTH (measured as true 3D arc length outward from the center t),
  // offset laterally by the zone's `lateral`, and hovers `hover` units above the
  // surface along the frame normal. Shared by the game mesh and the editor
  // preview so the two can never drift.
  function zonePathStrip(controlPoints, closed, rollPoints, widthPoints, crossSectionPoints, zone, hover) {
    closed = closed !== false;
    const { evalTrack, CP_N } = makeEvaluator(controlPoints, closed, rollPoints, widthPoints, crossSectionPoints);
    const gMax = (closed ? CP_N : CP_N - 1) || 1;
    const host = zone.host || {};
    const gCenter = clampRange(host.t, 0, 1, 0.5) * gMax;
    const halfLen = Math.max(0.25, (zone.length || 0) / 2);
    const lateral = finiteOr(host.lateral, 0);
    const halfW = Math.max(0.25, (zone.width || 0) / 2);
    const hv = hover || 0;
    const clampG = g => closed ? g : Math.max(0, Math.min(gMax, g));
    // Step fine enough that a chord approximates the arc well AND that the
    // endpoint interpolation below is accurate even for a short zone on a big
    // loop (where one whole g-step can be tens of units of arc).
    const step = gMax / Math.max(600, CP_N * 60);
    // Walk outward from the center accumulating true 3D arc length until half
    // the zone length is covered, interpolating the final partial step so the
    // returned g lands exactly at halfLen (not a whole step past it).
    const walk = dir => {
      let g = gCenter, prev = evalTrack(clampG(g)).pos, acc = 0;
      for (let i = 0; i < 40000; i++) {
        const gNext = g + dir * step;
        const p = evalTrack(clampG(gNext)).pos;
        const d = Math.hypot(p.x - prev.x, p.y - prev.y, p.z - prev.z);
        if (acc + d >= halfLen) {
          const frac = d > 1e-9 ? (halfLen - acc) / d : 0;
          return g + dir * step * frac;
        }
        acc += d; prev = p; g = gNext;
        if (!closed && (g <= 0 || g >= gMax)) return clampG(g);
      }
      return g;
    };
    const gLo = walk(-1), gHi = walk(1);
    // One strip cross-section roughly every 6 units of the zone's length, so it
    // conforms to a curve without over-tessellating a straight pad.
    const K = Math.max(2, Math.min(96, Math.round((zone.length || 0) / 6) || 2));
    const left = [], right = [];
    for (let i = 0; i <= K; i++) {
      const g = gLo + (gHi - gLo) * (i / K);
      const f = frameFromSample(evalTrack(clampG(g)));
      const mid = vaddScaled(vaddScaled(f.pos, f.edgeRight, lateral), f.normal, hv);
      left.push(vaddScaled(mid, f.edgeRight, -halfW));
      right.push(vaddScaled(mid, f.edgeRight, halfW));
    }
    return { left, right, gLo, gHi, gMax, closed };
  }

  // Is the ship's evaluator parameter gShip within a path zone's [gLo, gHi]
  // window? For a closed path the window may be expressed outside [0, CP_N)
  // (it can straddle the wrap), so gShip is shifted by whole cycles into the
  // window's neighbourhood before the range test.
  function zoneAlongContains(gShip, gLo, gHi, gMax, closed) {
    if (!closed) return gShip >= gLo - 1e-9 && gShip <= gHi + 1e-9;
    const center = (gLo + gHi) / 2;
    let g = gShip;
    while (g < center - gMax / 2) g += gMax;
    while (g > center + gMax / 2) g -= gMax;
    return g >= gLo - 1e-9 && g <= gHi + 1e-9;
  }

  // Accepts either the current { paths: [{closed, points}, ...] } schema, the
  // pre-refactor three-array schema, or the legacy single-closed-loop
  // { controlPoints: [...] } schema (see normalizePath).
  function parseTrack(text) {
    const data = JSON.parse(text);
    // One-time unit migration: anything written before schema 5 is in the old
    // half-size units. Converting up front means every consumer downstream sees
    // a single unit system, with no runtime scale factor anywhere.
    const sourceVersion = (!Array.isArray(data) && data && data.version) || 3;
    // Keyed to the version that changed units, NOT to the current version --
    // see UNIT_SCALE_SCHEMA_VERSION. A schema-5 file is already in current
    // units and must be left alone no matter how far the schema moves on.
    if (sourceVersion < UNIT_SCALE_SCHEMA_VERSION) scaleRawTrackData(data, LEGACY_UNIT_SCALE);
    let rawPaths;
    if (Array.isArray(data)) rawPaths = [{ closed: true, controlPoints: data }];
    else if (Array.isArray(data.paths)) rawPaths = data.paths;
    else if (Array.isArray(data.controlPoints)) rawPaths = [{ closed: true, controlPoints: data.controlPoints }];
    else throw new Error('no paths or controlPoints array found');
    if (rawPaths.length < 1) throw new Error('a track needs at least one path');
    const topLevelCrossSectionCurvature = clampSignedUnit(data && data.crossSectionCurvature);
    const paths = rawPaths.map((rawPath, i) => {
      const path = normalizePath(rawPath, i);
      const rawPoints = rawPath && !Array.isArray(rawPath) && Array.isArray(rawPath.points) ? rawPath.points : null;
      const hadCrossSectionPoints = rawPoints && rawPoints.some(p => p && p.type === 'crossSection');
      if (!hadCrossSectionPoints) {
        for (const p of path.points) if (p.type === 'crossSection') p.curvature = topLevelCrossSectionCurvature;
      }
      return path;
    });
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
    const textureAssets = normalizeTextureAssets(data && data.textureAssets);
    pruneInvalidPathTextures(paths, textureAssets);

    // Mesh regions are optional: tracks written before schema 4 simply have
    // none, and placements whose asset went missing are dropped rather than
    // left dangling for the game to trip over.
    const meshAssets = normalizeMeshAssets(data && data.meshAssets);
    const meshes = (Array.isArray(data && data.meshes) ? data.meshes : [])
      .map(normalizeMeshPlacement)
      .filter(m => m && Object.prototype.hasOwnProperty.call(meshAssets, m.asset));
    return {
      version: TRACK_SCHEMA_VERSION,
      name: (data && data.name) || 'Untitled Track',
      samples: (data && data.samples) || N_DEFAULT,
      paths,
      meshAssets,
      meshes,
      textureAssets,
      zones: normalizeZones(data && data.zones, paths, meshes),
      disjointSeams: Array.isArray(data && data.disjointSeams) ? data.disjointSeams : [],
      junctions: Array.isArray(data && data.junctions) ? data.junctions : [],
      selfIntersectionOverrides: (Array.isArray(data && data.selfIntersectionOverrides) ? data.selfIntersectionOverrides : [])
        .map(normalizeSelfIntersectionOverride).filter(Boolean),
      handling: normalizeHandling(data && data.handling),
      start: normalizeStart(data && data.start, paths)
    };
  }

  function serializeTrack(track) {
    // Compact one-line-per-point formatting for readability.
    const pathsJson = track.paths.map(path => {
      const lines = path.points.map(p => {
        if (p.type === 'roll') return '      { "type": "roll", "t": ' + p.t + ', "roll": ' + p.roll + ' }';
        if (p.type === 'width') return '      { "type": "width", "t": ' + p.t + ', "width": ' + p.width + ' }';
        if (p.type === 'crossSection') return '      { "type": "crossSection", "t": ' + p.t + ', "curvature": ' + p.curvature + ', "tightness": ' + clampTightness(p.tightness) + ', "thickness": ' + clampThickness(p.thickness) + ' }';
        return '      { "type": "position", "id": ' + JSON.stringify(p.id || '') + ', "pos": [' + p.pos.join(', ') + '], "weight": ' + p.weight + ' }';
      }).join(',\n');
      const textureJson = path.texture ? ', "texture": ' + JSON.stringify(path.texture) : '';
      return '    { "id": ' + JSON.stringify(path.id || '') + ', "closed": ' + (path.closed !== false) + textureJson + ', "points": [\n' + lines + '\n    ] }';
    }).join(',\n');
    const start = normalizeStart(track.start, track.paths);
    const assets = referencedMeshAssets(track);
    const meshesJson = (track.meshes || [])
      .map(m => '    { "id": ' + JSON.stringify(m.id) + ', "asset": ' + JSON.stringify(m.asset) +
        ', "x": ' + m.x + ', "z": ' + m.z + ', "rotation": ' + m.rotation + ', "elevation": ' + m.elevation + ' }')
      .join(',\n');
    const assetsJson = Object.keys(assets)
      .map(id => '    ' + JSON.stringify(id) + ': ' + JSON.stringify(assets[id]))
      .join(',\n');
    return '{\n' +
      '  "version": ' + TRACK_SCHEMA_VERSION + ',\n' +
      '  "name": ' + JSON.stringify(track.name || 'Untitled Track') + ',\n' +
      '  "start": { "path": ' + start.path + ', "point": ' + start.point + ', "reverse": ' + start.reverse + ' },\n' +
      '  "handling": ' + JSON.stringify(normalizeHandling(track.handling)) + ',\n' +
      '  "zones": ' + JSON.stringify(track.zones || []) + ',\n' +
      '  "disjointSeams": ' + JSON.stringify(track.disjointSeams || []) + ',\n' +
      '  "junctions": ' + JSON.stringify(track.junctions || []) + ',\n' +
      '  "selfIntersectionOverrides": ' + JSON.stringify(track.selfIntersectionOverrides || []) + ',\n' +
      '  "meshAssets": {' + (assetsJson ? '\n' + assetsJson + '\n  ' : '') + '},\n' +
      '  "meshes": [' + (meshesJson ? '\n' + meshesJson + '\n  ' : '') + '],\n' +
      '  "textureAssets": ' + JSON.stringify(track.textureAssets || {}, null, 2).replace(/\n/g, '\n  ') + ',\n' +
      '  "paths": [\n' + pathsJson + '\n  ]\n}\n';
  }

  // --- built-in tracks --------------------------------------------------------
  const DEFAULT_TRACK = {
    version: TRACK_SCHEMA_VERSION,
    name: 'Default Circuit',
    start: { path: 0, point: 0, reverse: false },
    disjointSeams: [],
    junctions: [],
    meshAssets: {},
    meshes: [],
    zones: [],
    textureAssets: {},
    // The classic varied banked circuit, scaled uniformly x9 from its original
    // ~888 m into the current 7-10 km regime (~7995 m driven). Only lengths
    // scale -- positions, widths and cross-section thickness -- so the shape,
    // banking and relative proportions are byte-for-byte the same track, just
    // big. Angles (roll) and curve t are dimensionless and untouched.
    paths: [{
      closed: true,
      points: [
        { type: 'position', pos: [1620, 0, 0], weight: 1 },
        { type: 'position', pos: [1260, 72, 828], weight: 1 },
        { type: 'position', pos: [324, 144, 1080], weight: 1 },
        { type: 'position', pos: [-612, 90, 972], weight: 1 },
        { type: 'position', pos: [-1332, 0, 540], weight: 1 },
        { type: 'position', pos: [-1656, -72, -108], weight: 1 },
        { type: 'position', pos: [-1188, -54, -720], weight: 1 },
        { type: 'position', pos: [-288, 18, -1008], weight: 1 },
        { type: 'position', pos: [720, 54, -864], weight: 1 },
        { type: 'position', pos: [1440, 18, -396], weight: 1 },
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
        { type: 'width', t: 0.0, width: 396 },
        { type: 'width', t: 0.1, width: 324 },
        { type: 'width', t: 0.2, width: 252 },
        { type: 'width', t: 0.3, width: 234 },
        { type: 'width', t: 0.4, width: 288 },
        { type: 'width', t: 0.5, width: 216 },
        { type: 'width', t: 0.6, width: 216 },
        { type: 'width', t: 0.7, width: 360 },
        { type: 'width', t: 0.8, width: 432 },
        { type: 'width', t: 0.9, width: 396 },
        { type: 'crossSection', t: 0, curvature: DEFAULT_CROSS_SECTION_CURVATURE, tightness: DEFAULT_CROSS_SECTION_TIGHTNESS, thickness: 36 },
        { type: 'crossSection', t: 0.5, curvature: DEFAULT_CROSS_SECTION_CURVATURE, tightness: DEFAULT_CROSS_SECTION_TIGHTNESS, thickness: 36 }
      ]
    }]
  };

  const STARTER_TRACK = {
    version: TRACK_SCHEMA_VERSION,
    name: 'New Track',
    start: { path: 0, point: 0, reverse: false },
    disjointSeams: [],
    junctions: [],
    meshAssets: {},
    meshes: [],
    zones: [],
    textureAssets: {},
    // A flat circle whose DRIVEN centerline length is 8,000 m. The rational
    // cubic B-spline does not pass through its control points, so the 12 points
    // sit on a radius (~1332.9) calibrated by baking and measuring, NOT on the
    // 1273 m geometric radius of an 8,000 m circle -- placing them there would
    // yield a curve ~5% short. Flat (roll 0, y 0), constant width 36 (= the
    // default), no cross-section curvature.
    paths: [{
      closed: true,
      points: [
        { type: 'position', pos: [1332.907, 0, 0], weight: 1 },
        { type: 'position', pos: [1154.331, 0, 666.453], weight: 1 },
        { type: 'position', pos: [666.453, 0, 1154.331], weight: 1 },
        { type: 'position', pos: [0, 0, 1332.907], weight: 1 },
        { type: 'position', pos: [-666.453, 0, 1154.331], weight: 1 },
        { type: 'position', pos: [-1154.331, 0, 666.453], weight: 1 },
        { type: 'position', pos: [-1332.907, 0, 0], weight: 1 },
        { type: 'position', pos: [-1154.331, 0, -666.453], weight: 1 },
        { type: 'position', pos: [-666.453, 0, -1154.331], weight: 1 },
        { type: 'position', pos: [0, 0, -1332.907], weight: 1 },
        { type: 'position', pos: [666.453, 0, -1154.331], weight: 1 },
        { type: 'position', pos: [1154.331, 0, -666.453], weight: 1 },
        { type: 'roll', t: 0, roll: 0 },
        { type: 'roll', t: 0.5, roll: 0 },
        { type: 'width', t: 0, width: 36 },
        { type: 'width', t: 0.5, width: 36 },
        { type: 'crossSection', t: 0, curvature: DEFAULT_CROSS_SECTION_CURVATURE, tightness: DEFAULT_CROSS_SECTION_TIGHTNESS, thickness: DEFAULT_CROSS_SECTION_THICKNESS },
        { type: 'crossSection', t: 0.5, curvature: DEFAULT_CROSS_SECTION_CURVATURE, tightness: DEFAULT_CROSS_SECTION_TIGHTNESS, thickness: DEFAULT_CROSS_SECTION_THICKNESS }
      ]
    }]
  };

  global.TrackCore = {
    basis, basisDeriv, splitPoints, makeEvaluator, buildCenterline, buildEdges, buildFlatEdges,
    computePhysicalWallOffsets, computeDisjointEdgeCuts, removeLocalSelfIntersectionLoops, removeLocalEdgeSelfIntersections,
    findSelfIntersections, controlIdAtFrame, crossingKey, makeSelfIntersectionDeciders,
    DEFAULT_SELF_INTERSECTION_SPAN,
    evalRoll: evalRollSpline, evalWidth: evalWidthSpline,
    evalCrossSectionCurvature: evalCrossSectionSpline, evalCrossSectionTightness: evalCrossSectionTightnessSpline,
    evalCrossSectionThickness: evalCrossSectionThicknessSpline, DEFAULT_CROSS_SECTION_THICKNESS,
    crossSectionHeight, crossSectionHeightDerivative, crossSectionBreakpoints, crossSectionStitchPoint,
    frameFromSample, longitudinalBreakpoints, buildAdaptiveMeshFrames, adaptiveSampleCount,
    LONGITUDINAL_SAGITTA_TOLERANCE, LONGITUDINAL_MAX_DISTANCE, LONGITUDINAL_MAX_DEPTH,
    normalizeZones, zonePathStrip, zoneAlongContains,
    DEFAULT_ZONE_WIDTH, DEFAULT_ZONE_LENGTH, DEFAULT_BOOST_FACTOR, DEFAULT_BOOST_DURATION,
    parseTrack, serializeTrack, normalizeStart,
    normalizeMeshAssets, normalizeMeshPlacement, referencedMeshAssets, normalizeTextureAssets,
    DEFAULT_TRACK, STARTER_TRACK, N_DEFAULT, COLLISION_WALL_MARGIN, DEFAULT_RAIL_HEIGHT, DEFAULT_WIDTH,
    DEFAULT_HANDLING, normalizeHandling,
    TRACK_SCHEMA_VERSION, UNIT_SCALE_SCHEMA_VERSION,
    // expose a deep-clone helper so callers never share point references
    cloneTrack: t => JSON.parse(JSON.stringify(t))
  };
})(typeof window !== 'undefined' ? window : globalThis);
