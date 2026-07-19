
// ---------- Scene setup ----------
const scene = new THREE.Scene();
scene.background = new THREE.Color(0x02040a);
scene.fog = new THREE.Fog(0x02040a, 60, 220);

const camera = new THREE.PerspectiveCamera(
  65, window.innerWidth / window.innerHeight, 0.1, 1000
);

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setSize(window.innerWidth, window.innerHeight);
renderer.setPixelRatio(window.devicePixelRatio);
document.body.appendChild(renderer.domElement);

window.addEventListener('resize', () => {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
});

// ---------- Lighting ----------
scene.add(new THREE.HemisphereLight(0x88bbff, 0x080810, 0.45));
const sun = new THREE.DirectionalLight(0xffffff, 0.75);
sun.position.set(50, 80, 30);
scene.add(sun);

// ---------- Track: built from control points via shared TrackCore ------------
// The track is authored data: each path holds a single array of TYPED control
// points (type: 'position' | 'roll' | 'width', see track-core.js), split via
// TrackCore.splitPoints() into the three point sets buildPath() consumes.
// Position points interpolate with a rational cubic B-spline; roll/width each
// interpolate with their own independent spline. TrackCore holds the shared
// math so the editor's preview and this geometry can never drift apart.
// buildTrack() (re)generates everything from a path list, so importing new
// JSON at runtime just calls it again.
const N = TrackCore.N_DEFAULT;         // centerline samples per path
const CROSS_SECTION_SEGMENTS = 12;     // samples across the road width for curved cross-sections
const UP = new THREE.Vector3(0, 1, 0);
const toVec = o => new THREE.Vector3(o.x, o.y, o.z);
const clampSignedUnit = n => (typeof n === 'number' && isFinite(n) ? Math.max(-1, Math.min(1, n)) : 0);
const clampTightness = n => (typeof n === 'number' && isFinite(n) ? Math.max(0.2, Math.min(4, n)) : 1);
function crossSectionHeight(curvature, tightness, v, chordWidth) {
  const u = 2 * Math.max(0, Math.min(1, v)) - 1; // -1 left edge, 0 center, 1 right edge
  const base = Math.sqrt(Math.max(0, 1 - u * u));
  return clampSignedUnit(curvature) * (chordWidth / 2) * Math.pow(base, clampTightness(tightness));
}
function crossSectionHeightDerivative(curvature, tightness, v, chordWidth) {
  const c = clampSignedUnit(curvature), k = clampTightness(tightness);
  if (!c) return 0;
  const u = 2 * Math.max(0.001, Math.min(0.999, v)) - 1;
  const base = Math.sqrt(Math.max(0.000001, 1 - u * u));
  return c * (chordWidth / 2) * k * (-2 * u) * Math.pow(base, k - 2);
}

let paths = [];                        // compiled paths: { closed, centerline, mesh, stripeLine, railR, railL, anchors }
let connectedEndpointIds = new Set();  // shared/disjoint/branch endpoint point IDs that should not launch off-end
let trackName = '';
let trackStart = { path: 0, point: 0, reverse: false };

function disposeObject(obj) {
  if (!obj) return;
  scene.remove(obj);
  if (obj.geometry) obj.geometry.dispose();
  if (obj.material) obj.material.dispose();
}

// Compile a single path (closed loop or open curve) into renderable geometry
// and a physics-ready centerline.
function buildPath(controlPoints, closed, rollPoints, widthPoints, crossSectionPoints, prebuiltRaw, prebuiltEdges, endpointCuts, endpointNormals, deciders, skipSelfIntersectionCleanup) {
  // Bake the centerline via the shared core, then derive the two track edges
  // with self-intersections trimmed to sharp corners.
  const raw = prebuiltRaw || TrackCore.buildCenterline(controlPoints, N, closed, rollPoints, widthPoints, crossSectionPoints);
  let edges = prebuiltEdges || TrackCore.buildEdges(raw, closed);

  // At a DISJOINT seam (editor-authored hard corner) override the shared
  // endpoint's normal/edges so the two incident ribbons mitre into a clean
  // corner instead of just meeting/overlapping at the raw shared point.
  // Branch junctions (3+ incident ends, or 2-incident but not a disjoint
  // seam) are NOT touched here -- those still freely overlap.
  if (endpointNormals) {
    if (endpointNormals.start) raw[0].normal = endpointNormals.start;
    if (endpointNormals.end) raw[raw.length - 1].normal = endpointNormals.end;
  }
  if (endpointCuts) {
    const applyCut = (end, i) => {
      if (!endpointCuts[end]) return;
      if (endpointCuts[end].left) edges.left[i] = endpointCuts[end].left;
      if (endpointCuts[end].right) edges.right[i] = endpointCuts[end].right;
    };
    applyCut('start', 0);
    applyCut('end', raw.length - 1);
  }

  const wrapsAtDisjointSeam = !closed && !!endpointCuts && !!endpointCuts.start && !!endpointCuts.end &&
    controlPoints[0] && controlPoints[controlPoints.length - 1] && controlPoints[0].id === controlPoints[controlPoints.length - 1].id;
  // `deciders` (from track.selfIntersectionOverrides) lets specific crossings
  // be force-kept or force-collapsed; when absent the default local window rule
  // applies. Branch-connected curves intentionally skip this cleanup so branch
  // geometry is not altered by runtime self-intersection handling.
  if (!skipSelfIntersectionCleanup) {
    edges = TrackCore.removeLocalEdgeSelfIntersections(
      edges, closed, wrapsAtDisjointSeam,
      deciders && deciders.decideLeft, deciders && deciders.decideRight, deciders && deciders.scanSpan
    );
  }

  // Wrap frames into THREE vectors for the physics. Also record each sample's
  // signed lateral offset (along edgeRight) of the left/right edges, so the
  // collision corridor follows the actual (possibly mitred) road edge, not
  // just centerline +/- halfW.
  const wallOffsets = TrackCore.computePhysicalWallOffsets(raw, edges);
  const centerline = raw.map((f, i) => ({
    pos: toVec(f.pos), tangent: toVec(f.tangent), h: toVec(f.h),
    edgeRight: toVec(f.edgeRight), normal: toVec(f.normal),
    roll: f.roll, width: f.width, halfW: f.halfW,
    crossSectionCurvature: f.crossSectionCurvature, crossSectionTightness: f.crossSectionTightness,
    sLeft: wallOffsets[i].sLeft, sRight: wallOffsets[i].sRight
  }));

  // Ribbon surface between the two edges. Closed paths wrap the strip back to
  // the start; open paths leave the two ends unconnected. A global cross-
  // section curvature subdivides the strip across its width: 0 keeps the old
  // flat chord, 1 raises the center to a semicircular arc with the same edges.
  const roadMaterial = () => new THREE.MeshStandardMaterial({ color: 0x7fb4d4, emissive: 0x31566a, emissiveIntensity: 0.12, roughness: 0.75, metalness: 0.05, side: THREE.DoubleSide, flatShading: true });
  const pos = [], idx = [];
  const crossCount = CROSS_SECTION_SEGMENTS + 1;
  for (let i = 0; i < N; i++) {
    const left = edges.left[i], right = edges.right[i], f = raw[i];
    const chord = { x: right.x - left.x, y: right.y - left.y, z: right.z - left.z };
    const chordWidth = Math.hypot(chord.x, chord.y, chord.z) || 1;
    for (let j = 0; j < crossCount; j++) {
      const v = j / CROSS_SECTION_SEGMENTS;
      const h = crossSectionHeight(f.crossSectionCurvature, f.crossSectionTightness, v, chordWidth);
      pos.push(
        left.x + chord.x * v + f.normal.x * h,
        left.y + chord.y * v + f.normal.y * h,
        left.z + chord.z * v + f.normal.z * h
      );
    }
  }
  const segCount = closed ? N : N - 1;
  for (let i = 0; i < segCount; i++) {
    const ni = closed ? (i + 1) % N : i + 1;
    for (let j = 0; j < CROSS_SECTION_SEGMENTS; j++) {
      const a = i * crossCount + j, b = i * crossCount + j + 1;
      const c = ni * crossCount + j, d = ni * crossCount + j + 1;
      idx.push(a, b, c, b, d, c);
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setIndex(idx);
  // Duplicate indexed vertices and compute normals per triangle so lighting is
  // flat per rendered polygon, not smoothed/interpolated across the road mesh.
  const flatG = g.toNonIndexed();
  flatG.computeVertexNormals();
  g.dispose();
  const mesh = new THREE.Mesh(flatG, roadMaterial());
  scene.add(mesh);

  // Dashed racing stripe riding just above the banked centerline.
  const sg = new THREE.BufferGeometry().setFromPoints(
    centerline.map(c => c.pos.clone().addScaledVector(c.normal, crossSectionHeight(c.crossSectionCurvature, c.crossSectionTightness, 0.5, c.width) + 0.05))
  );
  const stripeLine = closed
    ? new THREE.LineLoop(sg, new THREE.LineDashedMaterial({ color: 0xffdd00, dashSize: 3, gapSize: 2 }))
    : new THREE.Line(sg, new THREE.LineDashedMaterial({ color: 0xffdd00, dashSize: 3, gapSize: 2 }));
  stripeLine.computeLineDistances();
  scene.add(stripeLine);

  // Walls remain physical via centerline.sLeft/sRight collision bounds above,
  // but are not rendered as guard-rail meshes.
  const railR = null;
  const railL = null;

  const anchors = controlPoints.map(c => new THREE.Vector3(c.pos[0], c.pos[1], c.pos[2]));
  const endpointIds = {
    start: controlPoints[0] && controlPoints[0].id,
    end: controlPoints[controlPoints.length - 1] && controlPoints[controlPoints.length - 1].id
  };
  return { closed, centerline, mesh, stripeLine, railR, railL, anchors, endpointIds };
}

// For each position-point ID that is a shared open endpoint of 2+ baked paths,
// the list of { pathIndex, end } incidences touching it there.
function sharedEndpointGroups(bakedPaths) {
  const groups = new Map();
  const add = (id, pathIndex, end) => {
    if (!id) return;
    if (!groups.has(id)) groups.set(id, []);
    groups.get(id).push({ pathIndex, end });
  };
  bakedPaths.forEach((bp, pathIndex) => {
    if (bp.closed || !bp.controlPoints.length) return;
    const first = bp.controlPoints[0], last = bp.controlPoints[bp.controlPoints.length - 1];
    if (first) add(first.id, pathIndex, 'start');
    if (last) add(last.id, pathIndex, 'end');
  });
  return groups;
}
function endpointIncidentCounts(bakedPaths) {
  const counts = new Map();
  for (const [id, list] of sharedEndpointGroups(bakedPaths)) counts.set(id, list.length);
  return counts;
}

function inferBranchPointIds(trackPaths, junctions) {
  const ids = new Set((junctions || []).map(j => j.pointId).filter(Boolean));
  const stats = new Map();
  const stat = id => {
    if (!stats.has(id)) stats.set(id, { endpoints: 0, interior: 0, closed: 0 });
    return stats.get(id);
  };
  for (const path of trackPaths || []) {
    const cps = TrackCore.splitPoints(path.points || []).controlPoints;
    const closed = path.closed !== false;
    for (let i = 0; i < cps.length; i++) {
      const p = cps[i];
      if (!p || !p.id) continue;
      const s = stat(p.id);
      if (closed) s.closed++;
      else if (i === 0 || i === cps.length - 1) s.endpoints++;
      else s.interior++;
    }
  }
  for (const [id, s] of stats) {
    if (s.endpoints >= 3) ids.add(id);
    else if (s.endpoints >= 1 && (s.closed > 0 || s.interior > 0)) ids.add(id);
  }
  return ids;
}

// At a disjoint seam the two incident path ends generally have different
// surface normals (the two curves approach the hard corner from different
// directions/banking). If each rail used its own normal, the wall bases
// would meet (via computeDisjointEdgeCuts) but the tops would still splay
// apart. Average the endpoint normals per seam and use that shared normal at
// BOTH incident ends so the rail tops meet too, not just the bases.
function computeDisjointEndpointNormals(bakedPaths, disjointSeams) {
  const out = bakedPaths.map(() => ({}));
  const norm = v => { const l = Math.hypot(v.x, v.y, v.z) || 1; return { x: v.x / l, y: v.y / l, z: v.z / l }; };
  for (const seam of disjointSeams || []) {
    const incs = [];
    bakedPaths.forEach((bp, pathIndex) => {
      if (bp.closed || !bp.controlPoints.length || !bp.frames.length) return;
      const lastCp = bp.controlPoints.length - 1;
      if (bp.controlPoints[0] && bp.controlPoints[0].id === seam.pointId) {
        incs.push({ pathIndex, end: 'start', normal: bp.frames[0].normal });
      }
      if (bp.controlPoints[lastCp] && bp.controlPoints[lastCp].id === seam.pointId) {
        incs.push({ pathIndex, end: 'end', normal: bp.frames[bp.frames.length - 1].normal });
      }
    });
    if (incs.length < 2) continue;
    const avg = norm(incs.reduce((s, inc) => ({
      x: s.x + inc.normal.x, y: s.y + inc.normal.y, z: s.z + inc.normal.z
    }), { x: 0, y: 0, z: 0 }));
    for (const inc of incs) out[inc.pathIndex][inc.end] = avg;
  }
  return out;
}
// (Re)build the entire track from a normalized track object, then reset the
// ship. `track.paths` is [{ closed, points }, ...] (points: typed
// position/roll/width control points, see track-core.js). `track.start` is
// { path, point, reverse } picking which position control point the ship
// begins at and which way it faces. Paths that share a control point simply
// meet there and overlap -- EXCEPT disjoint seams (editor-authored hard
// corners), whose edges are cut to a clean mitre instead.
function buildTrack(track) {
  // Preserve authored branch geometry exactly. Branch-connected paths are not
  // split/opened or otherwise normalized at runtime.
  const trackPaths = track.paths || [];
  const branchPointIds = inferBranchPointIds(trackPaths, track.junctions || []);
  trackName = track.name || '';
  trackStart = track.start || { path: 0, point: 0, reverse: false };
  connectedEndpointIds = new Set((track.disjointSeams || []).concat(track.junctions || []).map(j => j.pointId));

  // Drop any previously-built geometry before rebuilding.
  for (const p of paths) {
    disposeObject(p.mesh); disposeObject(p.stripeLine);
    disposeObject(p.railR); disposeObject(p.railL);
  }
  const bakedPaths = trackPaths.map(p => {
    const { controlPoints, rollPoints, widthPoints, crossSectionPoints } = TrackCore.splitPoints(p.points);
    const closed = p.closed !== false;
    const frames = TrackCore.buildCenterline(controlPoints, N, closed, rollPoints, widthPoints, crossSectionPoints);
    const edges = TrackCore.buildEdges(frames, closed);
    const hasBranchConnection = controlPoints.some(cp => cp && branchPointIds.has(cp.id));
    return { id: p.id, closed, controlPoints, rollPoints, widthPoints, crossSectionPoints, frames, edges, hasBranchConnection };
  });
  const incidentCounts = endpointIncidentCounts(bakedPaths);
  for (const [id, count] of incidentCounts) if (count >= 2) connectedEndpointIds.add(id);
  const disjointSeams = track.disjointSeams || [];
  const overrides = track.selfIntersectionOverrides || [];
  const edgeCuts = TrackCore.computeDisjointEdgeCuts(bakedPaths, disjointSeams);
  const endpointNormals = computeDisjointEndpointNormals(bakedPaths, disjointSeams);
  paths = bakedPaths.map((p, i) => buildPath(
    p.controlPoints, p.closed, p.rollPoints, p.widthPoints, p.crossSectionPoints, p.frames, p.edges, edgeCuts[i], endpointNormals[i],
    TrackCore.makeSelfIntersectionDeciders(p.controlPoints, p.closed, N, overrides), p.hasBranchConnection
  ));
  resetShip();
  const label = document.getElementById('trackName');
  if (label) label.textContent = trackName;
}

// Sample a smooth, interpolated track frame at a horizontal position. Searches
// ALL path segments for the nearest projected point, then interpolates within
// that segment. Segment-based selection avoids endpoint ambiguity at disjoint
// shared control points, where nearest-sample selection can briefly stick to
// the old path end and then jump to the new path.
const _sample = {
  pos: new THREE.Vector3(),
  tangent: new THREE.Vector3(),
  edgeRight: new THREE.Vector3(),
  normal: new THREE.Vector3(),
  halfW: 0, sLeft: 0, sRight: 0, crossSectionCurvature: 0, crossSectionTightness: 1,
  offEnd: false
};
function curvedSurfaceHeight(sample, s) {
  const lo = sample.sLeft, hi = sample.sRight;
  const span = hi - lo;
  if (Math.abs(span) < 1e-6) return 0;
  const v = (s - lo) / span;
  return crossSectionHeight(sample.crossSectionCurvature, sample.crossSectionTightness, v, Math.abs(span));
}
function curvedSurfaceFrame(sample, s) {
  const lo = sample.sLeft, hi = sample.sRight;
  const span = hi - lo;
  const v = Math.abs(span) < 1e-6 ? 0.5 : (s - lo) / span;
  const lift = crossSectionHeight(sample.crossSectionCurvature, sample.crossSectionTightness, v, Math.abs(span));
  const pos = sample.pos.clone()
    .addScaledVector(sample.edgeRight, s)
    .addScaledVector(sample.normal, lift);
  const dhdv = crossSectionHeightDerivative(sample.crossSectionCurvature, sample.crossSectionTightness, v, Math.abs(span));
  const crossT = sample.edgeRight.clone().multiplyScalar(span).addScaledVector(sample.normal, dhdv);
  const normal = new THREE.Vector3().crossVectors(sample.tangent, crossT).normalize();
  if (normal.dot(sample.normal) < 0) normal.negate();
  return { pos, normal };
}

function sampleTrack(x, z) {
  let fallback = { path: paths[0], a: 0, b: 1, t: 0, d: Infinity };
  let bestUnder = null;
  for (const path of paths) {
    const cl = path.centerline, M = cl.length;
    const segCount = path.closed ? M : M - 1;
    for (let i = 0; i < segCount; i++) {
      const j = path.closed ? (i + 1) % M : i + 1;
      const a = cl[i], b = cl[j];
      const sx = b.pos.x - a.pos.x, sz = b.pos.z - a.pos.z;
      const segLen2 = sx * sx + sz * sz;
      const t = segLen2 > 0
        ? THREE.MathUtils.clamp(((x - a.pos.x) * sx + (z - a.pos.z) * sz) / segLen2, 0, 1)
        : 0;
      const px = a.pos.x + sx * t, pz = a.pos.z + sz * t;
      const dx = x - px, dz = z - pz;
      const d = dx * dx + dz * dz;
      if (d < fallback.d) fallback = { path, a: i, b: j, t, d };

      // If multiple road ribbons overlap under the ship, use only the closest
      // one whose actual collision corridor contains this X/Z. Segments that
      // are nearby but outside their sLeft/sRight bounds are not "under" the
      // ship and should not steal physics from an overlapping branch/road.
      let erx = a.edgeRight.x + (b.edgeRight.x - a.edgeRight.x) * t;
      let ery = a.edgeRight.y + (b.edgeRight.y - a.edgeRight.y) * t;
      let erz = a.edgeRight.z + (b.edgeRight.z - a.edgeRight.z) * t;
      const erl = Math.hypot(erx, ery, erz) || 1;
      erx /= erl; ery /= erl; erz /= erl;
      const cx = a.pos.x + (b.pos.x - a.pos.x) * t;
      const cy = a.pos.y + (b.pos.y - a.pos.y) * t;
      const cz = a.pos.z + (b.pos.z - a.pos.z) * t;
      const cosR2 = erx * erx + erz * erz || 1;
      const lateral = ((x - cx) * erx + (z - cz) * erz) / cosR2;
      let loS = (a.sLeft + (b.sLeft - a.sLeft) * t) + TrackCore.COLLISION_WALL_MARGIN;
      let hiS = (a.sRight + (b.sRight - a.sRight) * t) - TrackCore.COLLISION_WALL_MARGIN;
      if (loS > hiS) { const m = (loS + hiS) / 2; loS = m; hiS = m; }
      let wouldOffEnd = false;
      if (!path.closed) {
        if (i === 0 && t <= 1e-4) {
          const e = cl[0];
          wouldOffEnd = !connectedEndpointIds.has(path.endpointIds.start) &&
            ((x - e.pos.x) * e.tangent.x + (z - e.pos.z) * e.tangent.z) < 0;
        } else if (j === M - 1 && t >= 1 - 1e-4) {
          const e = cl[M - 1];
          wouldOffEnd = !connectedEndpointIds.has(path.endpointIds.end) &&
            ((x - e.pos.x) * e.tangent.x + (z - e.pos.z) * e.tangent.z) > 0;
        }
      }
      if (!wouldOffEnd && lateral >= loS && lateral <= hiS && (!bestUnder || d < bestUnder.d)) bestUnder = { path, a: i, b: j, t, d };
    }
  }
  const best = bestUnder || fallback;
  const bestPath = best.path, bestA = best.a, bestB = best.b;
  const cl = bestPath.centerline;
  const a = cl[bestA], b = cl[bestB];
  const t = best.t;

  _sample.pos.copy(a.pos).lerp(b.pos, t);
  _sample.tangent.copy(a.tangent).lerp(b.tangent, t).normalize();
  _sample.edgeRight.copy(a.edgeRight).lerp(b.edgeRight, t).normalize();
  _sample.normal.copy(a.normal).lerp(b.normal, t).normalize();
  _sample.halfW = a.halfW + (b.halfW - a.halfW) * t;
  _sample.crossSectionCurvature = a.crossSectionCurvature + (b.crossSectionCurvature - a.crossSectionCurvature) * t;
  _sample.crossSectionTightness = a.crossSectionTightness + (b.crossSectionTightness - a.crossSectionTightness) * t;
  _sample.sLeft = a.sLeft + (b.sLeft - a.sLeft) * t;
  _sample.sRight = a.sRight + (b.sRight - a.sRight) * t;
  _sample.offEnd = false;
  if (!bestPath.closed) {
    const M = bestPath.centerline.length;
    if (bestA === 0 && t <= 1e-4) {
      const e = bestPath.centerline[0];
      _sample.offEnd = !connectedEndpointIds.has(bestPath.endpointIds.start) &&
        ((x - e.pos.x) * e.tangent.x + (z - e.pos.z) * e.tangent.z) < 0;
    } else if (bestB === M - 1 && t >= 1 - 1e-4) {
      const e = bestPath.centerline[M - 1];
      _sample.offEnd = !connectedEndpointIds.has(bestPath.endpointIds.end) &&
        ((x - e.pos.x) * e.tangent.x + (z - e.pos.z) * e.tangent.z) > 0;
    }
  }
  return _sample;
}

// ---------- Player vehicle ----------
const shipGroup = new THREE.Group();
const bodyGeo = new THREE.BoxGeometry(1.2, 0.4, 2.0);
const bodyMat = new THREE.MeshStandardMaterial({ color: 0xd85f14, metalness: 0.35, roughness: 0.4, emissive: 0x331400, emissiveIntensity: 0.15, flatShading: true });
const body = new THREE.Mesh(bodyGeo, bodyMat);
body.position.y = 0.3;
shipGroup.add(body);

// nose accent so orientation is obvious
const noseGeo = new THREE.ConeGeometry(0.35, 0.8, 4);
const noseMat = new THREE.MeshStandardMaterial({ color: 0x00a8cc, emissive: 0x004866, emissiveIntensity: 0.25, flatShading: true });
const nose = new THREE.Mesh(noseGeo, noseMat);
nose.rotation.x = Math.PI / 2;
nose.rotation.y = Math.PI / 4;
nose.position.set(0, 0.3, 1.25);
shipGroup.add(nose);

scene.add(shipGroup);

// Place the ship at control point 0's region, facing along the track, and clear
// its motion. Called by buildTrack() on load and on every JSON import.
function resetShip() {
  // Start at the chosen control point (the spline doesn't pass exactly through
  // its control points, so find the closest baked sample rather than assuming
  // it's sample `point`), on the chosen path, facing its natural tangent
  // direction or the reverse of it.
  const pathIndex = THREE.MathUtils.clamp(trackStart.path, 0, paths.length - 1);
  const startPath = paths[pathIndex];
  const pointIndex = THREE.MathUtils.clamp(trackStart.point, 0, startPath.anchors.length - 1);
  const anchor = startPath.anchors[pointIndex];
  const cl = startPath.centerline;
  let startIndex = 0, bestD = Infinity;
  for (let i = 0; i < cl.length; i++) {
    const d = cl[i].pos.distanceToSquared(anchor);
    if (d < bestD) { bestD = d; startIndex = i; }
  }
  const s = cl[startIndex];
  // Centre of the track, following the actual cross-section geometry if enabled.
  const startSurface = curvedSurfaceFrame(s, 0);
  physics.groundPos.set(s.pos.x, startSurface.pos.y, s.pos.z);
  shipGroup.position.copy(startSurface.pos).addScaledVector(startSurface.normal, 0.5);
  let heading = Math.atan2(s.tangent.x, s.tangent.z);
  if (trackStart.reverse) heading += Math.PI;
  physics.heading = heading;
  physics.velocityAngle = heading;
  physics.speed = 0;
  physics.visualBank = 0;
  physics.visualPitch = 0;
  physics.airborne = false;
  physics.verticalVel = 0;
  physics.landingBounce = 0;
  physics.landingBounceVel = 0;
  physics.up.copy(startSurface.normal);
  physics.visualGroundPos.copy(startSurface.pos);
  physics.visualUp.copy(physics.up);
  physics.forward.set(Math.sin(heading), 0, Math.cos(heading));
  physics.right.set(Math.cos(heading), 0, -Math.sin(heading));
}

// ---------- Input ----------
const keys = {};
window.addEventListener('keydown', (e) => { keys[e.code] = true; });
window.addEventListener('keyup', (e) => { keys[e.code] = false; });

function isDown(...codes) { return codes.some(c => keys[c]); }

// ---------- Wipeout-style hover physics ----------
const physics = {
  heading: 0,        // facing direction (radians) — set by resetShip()
  velocityAngle: 0,  // direction the vehicle is actually moving
  speed: 0,                // signed scalar speed (units/sec)
  maxSpeed: 34,
  maxReverse: -12,
  accel: 26,
  brakeDecel: 42,
  friction: 20,      // decel when neither throttle nor brake held -- stops quickly when let go
  turnRate: 2.4,           // rad/sec at low speed
  grip: 3.2,               // how fast velocity direction chases heading (lower = more slide)
  bobTime: 0,
  visualBank: 0,           // smoothed steer-lean (rad)
  visualPitch: 0,          // smoothed accel-pitch (rad)
  up: new THREE.Vector3(0, 1, 0),      // current road surface normal
  forward: new THREE.Vector3(0, 0, 1), // ship's actual (banked) forward, orthogonal to `up`
  right: new THREE.Vector3(1, 0, 0),   // ship's actual (banked) right, orthogonal to both
  // Hover-free reference position (on the ribbon surface, no bob offset).
  // Velocity is integrated from THIS, not from shipGroup.position -- the
  // rendered position has the hover bob added along `normal`, and once the
  // track is banked `normal` isn't purely vertical, so its bob offset leaks
  // into X/Z. Feeding that back into next frame's lateral (s) projection
  // compounds into a slow one-directional drift on any banked section, even
  // at a dead stop. groundPos sidesteps that by staying bob-free.
  groundPos: new THREE.Vector3(),
  // Smoothed render-only surface position/up. Physics integrates against
  // groundPos directly, but the rendered ship/camera can ease over tiny
  // projection discontinuities (notably at disjoint path seams) instead of
  // visibly popping.
  visualGroundPos: new THREE.Vector3(),
  visualUp: new THREE.Vector3(0, 1, 0),
  airborne: false,
  verticalVel: 0,
  gravity: 30,
  landingBounce: 0,
  landingBounceVel: 0
};

function lerpAngle(a, b, t) {
  let diff = ((b - a + Math.PI) % (Math.PI * 2)) - Math.PI;
  if (diff < -Math.PI) diff += Math.PI * 2;
  return a + diff * t;
}

function updatePhysics(dt) {
  const throttle = isDown('KeyW', 'ArrowUp') ? 1 : 0;
  const brake = isDown('KeyS', 'ArrowDown') ? 1 : 0;
  const steer = (isDown('KeyD', 'ArrowRight') ? -1 : 0) + (isDown('KeyA', 'ArrowLeft') ? 1 : 0);
  const hasTranslation = !!(throttle || brake || Math.abs(physics.speed) > 0.001);

  // Longitudinal speed control
  if (throttle) {
    physics.speed += physics.accel * dt;
  } else if (brake) {
    physics.speed -= physics.brakeDecel * dt;
  } else {
    // natural friction bleeds speed toward 0
    const decay = physics.friction * dt;
    if (physics.speed > 0) physics.speed = Math.max(0, physics.speed - decay);
    else physics.speed = Math.min(0, physics.speed + decay);
  }
  physics.speed = THREE.MathUtils.clamp(physics.speed, physics.maxReverse, physics.maxSpeed);

  // Steering: turn rate scales down slightly at top speed for stability,
  // and inverts naturally when reversing.
  // `heading` is a WORLD-space yaw, but the driver's frame of reference is the
  // road surface. Once the track banks past vertical (roll > 90deg, up to
  // ~180deg inverted) the surface normal points downward and the ship/camera
  // are upside down, so a fixed world-yaw turn reads as reversed to the
  // driver. Flip the steer by the sign of the surface normal's vertical
  // component so "left" stays the driver's left through inverted sections.
  const upSign = physics.up.y < 0 ? -1 : 1;
  const speedRatio = Math.abs(physics.speed) / physics.maxSpeed;
  const effectiveTurn = physics.turnRate * (1 - 0.35 * speedRatio) * Math.sign(physics.speed || 1);
  physics.heading += steer * effectiveTurn * dt * upSign;

  // Hover "grip": velocity direction chases heading, but lags behind under
  // hard turns at speed -> gives the classic wipeout slide/drift feel.
  const gripThisFrame = physics.grip * (0.5 + 0.5 * (1 - Math.min(Math.abs(steer) * speedRatio, 1)));
  physics.velocityAngle = lerpAngle(physics.velocityAngle, physics.heading, Math.min(gripThisFrame * dt, 1));

  // Integrate/reproject the hover-free ground reference only while the ship is
  // translating (throttle/brake/coasting). Steering in place should rotate the
  // ship without reprojecting groundPos: reprojecting a stationary point onto a
  // banked/sloped cross-section can introduce tiny X/Z corrections that look
  // like movement while turning on the spot.
  let c = sampleTrack(physics.groundPos.x, physics.groundPos.z);
  let surfaceNormal = c.normal;
  let surfaceRenderPos = physics.groundPos;
  const vx = Math.sin(physics.velocityAngle) * physics.speed;
  const vz = Math.cos(physics.velocityAngle) * physics.speed;

  const projectToSurface = (sample, px, pz) => {
    const er = sample.edgeRight;
    const cosR2 = er.x * er.x + er.z * er.z || 1;         // = cos^2(roll)
    let s = ((px - sample.pos.x) * er.x + (pz - sample.pos.z) * er.z) / cosR2;

    // Drivable corridor is bounded by the rendered physical walls: the trimmed
    // edge offsets inset by the ship margin.
    let loS = sample.sLeft + TrackCore.COLLISION_WALL_MARGIN;    // inner limit of the left edge (negative side)
    let hiS = sample.sRight - TrackCore.COLLISION_WALL_MARGIN;   // inner limit of the right edge (positive side)
    if (loS > hiS) { const m = (loS + hiS) / 2; loS = m; hiS = m; } // corridor pinched to a point
    return { er, s, loS, hiS };
  };

  if (physics.airborne) {
    const px = physics.groundPos.x + vx * dt;
    const pz = physics.groundPos.z + vz * dt;
    physics.verticalVel -= physics.gravity * dt;
    physics.groundPos.set(px, physics.groundPos.y + physics.verticalVel * dt, pz);

    c = sampleTrack(px, pz);
    const { er, s, loS, hiS } = projectToSurface(c, px, pz);
    const surface = curvedSurfaceFrame(c, s);
    if (!c.offEnd && s >= loS && s <= hiS && physics.groundPos.y <= surface.pos.y) {
      const impactSpeed = Math.max(0, -physics.verticalVel);
      physics.airborne = false;
      physics.verticalVel = 0;
      physics.landingBounce += Math.min(1.6, impactSpeed * 0.09);
      physics.landingBounceVel += Math.min(8, impactSpeed * 0.35);
      physics.groundPos.set(c.pos.x + er.x * s, surface.pos.y, c.pos.z + er.z * s);
      surfaceRenderPos = surface.pos;
      surfaceNormal = surface.normal;
    }
  } else if (hasTranslation) {
    let px = physics.groundPos.x + vx * dt;
    let pz = physics.groundPos.z + vz * dt;

    // First test the proposed move against the track segment we were already
    // riding. If that move crosses its wall, resolve against THIS segment before
    // allowing sampleTrack() to choose another overlapping branch. Otherwise,
    // holding into a wall can tunnel through to a different nearby/overlapping
    // ribbon whose corridor contains the post-wall position.
    const current = c;
    let projection = projectToSurface(current, px, pz);
    let forceCurrentWall = !current.offEnd && (projection.s > projection.hiS || projection.s < projection.loS);

    if (!forceCurrentWall) {
      c = sampleTrack(px, pz);
      projection = projectToSurface(c, px, pz);
    }

    if (!forceCurrentWall && c.offEnd) {
      // Leaving an open curve: stop projecting to the clamped endpoint and let
      // the craft continue ballistically until its X/Z is over a curve again.
      physics.airborne = true;
      physics.verticalVel = 0;
      physics.groundPos.set(px, physics.groundPos.y, pz);
    } else {
      const { er, s, loS, hiS } = projection;

      let hitSign = 0;
      if (s > hiS) hitSign = 1; else if (s < loS) hitSign = -1;
      let finalS = s;
      if (hitSign) {
        finalS = THREE.MathUtils.clamp(s, loS, hiS);
        // Slide along the wall we hit: cancel only the velocity component pushing
        // into it (with light scrub), so the ship glides instead of stopping dead.
        const nl = Math.hypot(er.x, er.z) || 1;
        const wnx = (er.x / nl) * hitSign, wnz = (er.z / nl) * hitSign; // outward normal of hit wall
        let vX = Math.sin(physics.velocityAngle) * physics.speed;
        let vZ = Math.cos(physics.velocityAngle) * physics.speed;
        const into = vX * wnx + vZ * wnz;
        if (into > 0) { vX -= wnx * into; vZ -= wnz * into; }
        physics.speed = Math.hypot(vX, vZ) * 0.98;
        physics.velocityAngle = Math.atan2(vX, vZ);
      }

      const surface = curvedSurfaceFrame(c, finalS);
      physics.groundPos.set(c.pos.x + er.x * finalS, surface.pos.y, c.pos.z + er.z * finalS);
      surfaceRenderPos = surface.pos;
      surfaceNormal = surface.normal;
    }
  }

  if (!physics.airborne && !hasTranslation) {
    c = sampleTrack(physics.groundPos.x, physics.groundPos.z);
    const { s, loS, hiS } = projectToSurface(c, physics.groundPos.x, physics.groundPos.z);
    const finalS = THREE.MathUtils.clamp(s, loS, hiS);
    const surface = curvedSurfaceFrame(c, finalS);
    physics.groundPos.set(c.pos.x + c.edgeRight.x * finalS, surface.pos.y, c.pos.z + c.edgeRight.z * finalS);
    surfaceRenderPos = surface.pos;
    surfaceNormal = surface.normal;
  }

  // Sit on the surface, hovering a little along the surface normal. groundPos
  // (bob-free) is what next frame's integration reads from; shipGroup's
  // actual rendered position adds the hover bob on top of it. At disjoint
  // seams the nearest path segment can switch, producing a small projection
  // correction in groundPos; smooth only unexpectedly-large render deltas so
  // normal movement stays responsive while seam pops are eased out.
  const expectedStep = Math.abs(physics.speed) * dt * 1.5 + 0.08;
  const renderDelta = physics.visualGroundPos.distanceTo(surfaceRenderPos);
  if (renderDelta > expectedStep) {
    physics.visualGroundPos.lerp(surfaceRenderPos, Math.min(1, dt * 18));
  } else {
    physics.visualGroundPos.copy(surfaceRenderPos);
  }
  physics.visualUp.lerp(surfaceNormal, Math.min(1, dt * 18)).normalize();

  // Damped spring for landing impacts: a harder fall creates a larger, quick
  // hover rebound after re-contacting the track.
  physics.landingBounceVel += -55 * physics.landingBounce * dt;
  physics.landingBounceVel *= Math.exp(-7 * dt);
  physics.landingBounce += physics.landingBounceVel * dt;
  if (Math.abs(physics.landingBounce) < 0.001 && Math.abs(physics.landingBounceVel) < 0.001) {
    physics.landingBounce = 0; physics.landingBounceVel = 0;
  }

  physics.bobTime += dt;
  const hover = 0.5 + Math.sin(physics.bobTime * 6) * 0.03 + physics.landingBounce;
  shipGroup.position.set(
    physics.visualGroundPos.x + physics.visualUp.x * hover,
    physics.visualGroundPos.y + physics.visualUp.y * hover,
    physics.visualGroundPos.z + physics.visualUp.z * hover
  );
  physics.up.copy(physics.visualUp);

  // Orient the ship to the road: nose along heading, "up" along surface normal.
  const up = physics.visualUp;
  const fwd = new THREE.Vector3(Math.sin(physics.heading), 0, Math.cos(physics.heading));
  fwd.addScaledVector(up, -fwd.dot(up)).normalize();
  const right = new THREE.Vector3().crossVectors(up, fwd).normalize();
  const forward = new THREE.Vector3().crossVectors(right, up).normalize();
  shipGroup.quaternion.setFromRotationMatrix(new THREE.Matrix4().makeBasis(right, up, forward));
  // Keep this exact orthonormal basis around for the chase camera, so it
  // always sits directly behind/above the ship using the SAME up/forward --
  // not a separately-computed (and potentially non-orthogonal) pair.
  physics.right.copy(right);
  physics.forward.copy(forward);

  // Extra hover flair: lean into the steer, pitch back under acceleration.
  const targetBank = THREE.MathUtils.clamp(-steer * speedRatio * 0.5, -0.5, 0.5);
  physics.visualBank = THREE.MathUtils.lerp(physics.visualBank, targetBank, dt * 6);
  physics.visualPitch = THREE.MathUtils.lerp(physics.visualPitch, physics.speed * 0.004, dt * 6);
  shipGroup.quaternion.multiply(
    new THREE.Quaternion().setFromEuler(new THREE.Euler(physics.visualPitch, 0, physics.visualBank, 'XYZ'))
  );

  // HUD
  const kmh = Math.round(Math.abs(physics.speed) * 9);
  document.getElementById('speed').innerHTML = kmh + ' <span>km/h</span>';
}

// ---------- Chase camera (rigidly fixed behind and above the ship) ----------
const CAM_BACK = 6.5;   // distance behind the ship
const CAM_UP = 3.2;     // height above the ship
function updateCamera(dt) {
  // Use the ship's own orthonormal (forward, up) basis -- the same one its
  // quaternion was built from -- rather than a flat heading-only forward.
  // Mixing a flat forward with the fully-banked `up` (as before) isn't even
  // orthogonal once the track rolls, so the camera would drift out from
  // directly behind the ship; and leaving camera.up at world Y meant the
  // rendered horizon didn't bank with the track, fighting the position
  // offset. Setting camera.up to the track's own up keeps the camera locked
  // directly behind/above the ship at any roll, including past 90 degrees.
  const up = physics.up, fwd = physics.forward;
  const base = physics.visualGroundPos.clone().addScaledVector(up, 0.5);
  camera.up.copy(up);
  camera.position.copy(base)
    .addScaledVector(fwd, -CAM_BACK)
    .addScaledVector(up, CAM_UP);
  const lookAt = base.clone()
    .addScaledVector(fwd, 6)
    .addScaledVector(up, 0.8);
  camera.lookAt(lookAt);
}

// ---------- Track loading: built-in default + JSON import --------------------
function loadStoredTrack() {
  try {
    const stored = localStorage.getItem('web3d.currentTrack');
    return stored ? TrackCore.parseTrack(stored) : null;
  } catch (err) {
    console.warn('Could not load editor preview track:', err);
    return null;
  }
}
buildTrack(loadStoredTrack() || TrackCore.cloneTrack(TrackCore.DEFAULT_TRACK));
window.addEventListener('storage', (e) => {
  if (e.key !== 'web3d.currentTrack' || !e.newValue) return;
  try { buildTrack(TrackCore.parseTrack(e.newValue)); }
  catch (err) { console.warn('Could not live-update track from editor:', err); }
});

const fileInput = document.getElementById('fileInput');
document.getElementById('importBtn').addEventListener('click', () => fileInput.click());
fileInput.addEventListener('change', async (e) => {
  const file = e.target.files[0];
  if (!file) return;
  try {
    const track = TrackCore.parseTrack(await file.text());
    buildTrack(track);
  } catch (err) {
    alert('Could not load track: ' + err.message);
  }
  e.target.value = '';   // allow re-importing the same file
});

// ---------- Main loop ----------
const clock = new THREE.Clock();
function animate() {
  requestAnimationFrame(animate);
  const dt = Math.min(clock.getDelta(), 0.05);
  updatePhysics(dt);
  updateCamera(dt);
  renderer.render(scene, camera);
}
animate();
