
import * as TrackMesh from './track-mesh.js';
import { DEFAULT_SHIP_COUNT, gridSlot } from './ship-grid.js';

// ---------- Scene setup ----------
const scene = new THREE.Scene();
scene.background = new THREE.Color(0x02040a);
scene.fog = new THREE.Fog(0x02040a, 120, 440);

const camera = new THREE.PerspectiveCamera(
  65, window.innerWidth / window.innerHeight, 0.2, 2000
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
const N = TrackCore.N_DEFAULT;         // fallback bake size; buildTrack picks a per-path adaptive count (TrackCore.adaptiveSampleCount)
const UP = new THREE.Vector3(0, 1, 0);
const toVec = o => new THREE.Vector3(o.x, o.y, o.z);
// The road's cross-section profile lives in TrackCore, shared with the editor's
// preview and the USD exporter so all three draw the same surface.
const crossSectionHeight = TrackCore.crossSectionHeight;
const crossSectionHeightDerivative = TrackCore.crossSectionHeightDerivative;
const crossSectionBreakpoints = TrackCore.crossSectionBreakpoints;
// Merge two rings' adaptive v-breakpoints so a longitudinal strip between them
// has a vertex everywhere either ring wanted one -- both rings are continuous
// analytic curves, so evaluating one at the other's breakpoint is exact, not
// an approximation. That's what turns a would-be T-junction/gap into an
// ordinary (if sometimes denser-than-either-ring-alone) quad grid.
function unionBreakpoints(a, b) {
  const set = new Set(a);
  for (const v of b) set.add(v);
  return Array.from(set).sort((x, y) => x - y);
}

let paths = [];                        // compiled paths: { closed, centerline, mesh, stripeLine, railR, railL, anchors }
let connectedEndpointIds = new Set();  // shared/disjoint/branch endpoint point IDs that should not launch off-end
// Mesh regions: flat drivable areas imported from the geometry-js editor. Each
// is { compiled, elevation, railHeight, surface, railMesh } where `compiled` is
// the world-space bake from track-mesh.js that physics queries every frame.
let meshRegions = [];
let trackFloorY = -1e9;                // auto-respawn threshold, set by buildTrack()
// Rails are collision geometry everywhere regardless of this flag -- G is
// purely a rendering toggle and never changes what stops the ship. Off by
// default so the track reads cleanly; press G to see the walls.
let showGuardRails = false;
let showWireframe = false;
let trackName = '';
let trackStart = { path: 0, point: 0, reverse: false };

function textureGrid(asset) {
  if (!asset) return { cols: 0, rows: 0, count: 0 };
  const cols = Math.floor(asset.width / asset.tileWidth);
  const rows = Math.floor(asset.height / asset.tileHeight);
  return { cols, rows, count: cols * rows };
}

function extractTileTexture(asset, tile) {
  return new Promise((resolve, reject) => {
    const grid = textureGrid(asset);
    if (!asset || tile < 0 || tile >= grid.count) return reject(new Error('invalid texture tile'));
    const img = new Image();
    img.onload = () => {
      const sx = (tile % grid.cols) * asset.tileWidth;
      const sy = Math.floor(tile / grid.cols) * asset.tileHeight;
      const canvas = document.createElement('canvas');
      canvas.width = asset.tileWidth;
      canvas.height = asset.tileHeight;
      canvas.getContext('2d').drawImage(img, sx, sy, asset.tileWidth, asset.tileHeight, 0, 0, asset.tileWidth, asset.tileHeight);
      const tex = new THREE.CanvasTexture(canvas);
      tex.wrapS = THREE.ClampToEdgeWrapping;
      tex.wrapT = THREE.RepeatWrapping;
      if (THREE.SRGBColorSpace) tex.colorSpace = THREE.SRGBColorSpace;
      else if (THREE.sRGBEncoding) tex.encoding = THREE.sRGBEncoding;
      tex.needsUpdate = true;
      resolve(tex);
    };
    img.onerror = () => reject(new Error('could not load texture image'));
    img.src = asset.path;
  });
}

function applyPathTexture(mesh, pathTexture, textureAsset) {
  if (!pathTexture || !textureAsset) return;
  extractTileTexture(textureAsset, pathTexture.tile).then((tex) => {
    if (!mesh.parent) { tex.dispose(); return; }
    const old = mesh.material;
    mesh.material = new THREE.MeshBasicMaterial({
      map: tex, color: 0xffffff, side: THREE.DoubleSide
    });
    if (old) old.dispose();
  }).catch(err => console.warn('Could not apply track texture:', err));
}

function disposeObject(obj) {
  if (!obj) return;
  scene.remove(obj);
  if (obj.geometry) obj.geometry.dispose();
  if (obj.material) {
    if (obj.material.map) obj.material.map.dispose();
    obj.material.dispose();
  }
}

// Compile a single path (closed loop or open curve) into renderable geometry
// and a physics-ready centerline.
function buildPath(controlPoints, closed, rollPoints, widthPoints, crossSectionPoints, prebuiltRaw, prebuiltEdges, endpointCuts, endpointNormals, deciders, skipSelfIntersectionCleanup, pathTexture, textureAsset) {
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
  //
  // Longitudinal ring spacing is ALSO adaptive, MESH-ONLY: physics keeps riding
  // on the fixed, uniform `raw`/`edges` above (untouched -- centerline,
  // wallOffsets, guard rails all still read those), but the visual road
  // surface + shell are built from a separate, denser-or-sparser frame array
  // that TrackCore.buildAdaptiveMeshFrames derives from them. Every frame a
  // self-intersection fold (or a disjoint-seam endpoint override, above) moved
  // is carried through byte-for-byte, so the rendered corner always matches
  // the physics corridor exactly there; everywhere else -- most of a typical
  // track -- rings are freely added on sharp bends/hills or thinned out on
  // long straights. See TrackCore.buildAdaptiveMeshFrames and CLAUDE.md.
  const meshBake = TrackCore.buildAdaptiveMeshFrames(controlPoints, closed, rollPoints, widthPoints, crossSectionPoints, raw, edges);
  const meshRaw = meshBake.frames, meshEdges = meshBake.edges;
  const meshN = meshRaw.length;
  const roadMaterial = () => new THREE.MeshBasicMaterial({ color: 0x7fb4d4, side: THREE.DoubleSide });
  const surfacePoint = (frameIndex, v) => {
    const left = meshEdges.left[frameIndex], right = meshEdges.right[frameIndex], f = meshRaw[frameIndex];
    const chord = { x: right.x - left.x, y: right.y - left.y, z: right.z - left.z };
    const chordWidth = Math.hypot(chord.x, chord.y, chord.z) || 1;
    const h = crossSectionHeight(f.crossSectionCurvature, f.crossSectionTightness, v, chordWidth);
    return [
      left.x + chord.x * v + f.normal.x * h,
      left.y + chord.y * v + f.normal.y * h,
      left.z + chord.z * v + f.normal.z * h
    ];
  };
  // Adaptive cross-section resolution: each ring picks its own v-breakpoints
  // from how sharply ITS OWN curvature/tightness bend the profile (flat rings
  // stay coarse, tightly-curved ones subdivide). A ring is shared by TWO
  // strips (its left and right neighbor), which can each need different
  // "foreign" v's from it -- so ring i's own edge is always drawn through
  // ringPoint/ringUnderPoint (crossSectionStitchPoint), never surfacePoint
  // directly, for any v that didn't come from the ring's own breakpoints.
  // That pins the ring's rendered edge to a single, fixed polyline regardless
  // of which strip is asking, which is what actually prevents the crack --
  // evaluating the true analytic surface at both rings' union of v's (the
  // obvious-looking fix) is NOT enough, because it still lets the SAME ring
  // draw two different polylines when its two neighbors ask for different
  // extra points.
  const pos = [], uv = [];
  const pushPoint = p => pos.push(p[0], p[1], p[2]);
  const pushUv = (u, v) => uv.push(u, v);
  const distances = [0];
  for (let i = 1; i < meshRaw.length; i++) {
    const a = meshRaw[i - 1].pos, b = meshRaw[i].pos;
    distances[i] = distances[i - 1] + Math.hypot(b.x - a.x, b.y - a.y, b.z - a.z);
  }
  const avgWidth = meshRaw.reduce((sum, f) => sum + Math.max(1, f.width || 1), 0) / Math.max(1, meshRaw.length);
  const segCount = closed ? meshN : meshN - 1;
  const ringBreaks = meshRaw.map((f, i) => {
    const left = meshEdges.left[i], right = meshEdges.right[i];
    const chordWidth = Math.hypot(right.x - left.x, right.y - left.y, right.z - left.z) || 1;
    return crossSectionBreakpoints(f.crossSectionCurvature, f.crossSectionTightness, chordWidth);
  });
  const ringPoint = (ring, v) => TrackCore.crossSectionStitchPoint(ringBreaks[ring], v, vv => surfacePoint(ring, vv));
  for (let i = 0; i < segCount; i++) {
    const ni = closed ? (i + 1) % meshN : i + 1;
    const breaks = unionBreakpoints(ringBreaks[i], ringBreaks[ni]);
    const t0 = distances[i] / avgWidth;
    const t1 = (closed && ni === 0) ? ((distances[i] + Math.hypot(meshRaw[ni].pos.x - meshRaw[i].pos.x, meshRaw[ni].pos.y - meshRaw[i].pos.y, meshRaw[ni].pos.z - meshRaw[i].pos.z)) / avgWidth) : distances[ni] / avgWidth;
    for (let k = 0; k < breaks.length - 1; k++) {
      const v0 = breaks[k], v1 = breaks[k + 1];
      const a = ringPoint(i, v0), b = ringPoint(i, v1);
      const c = ringPoint(ni, v0), d = ringPoint(ni, v1);
      pushPoint(a); pushUv(v0, t0); pushPoint(b); pushUv(v1, t0); pushPoint(c); pushUv(v0, t1);
      pushPoint(b); pushUv(v1, t0); pushPoint(d); pushUv(v1, t1); pushPoint(c); pushUv(v0, t1);
    }
  }
  const flatG = new THREE.BufferGeometry();
  flatG.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  flatG.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  flatG.computeVertexNormals();
  const mesh = new THREE.Mesh(flatG, roadMaterial());
  applyPathTexture(mesh, pathTexture, textureAsset);
  scene.add(mesh);

  const wireLine = new THREE.LineSegments(
    new THREE.WireframeGeometry(flatG),
    new THREE.LineBasicMaterial({ color: 0x102838, transparent: true, opacity: 0.55, depthTest: false })
  );
  wireLine.visible = showWireframe;
  wireLine.renderOrder = 10;
  scene.add(wireLine);

  // ---- Shell -------------------------------------------------------------
  // Extrude the whole cross-section straight down its own normal to give the
  // ribbon an underside and two side walls, so an elevated road reads as a slab
  // rather than a paper sheet. Thickness comes from the cross-section spline, so
  // it can taper along the path like curvature and width do.
  //
  // Built as its OWN mesh, deliberately, for two reasons: the road surface's UV
  // mapping stays exactly as it was (a path texture would otherwise smear around
  // the sides and underside), and the substructure gets its own darker material.
  // It is purely visual -- physics still projects onto the top surface only, so
  // nothing here can change how the ship drives.
  const underPoint = (frameIndex, v) => {
    const s = surfacePoint(frameIndex, v);
    const f = meshRaw[frameIndex];
    const t = f.crossSectionThickness || 0;
    return [s[0] - f.normal.x * t, s[1] - f.normal.y * t, s[2] - f.normal.z * t];
  };
  const ringUnderPoint = (ring, v) => TrackCore.crossSectionStitchPoint(ringBreaks[ring], v, vv => underPoint(ring, vv));
  let shell = null;
  if (meshRaw.some(f => (f.crossSectionThickness || 0) > 1e-6)) {
    const shellPos = [];
    const tri = (p, q, r) => shellPos.push(p[0], p[1], p[2], q[0], q[1], q[2], r[0], r[1], r[2]);
    // Same two-triangle split the top surface uses, so a quad here tessellates
    // identically to the strip above it.
    const quad = (p, q, r, s) => { tri(p, q, r); tri(q, s, r); };

    for (let i = 0; i < segCount; i++) {
      const ni = closed ? (i + 1) % meshN : i + 1;
      const breaks = unionBreakpoints(ringBreaks[i], ringBreaks[ni]);
      for (let k = 0; k < breaks.length - 1; k++) {
        const v0 = breaks[k], v1 = breaks[k + 1];
        const a = ringUnderPoint(i, v0), b = ringUnderPoint(i, v1);
        const c = ringUnderPoint(ni, v0), d = ringUnderPoint(ni, v1);
        tri(a, c, b); tri(b, c, d);          // reversed vs the top, so it faces down
      }
      quad(surfacePoint(i, 0), underPoint(i, 0), surfacePoint(ni, 0), underPoint(ni, 0));
      quad(underPoint(i, 1), surfacePoint(i, 1), underPoint(ni, 1), surfacePoint(ni, 1));
    }
    // An open curve is a cut slab: cap both ends so you cannot see into it.
    // A closed loop wraps and needs none. A cap only ever touches one ring, so
    // it uses that ring's own breakpoints directly -- no union needed.
    if (!closed) {
      for (const end of [0, meshN - 1]) {
        const breaks = ringBreaks[end];
        for (let k = 0; k < breaks.length - 1; k++) {
          const v0 = breaks[k], v1 = breaks[k + 1];
          quad(surfacePoint(end, v0), underPoint(end, v0), surfacePoint(end, v1), underPoint(end, v1));
        }
      }
    }

    const shellG = new THREE.BufferGeometry();
    shellG.setAttribute('position', new THREE.Float32BufferAttribute(shellPos, 3));
    shellG.computeVertexNormals();
    shell = new THREE.Mesh(shellG, new THREE.MeshStandardMaterial({
      color: 0x3b5c72, roughness: 0.9, metalness: 0.05, side: THREE.DoubleSide, flatShading: true
    }));
    scene.add(shell);
  }

  const stripeLine = null;

  // Optional guard rails, toggled with G. Collision remains driven by
  // centerline.sLeft/sRight; these meshes are visual only.
  const buildGuardRail = (sideKey, color) => {
    const RAIL_H = 1.8, LIFT = 0.04;
    const pos = [], idx = [];
    for (const c of centerline) {
      const surface = curvedSurfaceFrame(c, c[sideKey]);
      const base = surface.pos.clone().addScaledVector(surface.normal, LIFT);
      const top = base.clone().addScaledVector(surface.normal, RAIL_H);
      pos.push(base.x, base.y, base.z, top.x, top.y, top.z);
    }
    const segCount = closed ? centerline.length : centerline.length - 1;
    for (let i = 0; i < segCount; i++) {
      const j = closed ? (i + 1) % centerline.length : i + 1;
      const a = i * 2, b = i * 2 + 1, c = j * 2, d = j * 2 + 1;
      idx.push(a, c, b, b, c, d);
    }
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
    g.setIndex(idx);
    const flatG = g.toNonIndexed();
    flatG.computeVertexNormals();
    g.dispose();
    const m = new THREE.MeshStandardMaterial({
      color, emissive: color, emissiveIntensity: 0.25,
      roughness: 0.55, metalness: 0.05, side: THREE.DoubleSide, flatShading: true
    });
    const rail = new THREE.Mesh(flatG, m);
    rail.visible = showGuardRails;
    return rail;
  };
  const railL = buildGuardRail('sLeft', 0xd8b400);
  const railR = buildGuardRail('sRight', 0xd8b400);
  scene.add(railL);
  scene.add(railR);

  const anchors = controlPoints.map(c => new THREE.Vector3(c.pos[0], c.pos[1], c.pos[2]));
  const endpointIds = {
    start: controlPoints[0] && controlPoints[0].id,
    end: controlPoints[controlPoints.length - 1] && controlPoints[controlPoints.length - 1].id
  };
  return { closed, centerline, mesh, wireLine, shell, stripeLine, railR, railL, anchors, endpointIds };
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
    disposeObject(p.mesh); disposeObject(p.wireLine); disposeObject(p.shell); disposeObject(p.stripeLine);
    disposeObject(p.railR); disposeObject(p.railL);
  }
  buildMeshRegions(track);
  const bakedPaths = trackPaths.map(p => {
    const { controlPoints, rollPoints, widthPoints, crossSectionPoints } = TrackCore.splitPoints(p.points);
    const closed = p.closed !== false;
    // Physics sample count scales with the path's driven length, so a 7-10 km
    // track keeps the same corridor fidelity a ~1 km one had (see
    // TrackCore.adaptiveSampleCount). The SAME count must feed
    // makeSelfIntersectionDeciders below, whose frame->control-id mapping is
    // relative to this frame count.
    const pathN = TrackCore.adaptiveSampleCount(controlPoints, closed, rollPoints, widthPoints, crossSectionPoints);
    const frames = TrackCore.buildCenterline(controlPoints, pathN, closed, rollPoints, widthPoints, crossSectionPoints);
    const edges = TrackCore.buildEdges(frames, closed);
    const hasBranchConnection = controlPoints.some(cp => cp && branchPointIds.has(cp.id));
    return { id: p.id, closed, controlPoints, rollPoints, widthPoints, crossSectionPoints, frames, edges, hasBranchConnection, texture: p.texture || null, pathN };
  });
  const incidentCounts = endpointIncidentCounts(bakedPaths);
  for (const [id, count] of incidentCounts) if (count >= 2) connectedEndpointIds.add(id);
  const disjointSeams = track.disjointSeams || [];
  const overrides = track.selfIntersectionOverrides || [];
  const edgeCuts = TrackCore.computeDisjointEdgeCuts(bakedPaths, disjointSeams);
  const endpointNormals = computeDisjointEndpointNormals(bakedPaths, disjointSeams);
  paths = bakedPaths.map((p, i) => buildPath(
    p.controlPoints, p.closed, p.rollPoints, p.widthPoints, p.crossSectionPoints, p.frames, p.edges, edgeCuts[i], endpointNormals[i],
    TrackCore.makeSelfIntersectionDeciders(p.controlPoints, p.closed, p.pathN, overrides), p.hasBranchConnection,
    p.texture, p.texture && (track.textureAssets || {})[p.texture.asset]
  ));
  // Zones ride on top of the finished paths + mesh regions.
  buildZones(track, bakedPaths);
  buildTriggers(track, bakedPaths);
  // Anything below every drivable surface by this much has clearly fallen off
  // and is never coming back, so it triggers an automatic respawn.
  let lowest = Infinity;
  for (const p of paths) for (const f of p.centerline) lowest = Math.min(lowest, f.pos.y);
  for (const region of meshRegions) lowest = Math.min(lowest, region.elevation);
  trackFloorY = (isFinite(lowest) ? lowest : 0) - RESPAWN_FALL_DEPTH;

  buildRoster(track);
  const label = document.getElementById('trackName');
  if (label) label.textContent = trackName;
  computeMinimapBounds();
}

// ---------- Mesh regions ----------
// Flat drivable areas. A region is horizontal, so its surface is just a plane
// at `elevation` with a +Y normal -- no banking, no cross-section. Railed edges
// become finite-height walls; every other boundary edge is a ledge, and driving
// over one drops the ship into the same ballistic code an open curve's end uses.
const MESH_SURFACE_COLOR = 0x6a4f96;
const MESH_RAIL_COLOR = 0xd8b400;

// ---------- Zones ----------
// Flat rectangular areas floating just above a surface that fire an effect when
// driven over (see track-core.js). Rendered as a solid colour by effect type;
// physics/detection is separate (see detectZoneTriggers/triggerBoost).
let zones = [];
const ZONE_HOVER = 0.15;                 // units above the surface, so they sit clear of z-fighting
const ZONE_RELEASE = 1;                  // seconds for the boost's smooth release back to max
const ZONE_CHECKER = 3;                  // world units per black/white square on the start grid
const ZONE_COLORS = { velocityChange: 0xffa520, startGrid: 0xcfd6dd };

// A black-and-white checkerboard texture for the start grid. A fresh one per
// start-grid mesh, deliberately: disposeObject disposes each mesh's material.map
// on rebuild, so a shared texture would be freed out from under the others.
function makeCheckerTexture() {
  const S = 64;   // pixels per square in the 2x2 tile
  const canvas = document.createElement('canvas');
  canvas.width = canvas.height = S * 2;
  const cx = canvas.getContext('2d');
  cx.fillStyle = '#f2f2f2'; cx.fillRect(0, 0, S * 2, S * 2);
  cx.fillStyle = '#0a0a0a'; cx.fillRect(0, 0, S, S); cx.fillRect(S, S, S, S);
  const tex = new THREE.CanvasTexture(canvas);
  tex.wrapS = tex.wrapT = THREE.RepeatWrapping;
  tex.magFilter = THREE.NearestFilter; tex.minFilter = THREE.NearestFilter;   // crisp edges, no mip blur
  if (THREE.SRGBColorSpace) tex.colorSpace = THREE.SRGBColorSpace;
  else if (THREE.sRGBEncoding) tex.encoding = THREE.sRGBEncoding;
  return tex;
}

// A zone mesh: a checker-textured surface for the start grid (UVs scaled so one
// texture tile spans 2*ZONE_CHECKER world units, i.e. square-in-world checks),
// or a flat solid colour for every other effect.
function zoneMeshFromPositions(pos, uv, effect) {
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  if (uv && uv.length) g.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  g.computeVertexNormals();
  const mat = effect === 'startGrid'
    ? new THREE.MeshBasicMaterial({ map: makeCheckerTexture(), side: THREE.DoubleSide, transparent: true, opacity: 0.92, depthWrite: false })
    : new THREE.MeshBasicMaterial({ color: ZONE_COLORS[effect] || 0xffffff, side: THREE.DoubleSide, transparent: true, opacity: 0.72, depthWrite: false });
  const mesh = new THREE.Mesh(g, mat);
  mesh.renderOrder = 5;   // drawn over the road surface it floats on
  return mesh;
}

function buildMeshRegions(track) {
  for (const region of meshRegions) { disposeObject(region.surface); disposeObject(region.railMesh); }
  meshRegions = [];

  const assets = track.meshAssets || {};
  for (const placement of track.meshes || []) {
    const asset = assets[placement.asset];
    if (!asset) continue;
    let mesh;
    try { mesh = TrackMesh.meshFromJSON(asset.mesh); }
    catch (err) { console.warn(`mesh asset "${placement.asset}" failed to load`, err); continue; }

    const compiled = TrackMesh.compile(mesh, placement);
    const elevation = placement.elevation || 0;
    // parseTrack always fills railHeight in, so this fallback only catches a
    // track object built by hand (tests, the console) that skipped it.
    const railHeight = asset.railHeight == null ? TrackCore.DEFAULT_RAIL_HEIGHT : asset.railHeight;

    // Surface: one flat triangle soup at the region's elevation.
    const surfacePositions = [];
    for (const tri of compiled.triangles) {
      for (const p of tri) surfacePositions.push(p.x, elevation, p.z);
    }
    let surface = null;
    if (surfacePositions.length) {
      const geom = new THREE.BufferGeometry();
      geom.setAttribute('position', new THREE.Float32BufferAttribute(surfacePositions, 3));
      geom.computeVertexNormals();
      surface = new THREE.Mesh(geom, new THREE.MeshStandardMaterial({
        color: MESH_SURFACE_COLOR, roughness: 0.85, metalness: 0.1, side: THREE.DoubleSide
      }));
      scene.add(surface);
    }

    // Rails: an upright quad per flagged edge, drawn double-sided so they read
    // from both approaches.
    const railPositions = [];
    for (const rail of compiled.rails) {
      const top = elevation + railHeight;
      const { a, b } = rail;
      railPositions.push(
        a.x, elevation, a.z, b.x, elevation, b.z, b.x, top, b.z,
        a.x, elevation, a.z, b.x, top, b.z, a.x, top, a.z
      );
    }
    let railMesh = null;
    if (railPositions.length) {
      const geom = new THREE.BufferGeometry();
      geom.setAttribute('position', new THREE.Float32BufferAttribute(railPositions, 3));
      geom.computeVertexNormals();
      railMesh = new THREE.Mesh(geom, new THREE.MeshStandardMaterial({
        color: MESH_RAIL_COLOR, roughness: 0.5, metalness: 0.3, side: THREE.DoubleSide
      }));
      railMesh.visible = showGuardRails;
      scene.add(railMesh);
    }

    meshRegions.push({ compiled, elevation, railHeight, surface, railMesh });
  }
}

// (Re)build the zone meshes and the compiled records physics tests against.
// Path zones reuse TrackCore.zonePathStrip (the same strip the editor previews)
// and keep the g-window it returns for detection; mesh zones are a flat rotated
// rectangle at the region's elevation. Called from buildTrack once `paths` and
// `meshRegions` exist; `bakedPaths` carries each path's control/roll/width/
// cross points (index-aligned with `paths`).
function buildZones(track, bakedPaths) {
  for (const z of zones) disposeObject(z.mesh);
  zones = [];
  for (const zone of track.zones || []) {
    const host = zone.host || {};
    if (host.kind === 'mesh') {
      const region = meshRegions.find(r => r.compiled && r.compiled.id === host.meshId);
      if (!region) continue;
      const y = region.elevation + ZONE_HOVER;
      const rot = (host.rotation || 0) * Math.PI / 180;
      const cos = Math.cos(rot), sin = Math.sin(rot);
      const hl = Math.max(0.25, (zone.length || 0) / 2), hw = Math.max(0.25, (zone.width || 0) / 2);
      // length runs along the local x-axis, width along local z (matches worldToLocal in detection).
      // UVs are the local coordinates scaled so the checker squares are world-sized.
      const uScale = 1 / (2 * ZONE_CHECKER);
      const corner = (lx, lz) => ({ x: host.x + lx * cos - lz * sin, z: host.z + lx * sin + lz * cos, u: lx * uScale, v: lz * uScale });
      const c00 = corner(-hl, -hw), c10 = corner(hl, -hw), c11 = corner(hl, hw), c01 = corner(-hl, hw);
      const pos = [], uv = [];
      const tri = (p, q, r) => { pos.push(p.x, y, p.z, q.x, y, q.z, r.x, y, r.z); uv.push(p.u, p.v, q.u, q.v, r.u, r.v); };
      tri(c00, c10, c11); tri(c00, c11, c01);
      const mesh = zoneMeshFromPositions(pos, uv, zone.effect); scene.add(mesh);
      zones.push({
        id: zone.id, kind: 'mesh', effect: zone.effect, factor: zone.factor, duration: zone.duration,
        hostRegion: region, x: host.x, z: host.z, rot, halfLen: hl, halfWidth: hw, mesh
      });
    } else {
      const idx = bakedPaths.findIndex(bp => bp.id === host.pathId);
      if (idx < 0) continue;
      const bp = bakedPaths[idx];
      const strip = TrackCore.zonePathStrip(bp.controlPoints, bp.closed, bp.rollPoints, bp.widthPoints, bp.crossSectionPoints, zone, ZONE_HOVER);
      // u runs along the strip (cumulative centerline distance), v across it (0
      // at the left edge, width at the right), both scaled so checks are world-sized.
      const uScale = 1 / (2 * ZONE_CHECKER), vW = (zone.width || 0) * uScale;
      const dist = [0];
      for (let i = 1; i < strip.left.length; i++) {
        const ax = (strip.left[i - 1].x + strip.right[i - 1].x) / 2, ay = (strip.left[i - 1].y + strip.right[i - 1].y) / 2, az = (strip.left[i - 1].z + strip.right[i - 1].z) / 2;
        const bx = (strip.left[i].x + strip.right[i].x) / 2, by = (strip.left[i].y + strip.right[i].y) / 2, bz = (strip.left[i].z + strip.right[i].z) / 2;
        dist[i] = dist[i - 1] + Math.hypot(bx - ax, by - ay, bz - az);
      }
      const pos = [], uv = [];
      for (let i = 0; i < strip.left.length - 1; i++) {
        const a = strip.left[i], b = strip.right[i], c = strip.left[i + 1], d = strip.right[i + 1];
        const u0 = dist[i] * uScale, u1 = dist[i + 1] * uScale;
        pos.push(a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z);
        uv.push(u0, 0, u0, vW, u1, 0);
        pos.push(b.x, b.y, b.z, d.x, d.y, d.z, c.x, c.y, c.z);
        uv.push(u0, vW, u1, vW, u1, 0);
      }
      const mesh = zoneMeshFromPositions(pos, uv, zone.effect); scene.add(mesh);
      zones.push({
        id: zone.id, kind: 'path', effect: zone.effect, factor: zone.factor, duration: zone.duration,
        hostPath: paths[idx], gLo: strip.gLo, gHi: strip.gHi, gMax: strip.gMax, closed: strip.closed,
        lateral: host.lateral || 0, halfWidth: Math.max(0.25, (zone.width || 0) / 2), mesh
      });
    }
  }
}

// The ship's evaluator parameter g on the path a sample landed on, recovered
// from the segment (a->b, segT) sampleTrack recorded. Matches buildCenterline's
// frame->g mapping so it lines up with the zone's own gLo/gHi window.
function shipParamG(sample) {
  const p = sample.pathObj;
  if (!p) return 0;
  const M = p.centerline.length, CP_N = p.anchors.length;
  const gAt = i => p.closed ? (i / M) * CP_N : (M > 1 ? (i / (M - 1)) * (CP_N - 1) : 0);
  const ga = gAt(sample.a);
  let gb = gAt(sample.b);
  if (p.closed && sample.b < sample.a) gb += CP_N;   // the wrap segment M-1 -> 0
  return ga + (gb - ga) * sample.segT;
}

// Effective speed cap this frame: the raised boost cap while a boost is running,
// otherwise the normal per-track max.
function effectiveMaxSpeed(physics) {
  return physics.boostActive ? Math.max(physics.maxSpeed, physics.boostEffCap) : physics.maxSpeed;
}
function clearBoost(ship) {
  const physics = ship.physics;
  physics.boostActive = false; physics.boostReleasing = false;
  physics.boostHold = 0; physics.boostReleaseT = 0; physics.boostCap = 0; physics.boostEffCap = 0;
  ship.zoneInside.clear();
}
// Start a boost for one ship. Each ship owns its lock and cap state.
function triggerBoost(ship, zone) {
  const physics = ship.physics;
  if (physics.boostActive) return;
  physics.boostActive = true;
  physics.boostReleasing = false;
  physics.boostHold = zone.duration || TrackCore.DEFAULT_BOOST_DURATION;
  physics.boostReleaseT = ZONE_RELEASE;
  physics.boostCap = (zone.factor || TrackCore.DEFAULT_BOOST_FACTOR) * physics.maxSpeed;
  physics.boostEffCap = physics.boostCap;
  if (physics.speed > 0) physics.speed = Math.max(physics.speed, physics.boostCap);
}
function tickBoost(ship, dt) {
  const physics = ship.physics;
  if (!physics.boostActive) return;
  if (!physics.boostReleasing) {
    physics.boostHold -= dt;
    physics.boostEffCap = physics.boostCap;
    if (physics.boostHold <= 0) { physics.boostReleasing = true; physics.boostReleaseT = ZONE_RELEASE; }
  } else {
    physics.boostReleaseT -= dt;
    const frac = Math.max(0, Math.min(1, physics.boostReleaseT / ZONE_RELEASE));
    physics.boostEffCap = physics.maxSpeed + (physics.boostCap - physics.maxSpeed) * frac;
    if (physics.boostReleaseT <= 0) { physics.boostActive = false; physics.boostEffCap = physics.maxSpeed; }
  }
}
function detectZoneTriggers(ship, sample, meshRegion) {
  const physics = ship.physics;
  for (const z of zones) {
    let inside = false;
    if (z.kind === 'path') {
      if (!meshRegion && sample && sample.pathObj === z.hostPath) {
        const proj = projectToSurface(sample, physics.groundPos.x, physics.groundPos.y, physics.groundPos.z);
        inside = TrackCore.zoneAlongContains(shipParamG(sample), z.gLo, z.gHi, z.gMax, z.closed) &&
          Math.abs(proj.s - z.lateral) <= z.halfWidth;
      }
    } else if (meshRegion === z.hostRegion) {
      const dx = physics.groundPos.x - z.x, dz = physics.groundPos.z - z.z;
      const cos = Math.cos(z.rot), sin = Math.sin(z.rot);
      const lx = dx * cos + dz * sin, lz = -dx * sin + dz * cos;
      inside = Math.abs(lx) <= z.halfLen && Math.abs(lz) <= z.halfWidth;
    }
    const wasInside = ship.zoneInside.get(z.id) || false;
    if (inside && !wasInside && z.effect === 'velocityChange') triggerBoost(ship, z);
    ship.zoneInside.set(z.id, inside);
  }
}

// ---------- Triggers / checkpoints ----------
// Vertical gate quads the ship passes THROUGH (see track-core.js). Never
// rendered in normal play; a debug view (J) draws each as a translucent quad
// with a direction arrow, coloured by armed state and flashing on fire. Dummy
// triggers only log; checkpoints drive lap progress and recovery.
let triggers = [];
let ships = [];
let playerShip = null;
const CHECKPOINT_FLASH_MS = 500;

function createRaceState(track, now) {
  const checkpoints = (track.triggers || []).filter(tr => tr.type === 'checkpoint');
  return {
    laps: 0, hit: new Set(),
    intermediateIds: checkpoints.filter(tr => tr.role !== 'finish').map(tr => tr.id),
    finishId: (checkpoints.find(tr => tr.role === 'finish') || {}).id || null,
    totalStartedAt: now, lapStartedAt: now, flashUntil: 0
  };
}

function rebuildCheckpointLights() {
  const row = document.getElementById('checkpointLights');
  if (!row || !playerShip) return;
  const race = playerShip.race;
  row.replaceChildren();
  for (const id of race.intermediateIds.concat(race.finishId ? [race.finishId] : [])) {
    const light = document.createElement('span');
    light.className = 'checkpointLight'; light.dataset.checkpointId = id;
    row.appendChild(light);
  }
}

function formatRaceTime(ms) {
  ms = Math.max(0, Math.floor(ms));
  const hours = Math.floor(ms / 3600000); ms %= 3600000;
  const minutes = Math.floor(ms / 60000); ms %= 60000;
  const seconds = Math.floor(ms / 1000), millis = ms % 1000;
  const mm = String(minutes).padStart(2, '0'), ss = String(seconds).padStart(2, '0'), mmm = String(millis).padStart(3, '0');
  return hours ? `${hours}:${mm}:${ss}.${mmm}` : `${mm}:${ss}.${mmm}`;
}

function updateRaceHud(now = performance.now()) {
  if (!playerShip) return;
  const race = playerShip.race;
  const flashing = now < race.flashUntil;
  const lapCount = document.getElementById('lapCount');
  const lapTime = document.getElementById('lapTime');
  const totalTime = document.getElementById('totalTime');
  if (lapCount) lapCount.textContent = race.laps;
  if (lapTime) lapTime.textContent = formatRaceTime(now - race.lapStartedAt);
  if (totalTime) totalTime.textContent = formatRaceTime(now - race.totalStartedAt);
  document.querySelectorAll('#checkpointLights .checkpointLight').forEach(light => {
    light.classList.toggle('hit', flashing || race.hit.has(light.dataset.checkpointId));
  });
}
let showTriggers = false;
const TRIGGER_ARMED_COLOR = 0x33dd66;      // green: ready to fire
const TRIGGER_DISARMED_COLOR = 0xdd3333;   // red: fired, waiting to re-arm
const TRIGGER_FLASH_TIME = 0.4;            // seconds a fire flash lasts
const TRIGGER_REARM_MARGIN = 3;            // units past the plane that counts as "clear"
const _trigColBase = new THREE.Color();
const _trigColFlash = new THREE.Color(0xffffff);

function disposeTriggerDebug(tr) {
  if (!tr.group) return;
  scene.remove(tr.group);
  tr.group.traverse(o => { if (o.geometry) o.geometry.dispose(); if (o.material) o.material.dispose(); });
  tr.group = null;
}

// Build the debug quad + direction arrow(s) for one compiled trigger, hidden
// unless the J debug view is on.
function buildTriggerDebugMesh(rec, direction) {
  const c = rec.center, r = rec.right, u = rec.up, f = rec.fwd, hw = rec.halfWidth, h = rec.height;
  const corner = (sr, su) => [c.x + r.x * sr * hw + u.x * su * h, c.y + r.y * sr * hw + u.y * su * h, c.z + r.z * sr * hw + u.z * su * h];
  const c0 = corner(-1, 0), c1 = corner(1, 0), c2 = corner(1, 1), c3 = corner(-1, 1);
  const pos = [];
  const tri = (a, b, cc) => pos.push(a[0], a[1], a[2], b[0], b[1], b[2], cc[0], cc[1], cc[2]);
  tri(c0, c1, c2); tri(c0, c2, c3);
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.computeVertexNormals();
  const quadMat = new THREE.MeshBasicMaterial({ color: TRIGGER_ARMED_COLOR, side: THREE.DoubleSide, transparent: true, opacity: 0.3, depthWrite: false });
  const quad = new THREE.Mesh(g, quadMat); quad.renderOrder = 6;

  const mid = [c.x + u.x * h * 0.5, c.y + u.y * h * 0.5, c.z + u.z * h * 0.5];
  const aLen = Math.max(3, hw * 0.5), barb = aLen * 0.35;
  const arrowPos = [];
  const addArrow = (sgn) => {
    const tip = [mid[0] + f.x * aLen * sgn, mid[1] + f.y * aLen * sgn, mid[2] + f.z * aLen * sgn];
    arrowPos.push(mid[0], mid[1], mid[2], tip[0], tip[1], tip[2]);
    for (const side of [1, -1]) {
      arrowPos.push(tip[0], tip[1], tip[2],
        tip[0] - f.x * barb * sgn + r.x * barb * side, tip[1] - f.y * barb * sgn + r.y * barb * side, tip[2] - f.z * barb * sgn + r.z * barb * side);
    }
  };
  if (direction === 'both' || direction === 'forward') addArrow(1);
  if (direction === 'both' || direction === 'backward') addArrow(-1);
  const ag = new THREE.BufferGeometry();
  ag.setAttribute('position', new THREE.Float32BufferAttribute(arrowPos, 3));
  const arrowMat = new THREE.LineBasicMaterial({ color: TRIGGER_ARMED_COLOR, transparent: true, depthTest: false });
  const arrow = new THREE.LineSegments(ag, arrowMat); arrow.renderOrder = 7;

  const group = new THREE.Group();
  group.add(quad); group.add(arrow);
  group.visible = showTriggers;
  scene.add(group);
  rec.group = group; rec.quadMat = quadMat; rec.arrowMat = arrowMat;
}

// (Re)build the compiled trigger records + debug meshes. Path triggers get their
// gate frame from the shared TrackCore.triggerPathFrame; mesh triggers are a
// flat gate at the region's elevation whose normal is set by `rotation`.
function buildTriggers(track, bakedPaths) {
  for (const tr of triggers) disposeTriggerDebug(tr);
  triggers = [];
  for (const trig of track.triggers || []) {
    const host = trig.host || {};
    let frame;
    if (host.kind === 'mesh') {
      const region = meshRegions.find(r => r.compiled && r.compiled.id === host.meshId);
      if (!region) continue;
      const rot = (trig.rotation || 0) * Math.PI / 180, cos = Math.cos(rot), sin = Math.sin(rot);
      frame = { center: { x: host.x, y: region.elevation, z: host.z }, fwd: { x: sin, y: 0, z: cos }, right: { x: cos, y: 0, z: -sin }, up: { x: 0, y: 1, z: 0 } };
    } else {
      const bp = bakedPaths.find(b => b.id === host.pathId);
      if (!bp) continue;
      frame = TrackCore.triggerPathFrame(bp.controlPoints, bp.closed, bp.rollPoints, bp.widthPoints, bp.crossSectionPoints, trig);
    }
    const rec = {
      id: trig.id, type: trig.type, role: trig.role, direction: trig.direction,
      center: new THREE.Vector3(frame.center.x, frame.center.y, frame.center.z),
      right: new THREE.Vector3(frame.right.x, frame.right.y, frame.right.z),
      up: new THREE.Vector3(frame.up.x, frame.up.y, frame.up.z),
      fwd: new THREE.Vector3(frame.fwd.x, frame.fwd.y, frame.fwd.z),
      halfWidth: Math.max(0.25, (trig.width || 0) / 2), height: Math.max(0.25, trig.height || 0)
    };
    buildTriggerDebugMesh(rec, trig.direction);
    triggers.push(rec);
  }
}

function fireTrigger(ship, rec, dir) {
  const state = ship.triggerStates.get(rec.id);
  if (ship === playerShip) state.flash = TRIGGER_FLASH_TIME;
  console.log(`[trigger][${ship.id}] ${rec.id} fired (${dir})`);
  if (rec.type !== 'checkpoint') return;

  const checkpoint = ship.lastCheckpoint;
  checkpoint.valid = true;
  checkpoint.triggerId = rec.id;
  checkpoint.pos.copy(rec.center);
  checkpoint.up.copy(rec.up);
  checkpoint.forward.copy(rec.fwd).multiplyScalar(dir === 'backward' ? -1 : 1);

  const race = ship.race;
  if (rec.role !== 'finish') {
    race.hit.add(rec.id);
    return;
  }
  if (!race.intermediateIds.every(id => race.hit.has(id))) return;

  const now = performance.now();
  race.laps++;
  race.hit.clear();
  race.lapStartedAt = now;
  race.flashUntil = now + CHECKPOINT_FLASH_MS;
}

// Swept crossing of the ship segment p0->p1 against every trigger gate. Fires
// once on an allowed crossing while armed, then disarms until the ship is clear
// of the gate (off its width x height footprint, or past the plane by the
// re-arm margin). Runs airborne too -- a gate can be crossed in the air.
function detectTriggers(ship, p0, p1) {
  for (const tr of triggers) {
    const state = ship.triggerStates.get(tr.id);
    const c = tr.center;
    const d0 = (p0.x - c.x) * tr.fwd.x + (p0.y - c.y) * tr.fwd.y + (p0.z - c.z) * tr.fwd.z;
    const d1 = (p1.x - c.x) * tr.fwd.x + (p1.y - c.y) * tr.fwd.y + (p1.z - c.z) * tr.fwd.z;
    const rr = (p1.x - c.x) * tr.right.x + (p1.y - c.y) * tr.right.y + (p1.z - c.z) * tr.right.z;
    const uu = (p1.x - c.x) * tr.up.x + (p1.y - c.y) * tr.up.y + (p1.z - c.z) * tr.up.z;
    if (!state.armed && (Math.abs(rr) > tr.halfWidth || uu < 0 || uu > tr.height || Math.abs(d1) > TRIGGER_REARM_MARGIN)) state.armed = true;
    if (state.armed && d0 !== d1 && ((d0 <= 0 && d1 > 0) || (d0 >= 0 && d1 < 0))) {
      const t = d0 / (d0 - d1);
      const xr = (p0.x + (p1.x - p0.x) * t - c.x), yr = (p0.y + (p1.y - p0.y) * t - c.y), zr = (p0.z + (p1.z - p0.z) * t - c.z);
      const lr = xr * tr.right.x + yr * tr.right.y + zr * tr.right.z;
      const lu = xr * tr.up.x + yr * tr.up.y + zr * tr.up.z;
      if (Math.abs(lr) <= tr.halfWidth && lu >= 0 && lu <= tr.height) {
        const dir = d1 > d0 ? 'forward' : 'backward';
        if (tr.direction === 'both' || tr.direction === dir) { fireTrigger(ship, tr, dir); state.armed = false; }
      }
    }
  }
}

function resetTriggers(ship, disarmedId = null) {
  ship.prevTriggerPos.copy(ship.physics.groundPos);
  for (const tr of triggers) ship.triggerStates.set(tr.id, { armed: tr.id !== disarmedId, flash: 0 });
}

// The shared debug mesh reflects only the player's independent trigger state.
function updateTriggerDebug(dt) {
  if (!playerShip) return;
  for (const tr of triggers) {
    const state = playerShip.triggerStates.get(tr.id) || { armed: true, flash: 0 };
    if (state.flash > 0) state.flash = Math.max(0, state.flash - dt);
    if (!showTriggers || !tr.quadMat) continue;
    _trigColBase.setHex(state.armed ? TRIGGER_ARMED_COLOR : TRIGGER_DISARMED_COLOR);
    const k = state.flash / TRIGGER_FLASH_TIME;
    _trigColBase.lerp(_trigColFlash, k);
    tr.quadMat.color.copy(_trigColBase);
    tr.arrowMat.color.copy(_trigColBase);
    tr.quadMat.opacity = 0.3 + 0.55 * k;
  }
}

/* The mesh region under X/Z whose surface sits nearest the ship's current Y,
 * or null. Surfaces above the ship are strongly penalised so a flyover never
 * steals the ship driving underneath it. */
function meshRegionAt(x, z, shipY) {
  let best = null;
  for (const region of meshRegions) {
    if (!TrackMesh.withinBounds(region.compiled, x, z)) continue;
    if (!TrackMesh.containsWorldPoint(region.compiled, x, z)) continue;
    const above = region.elevation - shipY;
    const score = Math.abs(above) + (above > SURFACE_SNAP_UP ? 1e6 : 0);
    if (!best || score < best.score) best = { region, score };
  }
  return best;
}

// A surface more than this far ABOVE the ship is treated as overhead geometry
// rather than something to snap up onto.
const SURFACE_SNAP_UP = 3;
// How far below the lowest drivable surface counts as "fallen off for good".
const RESPAWN_FALL_DEPTH = 100;

// Project a FULL 3D position onto a corridor sample's cross-section, returning
// the lateral offset `s` and the drivable range bounded by the rendered
// physical walls (the trimmed edge offsets, inset by the ship margin).
//
// `edgeRight` is a unit vector, so the lateral offset is simply the 3D dot
// product of the displacement with it -- no division, and no degeneracy at any
// roll. (This replaces an earlier X/Z-only version that divided by cos^2(roll),
// which blew up as the road verticalized near 90deg roll: with only X/Z known,
// recovering `s` meant inverting a map that becomes singular exactly there.
// Feeding the real 3D point -- the ship's Y is tracked now that velocity lives
// in the surface tangent plane -- makes the reconstruction exact at every roll.)
// The caller MUST pass the ship's actual Y; passing the centerline's Y would
// reintroduce the old degeneracy through the back door.
function projectToSurface(sample, px, py, pz) {
  const er = sample.edgeRight;
  const s = (px - sample.pos.x) * er.x + (py - sample.pos.y) * er.y + (pz - sample.pos.z) * er.z;
  let loS = sample.sLeft + TrackCore.COLLISION_WALL_MARGIN;    // inner limit of the left edge (negative side)
  let hiS = sample.sRight - TrackCore.COLLISION_WALL_MARGIN;   // inner limit of the right edge (positive side)
  if (loS > hiS) { const m = (loS + hiS) / 2; loS = m; hiS = m; } // corridor pinched to a point
  return { er, s, loS, hiS };
}

// How far along the tangent a point may sit from its sampled centerline frame
// and still count as "over the ribbon". Generous relative to the spacing
// between baked samples, but far smaller than any real gap.
const CORRIDOR_ALONG_TOL = 8;

/* Is X/Z genuinely over a corridor sample's drivable surface?
 *
 * The lateral bounds alone are NOT containment. `s` measures only the offset
 * along edgeRight, so a point far beyond the END of a segment projects onto
 * that segment's clamped endpoint and can report a small, perfectly in-range
 * `s` while actually being hundreds of units away down the track's axis. That
 * matters now that mesh regions let the ship be somewhere with no ribbon
 * anywhere near it: without the along-tangent check, leaving a mesh ledge
 * teleports the ship onto whichever distant ribbon happened to be nearest. */
function corridorContains(sample, x, y, z, proj) {
  if (sample.offEnd || proj.s < proj.loS || proj.s > proj.hiS) return false;
  // 3D along-tangent, so this stays meaningful where the centerline itself goes
  // vertical (a loop): an X/Z-only dot would collapse to ~0 there and never
  // reject a point off the segment's end.
  const along = (x - sample.pos.x) * sample.tangent.x + (y - sample.pos.y) * sample.tangent.y + (z - sample.pos.z) * sample.tangent.z;
  return Math.abs(along) <= CORRIDOR_ALONG_TOL;
}

const _meshSurfacePos = new THREE.Vector3();

/* Decide which surface owns a horizontal position: the mesh region under it, or
 * the spline corridor. Containment in a region is necessary but not sufficient
 * -- when the ship is also inside a corridor, the surface nearest it in Y wins,
 * which is what makes flyovers and stacked plazas behave. Returns the winning
 * region, or null to mean "the corridor owns this". */
function surfaceOwnerAt(x, z, shipY, corridorSample) {
  const meshHit = meshRegionAt(x, z, shipY);
  if (!meshHit) return null;
  const proj = projectToSurface(corridorSample, x, shipY, z);
  if (!corridorContains(corridorSample, x, shipY, z, proj)) return meshHit.region;
  const corridorY = curvedSurfaceFrame(corridorSample, proj.s).pos.y;
  return Math.abs(meshHit.region.elevation - shipY) <= Math.abs(corridorY - shipY) ? meshHit.region : null;
}

// ---------- Per-track ship handling ----------
// maxSpeed/accel/turnRate and the ship mass come from the track's `handling`
// section (TrackCore fills defaults when it is absent, so this always gets a
// complete object). brakeDecel/friction/maxReverse/grip are deliberately NOT
// touched -- they stay the fixed engine constants set on the physics object.
const HANDLING_BASE_WEIGHT = 1000;   // kg; the weight the collision reaction is tuned around
function applyHandling(track, physics) {
  const h = TrackCore.normalizeHandling(track && track.handling);
  physics.maxSpeed = h.maxSpeed;
  physics.accel = h.accel;
  physics.turnRate = h.turnSpeed * Math.PI / 180;
  physics.weight = h.weight;
}
// Weight-scaled wall bounciness. m = weight / neutral: a heavy ship (m > 1)
// bounces less, a light one (m < 1) more, capped so it never approaches a
// perfect-elastic ping. At the neutral weight this is exactly the base 0.4.
function weightRestitution(physics) {
  const m = (physics.weight || HANDLING_BASE_WEIGHT) / HANDLING_BASE_WEIGHT;
  return Math.max(0, Math.min(0.9, physics.wallRestitution / m));
}
// Fraction of speed kept after a wall impact -- heavier ships shrug it off and
// keep more, lighter ones scrub more. At the neutral weight this is the old 0.98.
function weightSpeedRetain(physics) {
  const m = (physics.weight || HANDLING_BASE_WEIGHT) / HANDLING_BASE_WEIGHT;
  return Math.max(0.85, Math.min(0.999, 1 - 0.02 / m));
}
// Hover-kick/shake proportional to impact MOMENTUM (weight x normal impact
// speed), so a heavier or faster hit jolts the ship more. Reuses the landing
// spring so it eases out the same way a hard landing does.
function addImpactJolt(physics, normalImpactSpeed) {
  const m = (physics.weight || HANDLING_BASE_WEIGHT) / HANDLING_BASE_WEIGHT;
  const momentum = m * Math.max(0, normalImpactSpeed);
  physics.landingBounce += Math.min(2.0, momentum * 0.012);
  physics.landingBounceVel += Math.min(10, momentum * 0.05);
}

// Swept rail collision lives in track-mesh.js (pure geometry, shared and
// testable); this just binds it to the ship's collision margin and the ship's
// current weight-derived bounciness.
const slideAlongRails = (physics, region, from, to, velocity) =>
  TrackMesh.slideAlongRails(region.compiled, from, to, velocity, TrackCore.COLLISION_WALL_MARGIN, weightRestitution(physics));

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
  offEnd: false,
  // Which compiled path this sample landed on and the segment (a->b, param segT)
  // it was projected onto -- carried so zone detection can recover the ship's
  // evaluator parameter g on that path (see shipParamG).
  pathObj: null, a: 0, b: 1, segT: 0
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

// How far past a centerline segment's own extent a point may sit and still
// count as being "over" that segment. A perpendicular projection lands exactly
// on the segment, so this only ever admits float noise at a shared sample
// point; anything beyond belongs to a neighbouring segment, or to no segment at
// all (which is what running off an open end means).
const SEGMENT_ALONG_TOL = 0.5;

// Selection is fully 3D: the ship's position projects onto each centerline
// segment in 3D and the nearest by 3D distance wins. This is what makes a
// vertical loop drivable -- where the centerline itself goes straight up, the
// ascending and descending sides share an X/Z column, so an X/Z-footprint search
// could not tell top-of-loop from bottom-of-loop. It also sharpens overlapping
// flyovers: the vertically-nearest ribbon is picked, not merely one that shares
// the X/Z column. `y` is the ship's real height, required by both the 3D
// distance and the 3D lateral-membership test below.
function sampleTrack(x, y, z) {
  let fallback = { path: paths[0], a: 0, b: 1, t: 0, d: Infinity };
  let bestUnder = null;
  for (const path of paths) {
    const cl = path.centerline, M = cl.length;
    const segCount = path.closed ? M : M - 1;
    for (let i = 0; i < segCount; i++) {
      const j = path.closed ? (i + 1) % M : i + 1;
      const a = cl[i], b = cl[j];
      const sx = b.pos.x - a.pos.x, sy = b.pos.y - a.pos.y, sz = b.pos.z - a.pos.z;
      const segLen2 = sx * sx + sy * sy + sz * sz;
      const t = segLen2 > 0
        ? THREE.MathUtils.clamp(((x - a.pos.x) * sx + (y - a.pos.y) * sy + (z - a.pos.z) * sz) / segLen2, 0, 1)
        : 0;
      const px = a.pos.x + sx * t, py = a.pos.y + sy * t, pz = a.pos.z + sz * t;
      const dx = x - px, dy = y - py, dz = z - pz;
      const d = dx * dx + dy * dy + dz * dz;
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
      // 3D lateral offset onto the unit edgeRight -- exact at any roll, matching
      // projectToSurface. On flat ground ery = 0, so this reduces to the old
      // X/Z dot; on a bank the ship's real Y is what keeps `s` honest.
      const lateral = (x - cx) * erx + (y - cy) * ery + (z - cz) * erz;
      let loS = (a.sLeft + (b.sLeft - a.sLeft) * t) + TrackCore.COLLISION_WALL_MARGIN;
      let hiS = (a.sRight + (b.sRight - a.sRight) * t) - TrackCore.COLLISION_WALL_MARGIN;
      if (loS > hiS) { const m = (loS + hiS) / 2; loS = m; hiS = m; }
      let wouldOffEnd = false;
      if (!path.closed) {
        if (i === 0 && t <= 1e-4) {
          const e = cl[0];
          wouldOffEnd = !connectedEndpointIds.has(path.endpointIds.start) &&
            ((x - e.pos.x) * e.tangent.x + (y - e.pos.y) * e.tangent.y + (z - e.pos.z) * e.tangent.z) < 0;
        } else if (j === M - 1 && t >= 1 - 1e-4) {
          const e = cl[M - 1];
          wouldOffEnd = !connectedEndpointIds.has(path.endpointIds.end) &&
            ((x - e.pos.x) * e.tangent.x + (y - e.pos.y) * e.tangent.y + (z - e.pos.z) * e.tangent.z) > 0;
        }
      }
      // Being within the lateral bounds is not the same as being OVER this
      // segment. `t` is clamped to [0,1], so a point beyond a segment's end
      // projects onto that end and reports the lateral offset of the endpoint
      // -- which on a straight run down the middle of a road is 0 for every
      // segment on the path, however far away. Left unchecked, a point off the
      // open END of a curve is claimed by some far-back segment, so `best` is
      // never the terminal segment and offEnd below can never fire: the ship
      // gets reprojected backwards instead of launching off the end. Require
      // the projection to be a genuine perpendicular foot, which it is exactly
      // when `t` did not clamp.
      const alongSeg = segLen2 > 0 ? ((x - px) * sx + (y - py) * sy + (z - pz) * sz) / Math.sqrt(segLen2) : 0;
      const overSegment = Math.abs(alongSeg) <= SEGMENT_ALONG_TOL;
      if (overSegment && !wouldOffEnd && lateral >= loS && lateral <= hiS && (!bestUnder || d < bestUnder.d)) bestUnder = { path, a: i, b: j, t, d };
    }
  }
  const best = bestUnder || fallback;
  const bestPath = best.path, bestA = best.a, bestB = best.b;
  const cl = bestPath.centerline;
  const a = cl[bestA], b = cl[bestB];
  const t = best.t;
  _sample.pathObj = bestPath; _sample.a = bestA; _sample.b = bestB; _sample.segT = t;

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
        ((x - e.pos.x) * e.tangent.x + (y - e.pos.y) * e.tangent.y + (z - e.pos.z) * e.tangent.z) < 0;
    } else if (bestB === M - 1 && t >= 1 - 1e-4) {
      const e = bestPath.centerline[M - 1];
      _sample.offEnd = !connectedEndpointIds.has(bestPath.endpointIds.end) &&
        ((x - e.pos.x) * e.tangent.x + (y - e.pos.y) * e.tangent.y + (z - e.pos.z) * e.tangent.z) > 0;
    }
  }
  return _sample;
}

// ---------- Ships / starting grid ----------
const bodyGeo = new THREE.BoxGeometry(2.4, 0.8, 4.0);
const noseGeo = new THREE.ConeGeometry(0.7, 1.6, 4);
const SHIP_HALF_WIDTH = 1.2;
const SHIP_COLORS = [0xd85f14, 0x3f8cff, 0x45c96b, 0xe5c642, 0xa66cff, 0xff5ca8, 0x38ced1, 0xf28b30];
function shipColor(index) {
  if (index < SHIP_COLORS.length) return SHIP_COLORS[index];
  return new THREE.Color().setHSL((index * 0.61803398875) % 1, 0.68, 0.55).getHex();
}

function makeShipGroup(color, player) {
  const group = new THREE.Group();
  const bodyColor = new THREE.Color(color);
  const body = new THREE.Mesh(bodyGeo, new THREE.MeshStandardMaterial({
    color: bodyColor, metalness: 0.35, roughness: 0.4,
    emissive: bodyColor.clone().multiplyScalar(0.18), emissiveIntensity: 0.15, flatShading: true
  }));
  body.position.y = 0.3; group.add(body);
  const noseColor = player ? new THREE.Color(0x00a8cc) : bodyColor.clone().offsetHSL(0, -0.05, 0.18);
  const nose = new THREE.Mesh(noseGeo, new THREE.MeshStandardMaterial({
    color: noseColor, emissive: noseColor.clone().multiplyScalar(0.28), emissiveIntensity: 0.25, flatShading: true
  }));
  nose.rotation.x = Math.PI / 2; nose.rotation.y = Math.PI / 4;
  nose.position.set(0, 0.3, 1.25); group.add(nose);
  scene.add(group);
  return group;
}

function disposeShips() {
  for (const ship of ships) {
    scene.remove(ship.group);
    ship.group.traverse(o => { if (o.material) o.material.dispose(); });
  }
  ships = []; playerShip = null;
}

function interpolatedGridFrame(path, startIndex, distanceBehind) {
  const cl = path.centerline, count = cl.length;
  const step = trackStart.reverse ? 1 : -1;
  let at = startIndex, remaining = distanceBehind, next = at, frac = 0;
  for (let n = 0; n < count && remaining > 1e-9; n++) {
    const candidate = path.closed ? (at + step + count) % count : at + step;
    if (candidate < 0 || candidate >= count) break;
    const len = cl[at].pos.distanceTo(cl[candidate].pos);
    if (remaining <= len && len > 0) { next = candidate; frac = remaining / len; remaining = 0; break; }
    remaining -= len; at = candidate; next = at; frac = 0;
  }
  const a = cl[at], b = cl[next];
  const lerpVec = key => a[key].clone().lerp(b[key], frac).normalize();
  return {
    pos: a.pos.clone().lerp(b.pos, frac), tangent: lerpVec('tangent'), edgeRight: lerpVec('edgeRight'), normal: lerpVec('normal'),
    sLeft: a.sLeft + (b.sLeft - a.sLeft) * frac, sRight: a.sRight + (b.sRight - a.sRight) * frac,
    crossSectionCurvature: a.crossSectionCurvature + (b.crossSectionCurvature - a.crossSectionCurvature) * frac,
    crossSectionTightness: a.crossSectionTightness + (b.crossSectionTightness - a.crossSectionTightness) * frac
  };
}

function startingGridPoses(count) {
  const pathIndex = THREE.MathUtils.clamp(trackStart.path, 0, paths.length - 1);
  const path = paths[pathIndex];
  const pointIndex = THREE.MathUtils.clamp(trackStart.point, 0, path.anchors.length - 1);
  const anchor = path.anchors[pointIndex];
  let startIndex = 0, bestD = Infinity;
  for (let i = 0; i < path.centerline.length; i++) {
    const d = path.centerline[i].pos.distanceToSquared(anchor);
    if (d < bestD) { bestD = d; startIndex = i; }
  }
  return Array.from({ length: count }, (_, i) => {
    const rough = gridSlot(i);
    const frame = interpolatedGridFrame(path, startIndex, rough.behind);
    const lo = frame.sLeft + TrackCore.COLLISION_WALL_MARGIN + SHIP_HALF_WIDTH;
    const hi = frame.sRight - TrackCore.COLLISION_WALL_MARGIN - SHIP_HALF_WIDTH;
    const slot = gridSlot(i, { lateralLimit: Math.max(0, Math.min(-lo, hi)) });
    let surface = curvedSurfaceFrame(frame, slot.lateral);
    let canonical = frame;
    // Settle the analytically-placed slot onto the exact same sampled surface
    // the parked physics branch uses, so an idle ship does not creep while the
    // two representations converge over its first frames.
    for (let n = 0; n < 3; n++) {
      canonical = sampleTrack(surface.pos.x, surface.pos.y, surface.pos.z);
      const proj = projectToSurface(canonical, surface.pos.x, surface.pos.y, surface.pos.z);
      surface = curvedSurfaceFrame(canonical, THREE.MathUtils.clamp(proj.s, proj.loS, proj.hiS));
    }
    const forward = canonical.tangent.clone().multiplyScalar(trackStart.reverse ? -1 : 1).normalize();
    tangentize(forward, surface.normal, forward);
    return { pos: surface.pos, up: surface.normal, forward, slot };
  });
}

function createShip(index, track, now) {
  const isPlayer = index === 0;
  const color = shipColor(index);
  return {
    id: isPlayer ? 'player' : `ai-${index}`,
    controllerKind: isPlayer ? 'player' : 'ai',
    controller: isPlayer ? playerController : idleController,
    color,
    group: makeShipGroup(color, isPlayer),
    physics: createPhysicsState(),
    zoneInside: new Map(), triggerStates: new Map(), prevTriggerPos: new THREE.Vector3(),
    race: createRaceState(track, now),
    lastCheckpoint: { valid: false, triggerId: null, pos: new THREE.Vector3(), forward: new THREE.Vector3(), up: new THREE.Vector3(0, 1, 0) },
    startPose: null
  };
}

function placeShipAtPose(ship, pose, disarmedId = null) {
  const physics = ship.physics;
  physics.groundPos.copy(pose.pos);
  physics.visualGroundPos.copy(pose.pos);
  physics.forward.copy(pose.forward);
  physics.moveDir.copy(pose.forward);
  physics.up.copy(pose.up); physics.visualUp.copy(pose.up);
  physics.right.crossVectors(pose.up, pose.forward).normalize();
  physics.heading = Math.atan2(pose.forward.x, pose.forward.z);
  physics.speed = 0; physics.airborne = false; physics.verticalVel = 0;
  physics.visualBank = 0; physics.visualPitch = 0;
  physics.landingBounce = 0; physics.landingBounceVel = 0;
  ship.group.position.copy(pose.pos).addScaledVector(pose.up, 1);
  clearBoost(ship);
  resetTriggers(ship, disarmedId);
}

function respawn(ship = playerShip) {
  if (typeof ship === 'string') ship = ships.find(s => s.id === ship);
  if (!ship) return;
  const checkpoint = ship.lastCheckpoint;
  const pose = checkpoint.valid ? checkpoint : ship.startPose;
  placeShipAtPose(ship, pose, checkpoint.valid ? checkpoint.triggerId : null);
}

function buildRoster(track, count = DEFAULT_SHIP_COUNT) {
  disposeShips();
  const now = performance.now();
  ships = Array.from({ length: count }, (_, i) => createShip(i, track, now));
  playerShip = ships[0] || null;
  const poses = startingGridPoses(ships.length);
  ships.forEach((ship, i) => {
    applyHandling(track, ship.physics);
    ship.startPose = poses[i];
    placeShipAtPose(ship, ship.startPose);
  });
  rebuildCheckpointLights();
}

// ---------- Input ----------
const keys = {};
let playerRespawnRequested = false;
window.addEventListener('keydown', (e) => {
  keys[e.code] = true;
  // `e.code` is a PHYSICAL key position (right for WASD, the deliberate
  // gaming convention), but [ and ] are punctuation: on many non-US layouts
  // the character is typed via a different physical key (often an AltGr
  // combo), so e.code never matches BracketLeft/BracketRight there and the
  // zoom keys would silently never fire. Track the literal character too, and
  // read zoom off that instead.
  keys[e.key] = true;
  // G is a pure rendering toggle: rails are solid collision either way.
  if (e.code === 'KeyG' && !e.repeat) {
    showGuardRails = !showGuardRails;
    for (const p of paths) {
      if (p.railL) p.railL.visible = showGuardRails;
      if (p.railR) p.railR.visible = showGuardRails;
    }
    for (const region of meshRegions) if (region.railMesh) region.railMesh.visible = showGuardRails;
  }
  if (e.code === 'KeyH' && !e.repeat) {
    showWireframe = !showWireframe;
    for (const p of paths) if (p.wireLine) p.wireLine.visible = showWireframe;
    // Regions have no separate wire overlay; flip their surface material instead,
    // which also exposes the imported mesh's triangulation.
    for (const region of meshRegions) if (region.surface) region.surface.material.wireframe = showWireframe;
  }
  if (e.code === 'KeyR' && !e.repeat) playerRespawnRequested = true;
  // J: toggle the trigger debug view (off by default; triggers are otherwise
  // never rendered).
  if (e.code === 'KeyJ' && !e.repeat) {
    showTriggers = !showTriggers;
    for (const tr of triggers) if (tr.group) tr.group.visible = showTriggers;
  }
});
window.addEventListener('keyup', (e) => { keys[e.code] = false; keys[e.key] = false; });

function isDown(...codes) { return codes.some(c => keys[c]); }

const playerController = {
  kind: 'player',
  intent() {
    const out = {
      throttle: isDown('KeyW', 'ArrowUp') ? 1 : 0,
      brake: isDown('KeyS', 'ArrowDown') ? 1 : 0,
      steer: (isDown('KeyD', 'ArrowRight') ? -1 : 0) + (isDown('KeyA', 'ArrowLeft') ? 1 : 0),
      respawn: playerRespawnRequested
    };
    playerRespawnRequested = false;
    return out;
  }
};
const IDLE_INTENT = Object.freeze({ throttle: 0, brake: 0, steer: 0, respawn: false });
const idleController = { kind: 'ai', intent: () => IDLE_INTENT };

// ---------- Wipeout-style hover physics ----------
function createPhysicsState() { return {
  // Facing/motion are TANGENT-PLANE unit vectors, not world-yaw scalars, so the
  // ship can be oriented and move on an arbitrarily-rolled (even vertical or
  // inverted) surface. `forward`/`right`/`up` form the ship's basis; `moveDir`
  // is where it's actually travelling (lags `forward` under hard turns -> the
  // wipeout drift). `heading` is kept only as a derived world azimuth of
  // `forward` (atan2 of its X/Z), which is all the top-down minimap needs.
  heading: 0,        // derived world-yaw azimuth of `forward` (for the minimap)
  speed: 0,                // signed scalar speed (units/sec)
  // 1 world unit = 1 metre, so maxSpeed 140 = 140 m/s (504 km/h). The
  // longitudinal rates below are the old 102-tuned values scaled x1.373 (=
  // 140/102) so the pedal FEEL is unchanged -- same ~2.0s to top speed, same
  // braking punch, same coast-down -- just at the higher ceiling. turnRate/grip
  // are angular rates (scale-invariant) and stay put, which keeps both the big
  // new tracks and small legacy tracks driveable.
  maxSpeed: 140,
  maxReverse: -33,   // -24 * 1.373
  accel: 71,         // 52 * 1.373
  brakeDecel: 115,   // 84 * 1.373
  friction: 55,      // 40 * 1.373 -- decel when neither throttle nor brake held
  turnRate: 2.4,           // rad/sec at low speed
  grip: 3.2,               // how fast velocity direction chases heading (lower = more slide)
  wallRestitution: 0.4,    // BASE guard-rail bounce at the neutral weight; scaled per weight (weightRestitution)
  weight: 1000,            // ship mass (kg); 1000 = neutral. Heavier bounces less, lighter more
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
  // Actual travel direction, a unit vector in the surface tangent plane. Chases
  // `forward` (see grip below); `velocity = moveDir * speed` is a full 3D vector
  // that gains a vertical component on a banked surface, which is what lets the
  // ship climb toward a rolled edge instead of only ever sliding horizontally.
  moveDir: new THREE.Vector3(0, 0, 1),
  airborne: false,
  verticalVel: 0,
  gravity: 60,
  landingBounce: 0,
  landingBounceVel: 0,
  // Boost-zone state. While active, the speed clamp uses boostEffCap (>= maxSpeed)
  // instead of maxSpeed: it holds at boostCap for boostHold seconds, then eases
  // back to maxSpeed over BOOST_RELEASE (the "smooth release"). boostActive stays
  // true through the release, which is this ship's lock that makes a boost
  // fire once on enter and ignore further pads until it ends.
  boostActive: false,
  boostReleasing: false,
  boostHold: 0,
  boostReleaseT: 0,
  boostCap: 0,
  boostEffCap: 0
}; }
// Scratch vectors reused each frame so the physics loop allocates nothing.
const _vel = new THREE.Vector3();
const _newPos = new THREE.Vector3();
const _wallN = new THREE.Vector3();
const _launchVel = new THREE.Vector3();

// --- surface-relative motion helpers (see "roll near 90deg" note below) ------
// The ship's facing and motion are 3D unit vectors that live in the TANGENT
// PLANE of the surface it's on, not world-yaw scalars. That's what lets it
// drive across and along a vertical (90deg-rolled) wall and through a loop:
// world-yaw can only ever describe an azimuth, which cannot point up/down a
// vertical surface, so a horizontal-velocity model degenerates exactly there.
const _tanTmp = new THREE.Vector3();
// Re-project unit-ish `v` into the plane tangent to unit normal `n` and
// renormalize, keeping a facing/motion vector on the surface as the surface
// tilts underneath it frame to frame. Falls back to `fallback` when `v` is
// (near) parallel to `n` -- e.g. a cross-track direction on a surface that has
// just gone vertical -- so it never returns a zero/NaN vector.
function tangentize(v, n, fallback) {
  _tanTmp.copy(v).addScaledVector(n, -v.dot(n));
  if (_tanTmp.lengthSq() < 1e-9) { return v.copy(fallback); }
  return v.copy(_tanTmp).normalize();
}
const _saCross = new THREE.Vector3();
// Signed angle (radians) rotating unit `a` onto unit `b` about unit `axis`,
// with a and b assumed ~perpendicular to axis. On flat ground (axis = +Y) this
// is exactly the yaw delta the old lerpAngle steering used.
function signedAngleAbout(a, b, axis) {
  const d = THREE.MathUtils.clamp(a.dot(b), -1, 1);
  const ang = Math.acos(d);
  _saCross.crossVectors(a, b);
  return _saCross.dot(axis) < 0 ? -ang : ang;
}
// Enter the ballistic state from a 3D launch velocity: its vertical component
// seeds `verticalVel` (so launching off a banked ramp or wall arcs correctly --
// the old code always zeroed this, which was only right for a level launch),
// and its horizontal part becomes the constant-in-air travel direction/speed.
function beginAirborne(ship, vel3D) {
  const physics = ship.physics;
  physics.airborne = true;
  physics.verticalVel = vel3D.y;
  const horiz = Math.hypot(vel3D.x, vel3D.z);
  physics.speed = horiz;
  if (horiz > 1e-6) physics.moveDir.set(vel3D.x / horiz, 0, vel3D.z / horiz);
  else tangentize(physics.moveDir, UP, physics.forward);   // launched straight up: keep a horizontal azimuth
}
// Re-contact a surface: drop the vertical velocity (as before) and re-flatten
// the horizontal travel/facing directions onto the landed surface's tangent
// plane so they immediately follow its bank. Horizontal speed is preserved,
// matching the old landing behaviour; on a flat surface (normal +Y) this is a
// no-op beyond clearing the airborne flags.
function landOnSurface(ship, normal) {
  const physics = ship.physics;
  physics.airborne = false;
  physics.verticalVel = 0;
  tangentize(physics.moveDir, normal, physics.forward);
  tangentize(physics.forward, normal, physics.moveDir);
}

// Largest integration step the physics is allowed to take. A long render frame
// is split into ceil(dt / MAX_PHYSICS_STEP) equal sub-steps, so the position
// advance and the wall/rail collision (both tested once per step) can never
// move a fast ship far enough to tunnel a wall or skip past a corridor edge in
// a single test. Small enough that even a slow (~20fps) frame still integrates
// in a handful of steps; the per-step work is cheap float math and one
// sampleTrack pass, negligible next to rendering.
const MAX_PHYSICS_STEP = 1 / 120;

// Advance the physics state by ONE integration sub-step, returning the surface
// normal + render position the once-per-frame visual pass should use, and
// whether a respawn fired (which aborts the rest of the frame, as the original
// early-return did). Everything here is integration/collision; the visual work
// (hover, orientation, camera basis, HUD) stays in updateShip below.
function stepPhysics(ship, dt, throttle, brake, steer) {
  const physics = ship.physics;
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
  physics.speed = THREE.MathUtils.clamp(physics.speed, physics.maxReverse, effectiveMaxSpeed(physics));

  // Clamped to [0,1] on purpose: during a boost speed exceeds maxSpeed, and the
  // steering/grip/lean terms below must not be driven past their tuned range
  // (a raw ratio > 1 would flip the turn-rate falloff negative). The HUD reads
  // the true speed separately.
  const speedRatio = Math.min(1, Math.abs(physics.speed) / physics.maxSpeed);

  // Sample the surface under the ship FIRST: its normal is the axis we steer and
  // drift about, so turning is defined in the driver's frame at any roll.
  let c = sampleTrack(physics.groundPos.x, physics.groundPos.y, physics.groundPos.z);
  let surfaceNormal = c.normal;
  let surfaceRenderPos = physics.groundPos;

  // Which surface owns the ship right now -- a flat mesh region, or the spline
  // corridor? Whichever sits nearer in Y wins, so a mesh bridge passing over a
  // ribbon never hijacks the ship driving underneath it.
  const meshRegion = surfaceOwnerAt(physics.groundPos.x, physics.groundPos.z, physics.groundPos.y, c);

  // The plane the ship turns and travels in: the corridor surface normal on a
  // ribbon, world +Y on a flat mesh region, and world +Y while airborne
  // (gravity's frame -- air steering is world-horizontal). Rotating `forward`
  // about THIS axis is the driver-correct turn on any roll, including past
  // vertical and inverted -- which is why the old world-yaw `upSign` flip is
  // gone: on an inverted surface the normal points down, so a rotation about it
  // already flips the turn the way the (upside-down) driver perceives it.
  const steerAxis = (physics.airborne || meshRegion) ? UP : surfaceNormal;

  // Steering: turn rate eases off slightly at top speed, and reverses sign in
  // reverse. Rotate the facing vector about the surface normal, then re-flatten
  // it onto the (possibly newly-tilted) tangent plane.
  const effectiveTurn = physics.turnRate * (1 - 0.35 * speedRatio) * Math.sign(physics.speed || 1);
  physics.forward.applyAxisAngle(steerAxis, steer * effectiveTurn * dt);
  tangentize(physics.forward, steerAxis, physics.forward);

  // Hover "grip": travel direction chases facing, lagging under hard turns at
  // speed -> the classic wipeout drift. Rotate moveDir a fraction of the way
  // toward forward about the same axis (on flat ground this is exactly the old
  // lerpAngle(velocityAngle, heading, ...)), then re-flatten onto the tangent.
  const gripThisFrame = physics.grip * (0.5 + 0.5 * (1 - Math.min(Math.abs(steer) * speedRatio, 1)));
  const toForward = signedAngleAbout(physics.moveDir, physics.forward, steerAxis);
  physics.moveDir.applyAxisAngle(steerAxis, toForward * Math.min(gripThisFrame * dt, 1));
  tangentize(physics.moveDir, steerAxis, physics.forward);

  // Velocity is a full 3D vector along the tangent travel direction, so on a
  // banked surface it gains the vertical component that lets the ship climb
  // toward a rolled edge instead of only ever sliding along the ground plane.
  // `vx`/`vz` are its horizontal part, reused by the airborne and mesh-region
  // code (which integrate in world X/Z). On flat ground moveDir is horizontal,
  // so vx/vz equal the old sin/cos(velocityAngle)*speed exactly.
  const vel = _vel.copy(physics.moveDir).multiplyScalar(physics.speed);
  const vx = vel.x, vz = vel.z;

  if (physics.airborne) {
    let ax = vx, az = vz;
    let px = physics.groundPos.x + ax * dt;
    let pz = physics.groundPos.z + az * dt;

    // Rails are finite-height walls: they still block an airborne ship that has
    // not cleared their top, and are simply absent above it.
    for (const region of meshRegions) {
      if (physics.groundPos.y >= region.elevation + region.railHeight) continue;
      if (!TrackMesh.withinBounds(region.compiled, px, pz, TrackCore.COLLISION_WALL_MARGIN)) continue;
      const velocity = { x: ax, z: az };
      const before = Math.hypot(ax, az);
      const moved = slideAlongRails(physics, region, { x: physics.groundPos.x, z: physics.groundPos.z }, { x: px, z: pz }, velocity);
      if (!moved.hit) continue;
      px = moved.x; pz = moved.z; ax = velocity.x; az = velocity.z;
      physics.speed = Math.hypot(ax, az) * weightSpeedRetain(physics);
      addImpactJolt(physics, before - Math.hypot(ax, az));
      if (physics.speed > 1e-6) physics.moveDir.set(ax, 0, az).normalize();   // horizontal air travel dir
    }

    physics.verticalVel -= physics.gravity * dt;
    physics.groundPos.set(px, physics.groundPos.y + physics.verticalVel * dt, pz);

    const landing = meshRegionAt(px, pz, physics.groundPos.y);
    if (landing && physics.groundPos.y <= landing.region.elevation) {
      const impactSpeed = Math.max(0, -physics.verticalVel);
      landOnSurface(ship, UP);
      physics.landingBounce += Math.min(3.2, impactSpeed * 0.09);
      physics.landingBounceVel += Math.min(16, impactSpeed * 0.35);
      physics.groundPos.set(px, landing.region.elevation, pz);
      surfaceRenderPos = _meshSurfacePos.copy(physics.groundPos);
      surfaceNormal = UP;
    } else {
      c = sampleTrack(px, physics.groundPos.y, pz);
      const proj = projectToSurface(c, px, physics.groundPos.y, pz);
      const { s } = proj;
      const surface = curvedSurfaceFrame(c, s);
      if (corridorContains(c, px, physics.groundPos.y, pz, proj) && physics.groundPos.y <= surface.pos.y) {
        const impactSpeed = Math.max(0, -physics.verticalVel);
        landOnSurface(ship, surface.normal);
        physics.landingBounce += Math.min(3.2, impactSpeed * 0.09);
        physics.landingBounceVel += Math.min(16, impactSpeed * 0.35);
        physics.groundPos.copy(surface.pos);
        surfaceRenderPos = surface.pos;
        surfaceNormal = surface.normal;
      }
    }
  } else if (meshRegion && hasTranslation) {
    // Free 2D motion across a flat region, bounded only by railed edges. There
    // is no lateral parameter to clamp here -- the corridor's clamp(s) has no
    // meaning on an arbitrary polygon.
    const from = { x: physics.groundPos.x, z: physics.groundPos.z };
    const velocity = { x: vx, z: vz };
    const moved = slideAlongRails(physics, meshRegion, from, { x: from.x + vx * dt, z: from.z + vz * dt }, velocity);
    if (moved.hit) {
      const before = Math.hypot(vx, vz), after = Math.hypot(velocity.x, velocity.z);
      physics.speed = after * weightSpeedRetain(physics);
      if (physics.speed > 1e-6) physics.moveDir.set(velocity.x, 0, velocity.z).normalize();   // region is flat: horizontal
      addImpactJolt(physics, before - after);
    }

    const stillOn = TrackMesh.containsWorldPoint(meshRegion.compiled, moved.x, moved.z)
      ? meshRegion
      : (meshRegionAt(moved.x, moved.z, meshRegion.elevation) || {}).region || null;

    if (stillOn) {
      physics.groundPos.set(moved.x, stillOn.elevation, moved.z);
      surfaceRenderPos = _meshSurfacePos.copy(physics.groundPos);
      surfaceNormal = UP;
    } else {
      // Left the region across a bare edge. Drive onto the corridor if one
      // meets this region at a compatible height, otherwise it was a ledge.
      c = sampleTrack(moved.x, meshRegion.elevation, moved.z);
      const proj = projectToSurface(c, moved.x, meshRegion.elevation, moved.z);
      const { s } = proj;
      const surface = corridorContains(c, moved.x, meshRegion.elevation, moved.z, proj) ? curvedSurfaceFrame(c, s) : null;
      if (surface && Math.abs(surface.pos.y - meshRegion.elevation) <= SURFACE_SNAP_UP) {
        physics.groundPos.copy(surface.pos);
        // Region travel is horizontal; re-flatten it onto the (possibly banked)
        // corridor surface so the ship follows the ribbon instead of the plane.
        tangentize(physics.moveDir, surface.normal, physics.forward);
        tangentize(physics.forward, surface.normal, physics.moveDir);
        surfaceRenderPos = surface.pos;
        surfaceNormal = surface.normal;
      } else {
        beginAirborne(ship, _launchVel.copy(physics.moveDir).multiplyScalar(physics.speed));
        physics.groundPos.set(moved.x, meshRegion.elevation, moved.z);
      }
    }
  } else if (hasTranslation) {
    // Advance the FULL 3D position by the tangent-plane velocity, so on a bank
    // the ship climbs toward its rolled edge (vel.y != 0 there) rather than only
    // sliding along the ground plane and relying on reprojection to guess Y.
    const newPos = _newPos.copy(physics.groundPos).addScaledVector(vel, dt);

    // First test the proposed move against the segment we were already riding.
    // If it crosses that segment's wall, resolve against THIS segment before
    // sampleTrack() can pick another overlapping branch whose corridor happens
    // to contain the post-wall position (which would tunnel the ship across).
    const current = c;
    let projection = projectToSurface(current, newPos.x, newPos.y, newPos.z);
    let forceCurrentWall = !current.offEnd && (projection.s > projection.hiS || projection.s < projection.loS);

    if (!forceCurrentWall) {
      c = sampleTrack(newPos.x, newPos.y, newPos.z);
      projection = projectToSurface(c, newPos.x, newPos.y, newPos.z);
    }

    if (!forceCurrentWall && c.offEnd) {
      // Leaving an open curve: go ballistic from the current 3D velocity (its
      // vertical part matters if the end was banked or on a slope) until the
      // ship's X/Z is back over a curve.
      beginAirborne(ship, vel);
      physics.groundPos.copy(newPos);
    } else {
      const { er, s, loS, hiS } = projection;

      let hitSign = 0;
      if (s > hiS) hitSign = 1; else if (s < loS) hitSign = -1;
      let finalS = s;
      if (hitSign) {
        finalS = THREE.MathUtils.clamp(s, loS, hiS);
        // Bounce off the guard rail: reflect the velocity component pushing into
        // the wall (scaled by wallRestitution) instead of cancelling it, so a
        // square hit bounces back while a glancing one still mostly slides. The
        // wall's inward normal is the (unit, 3D) edgeRight signed by the side
        // hit; edgeRight is tangent to the surface, so reflecting the 3D
        // velocity about it keeps the ship on the surface AND works on a bank.
        _wallN.copy(er).multiplyScalar(hitSign);   // er is already a unit vector
        const into = vel.dot(_wallN);
        if (into > 0) {
          // Weight-scaled bounce, plus a momentum-scaled jolt on a real impact.
          vel.addScaledVector(_wallN, -into * (1 + weightRestitution()));
          addImpactJolt(physics, into);
        }
        physics.speed = vel.length() * weightSpeedRetain(physics);
        if (physics.speed > 1e-6) physics.moveDir.copy(vel).normalize();
      }

      const surface = curvedSurfaceFrame(c, finalS);
      physics.groundPos.copy(surface.pos);
      // No re-tangentize here: next frame's top-of-frame steering/grip flattens
      // forward/moveDir onto the new normal, once. Keeping tangentization to a
      // single projection per frame is what stops the facing precessing onto the
      // track tangent (see the render block and the parked branch).
      surfaceRenderPos = surface.pos;
      surfaceNormal = surface.normal;
    }
  }

  if (!physics.airborne && !hasTranslation && meshRegion) {
    // Parked on a flat region: nothing to reproject, just sit on the plane.
    physics.groundPos.y = meshRegion.elevation;
    surfaceRenderPos = _meshSurfacePos.copy(physics.groundPos);
    surfaceNormal = UP;
  } else if (!physics.airborne && !hasTranslation) {
    c = sampleTrack(physics.groundPos.x, physics.groundPos.y, physics.groundPos.z);
    if (ship.controller === idleController) {
      // The placeholder AI explicitly holds its authored grid pose. It still
      // runs physics and interaction detection; once an external effect gives
      // it speed, the normal translating branches take over.
      surfaceRenderPos = physics.groundPos;
      surfaceNormal = physics.up;
    } else {
      const { s, loS, hiS } = projectToSurface(c, physics.groundPos.x, physics.groundPos.y, physics.groundPos.z);
      const finalS = THREE.MathUtils.clamp(s, loS, hiS);
      const surface = curvedSurfaceFrame(c, finalS);
      physics.groundPos.copy(surface.pos);
      // No re-tangentize here: parked, the surface normal isn't changing, and the
      // top-of-frame steering/grip already flattened forward/moveDir onto it.
      surfaceRenderPos = surface.pos;
      surfaceNormal = surface.normal;
    }
  }

  // Sit on the surface, hovering a little along the surface normal. groundPos
  // (bob-free) is what next frame's integration reads from; shipGroup's
  // actual rendered position adds the hover bob on top of it. At disjoint
  // seams the nearest path segment can switch, producing a small projection
  // correction in groundPos; smooth only unexpectedly-large render deltas so
  // normal movement stays responsive while seam pops are eased out.
  // Zones: the boost timer advances every sub-step (it keeps running through the
  // air), but a trigger only fires while grounded on the zone's host surface.
  tickBoost(ship, dt);
  if (!physics.airborne) detectZoneTriggers(ship, c, meshRegion);

  // Triggers: swept crossing over this sub-step's motion (grounded OR airborne).
  detectTriggers(ship, ship.prevTriggerPos, physics.groundPos);
  ship.prevTriggerPos.copy(physics.groundPos);

  if (physics.airborne && physics.groundPos.y < trackFloorY) {
    respawn(ship);
    return { respawned: true };
  }
  return { surfaceNormal, surfaceRenderPos, respawned: false };
}

// Read input once per frame, integrate it in fixed-size sub-steps (see
// MAX_PHYSICS_STEP), then run the render/visual pass ONCE off the final
// integrated state. Sub-steps are equal and sum EXACTLY to dt, so there is no
// accumulator remainder to interpolate and normal movement stays frame-exact;
// a typical ~60fps frame is 2 steps, a long stall a few more.
function updateShip(ship, dt, intent) {
  const physics = ship.physics, shipGroup = ship.group;
  const { throttle, brake, steer } = intent;
  if (intent.respawn) { respawn(ship); return; }

  // A parked placeholder AI has no swept movement to tunnel or skip, so one
  // full physics/detection step per render frame is sufficient. Moving ships
  // retain the exact original fixed-size sub-stepping.
  const stationaryIdle = ship.controller === idleController && !physics.airborne && Math.abs(physics.speed) <= 0.001 && !throttle && !brake;
  const subSteps = stationaryIdle ? 1 : Math.max(1, Math.ceil(dt / MAX_PHYSICS_STEP));
  const sdt = dt / subSteps;
  let surfaceNormal = physics.up, surfaceRenderPos = physics.groundPos;
  for (let i = 0; i < subSteps; i++) {
    const r = stepPhysics(ship, sdt, throttle, brake, steer);
    if (r.respawned) return;   // ship already reset; skip this frame's render, matching the old early return
    surfaceNormal = r.surfaceNormal;
    surfaceRenderPos = r.surfaceRenderPos;
  }

  // Recomputed from the FINAL sub-step's speed (speed changes across sub-steps);
  // feeds only the cosmetic steer-lean flair below. Clamped like the physics
  // one so a boost's over-max speed doesn't overdrive the lean.
  const speedRatio = Math.min(1, Math.abs(physics.speed) / physics.maxSpeed);

  const expectedStep = Math.abs(physics.speed) * dt * 1.5 + 0.16;
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
  // The idle controller promises a genuinely stationary placeholder: suppress
  // even the cosmetic hover oscillation, which reads as a small forward creep
  // in the perspective camera despite groundPos being bit-for-bit unchanged.
  const hover = stationaryIdle ? 1 : 1 + Math.sin(physics.bobTime * 6) * 0.06 + physics.landingBounce;
  shipGroup.position.set(
    physics.visualGroundPos.x + physics.visualUp.x * hover,
    physics.visualGroundPos.y + physics.visualUp.y * hover,
    physics.visualGroundPos.z + physics.visualUp.z * hover
  );
  physics.up.copy(physics.visualUp);

  // Orient the ship to the road: nose along the (tangent-plane) facing vector,
  // "up" along the smoothed surface normal. Flatten a COPY of physics.forward
  // onto the render up purely to build a clean orthonormal basis for display.
  const up = physics.visualUp;
  const fwd = new THREE.Vector3().copy(physics.forward);
  fwd.addScaledVector(up, -fwd.dot(up));
  if (fwd.lengthSq() < 1e-9) fwd.copy(physics.moveDir);   // guard a degenerate flatten
  fwd.normalize();
  const right = new THREE.Vector3().crossVectors(up, fwd).normalize();
  const forward = new THREE.Vector3().crossVectors(right, up).normalize();
  shipGroup.quaternion.setFromRotationMatrix(new THREE.Matrix4().makeBasis(right, up, forward));
  // Deliberately DO NOT write this basis back into physics.forward. It was
  // flattened onto `visualUp`, which lags the true surface normal by a tiny
  // asymptotic amount; feeding it back re-projected the facing onto a slightly
  // different plane every frame, and since every surface normal along the path
  // is perpendicular to the track tangent, those planes all share that tangent
  // -- so repeated projection slowly precessed the nose onto it. That was the
  // ship rotating to face along the track when parked near 90deg roll. Steering
  // is the ONLY thing that turns physics.forward now; the camera reads it
  // directly and the sub-degree gap from this display basis is invisible.
  physics.right.copy(right);
  // Derived world azimuth of the nose -- all the top-down minimap consumes.
  physics.heading = Math.atan2(forward.x, forward.z);

  // Extra hover flair: lean into the steer, pitch back under acceleration.
  const targetBank = THREE.MathUtils.clamp(-steer * speedRatio * 0.5, -0.5, 0.5);
  physics.visualBank = THREE.MathUtils.lerp(physics.visualBank, targetBank, dt * 6);
  physics.visualPitch = THREE.MathUtils.lerp(physics.visualPitch, physics.speed * 0.004, dt * 6);
  shipGroup.quaternion.multiply(
    new THREE.Quaternion().setFromEuler(new THREE.Euler(physics.visualPitch, 0, physics.visualBank, 'XYZ'))
  );

  // HUD. 1 world unit = 1 metre (see CONTEXT.md), so speed is m/s and the
  // km/h readout is a straight m/s * 3.6 -- 140 m/s reads 504 km/h.
  if (ship === playerShip) {
    const kmh = Math.round(Math.abs(physics.speed) * 3.6);
    document.getElementById('speed').innerHTML = kmh + ' <span>km/h</span>' +
      (physics.boostActive ? ' <span style="color:#ffb020">▲ BOOST</span>' : '');
  }
}

// ---------- Chase camera (rigidly fixed behind and above the ship) ----------
const CAM_BACK = 13;         // distance behind the ship, at zoom 1
const CAM_UP_DEFAULT = 6.4;  // height above the ship, at zoom 1
// [ and ] scale both distance-behind and height together, so the camera moves
// straight in/out along its existing angle. O/K instead adjust camHeight
// itself -- independent of zoom -- so the camera can sit higher or lower
// without also changing how far back it is.
const CAM_ZOOM_MIN = 0.4;
const CAM_ZOOM_MAX = 3;
const CAM_ZOOM_RATE = 1.2;   // zoom multiplier change per second, held down
let camZoom = 1;
const CAM_UP_MIN = 0.5;
const CAM_UP_MAX = 25;
const CAM_UP_RATE = 6;   // units per second, held down
let camHeight = CAM_UP_DEFAULT;
// Height, in the ship's own local up, of the body mesh's geometric centre --
// matches body.position.y (BoxGeometry is centred on its own local origin, so
// that offset from shipGroup IS the body's centre). This is "the centre of the
// ship" the look-at target below is anchored to.
const SHIP_CENTER_HEIGHT = 0.3;
// The look-at target's FIXED offset from that centre, in the ship's own
// (forward, up) frame -- these numbers alone decide where the camera aims,
// independent of camera distance/zoom/height. Forward is well ahead of the
// ship (a look-ahead chase cam, so the aim leads into a turn rather than
// trailing behind it); up is a little above the body so the horizon doesn't
// sit right on the ship's roofline.
const LOOK_AT_FORWARD = 12;
const LOOK_AT_UP_DEFAULT = 1.6;
// P/L walk the look-at target's height up/down from its default -- e.g. aim
// higher over a crest, or lower for a tighter, more head-on view. Letters,
// like G/H/R below, so tracked by e.code (physical position): unlike [ and ],
// there's an established gaming convention for letter keys to be physical-
// position-based, and these don't have the AltGr-combo problem punctuation
// does.
const LOOK_AT_UP_MIN = -6;
const LOOK_AT_UP_MAX = 12;
const LOOK_AT_UP_RATE = 4;    // units per second, held down
let lookAtUp = LOOK_AT_UP_DEFAULT;
function updateCamera(dt) {
  if (!playerShip) return;
  const physics = playerShip.physics;
  // By character (e.key), not physical position (e.code) -- see the keydown
  // handler for why that matters for punctuation keys specifically.
  if (isDown(']')) camZoom = Math.min(CAM_ZOOM_MAX, camZoom * (1 + CAM_ZOOM_RATE * dt));
  if (isDown('[')) camZoom = Math.max(CAM_ZOOM_MIN, camZoom / (1 + CAM_ZOOM_RATE * dt));
  if (isDown('KeyO')) camHeight = Math.min(CAM_UP_MAX, camHeight + CAM_UP_RATE * dt);
  if (isDown('KeyK')) camHeight = Math.max(CAM_UP_MIN, camHeight - CAM_UP_RATE * dt);
  if (isDown('KeyP')) lookAtUp = Math.min(LOOK_AT_UP_MAX, lookAtUp + LOOK_AT_UP_RATE * dt);
  if (isDown('KeyL')) lookAtUp = Math.max(LOOK_AT_UP_MIN, lookAtUp - LOOK_AT_UP_RATE * dt);

  // Use the ship's own orthonormal (forward, up) basis -- the same one its
  // quaternion was built from -- rather than a flat heading-only forward.
  // Mixing a flat forward with the fully-banked `up` (as before) isn't even
  // orthogonal once the track rolls, so the camera would drift out from
  // directly behind the ship; and leaving camera.up at world Y meant the
  // rendered horizon didn't bank with the track, fighting the position
  // offset. Setting camera.up to the track's own up keeps the camera locked
  // directly behind/above the ship at any roll, including past 90 degrees.
  //
  // Note this basis is deliberately NOT shipGroup's actual quaternion: that
  // one also carries the cosmetic lean/pitch "flair" added at the end of
  // updateShip, and following that here would make the camera bank and
  // pitch along with it. physics.up/physics.forward are the orthonormal pair
  // the flair is layered ON TOP of, so building off them keeps the camera's
  // own motion smooth regardless of that flourish.
  const up = physics.up, fwd = physics.forward;
  // physics.visualGroundPos already tracks the ship's own smoothed render
  // position (shipGroup.position is this plus the hover bob), so anchoring
  // here keeps the camera glued to the ship without also picking up the bob's
  // small oscillation -- shipCenter is that same point, at the body's actual
  // centre height instead of an arbitrary lift.
  const shipCenter = physics.visualGroundPos.clone().addScaledVector(up, SHIP_CENTER_HEIGHT);
  camera.up.copy(up);
  camera.position.copy(shipCenter)
    .addScaledVector(fwd, -CAM_BACK * camZoom)
    .addScaledVector(up, camHeight * camZoom);
  // The look-at point: a FIXED position relative to the ship's centre, in the
  // ship's own frame, so it moves and turns rigidly with the ship. Neither
  // zoom nor camHeight (O/K) touch it -- only P/L, which adjust its own
  // height (lookAtUp) along `up`.
  const lookAt = shipCenter.clone()
    .addScaledVector(fwd, LOOK_AT_FORWARD)
    .addScaledVector(up, lookAtUp);
  camera.lookAt(lookAt);
}

// ---------- Minimap ----------
// A flat top-down (X/Z) overview in its own 2D canvas, separate from the
// three.js scene entirely -- it needs none of the perspective/lighting/
// texture machinery the main render does, just filled shapes and a dot.
// Same X -> screen-x, Z -> screen-y convention as the editor's top-down view,
// so anyone used to authoring tracks there sees the same orientation here.
// A missing element here (stale cached HTML, or a page that simply doesn't
// carry a #minimap) must degrade to "no minimap", not crash the render loop:
// drawMinimap() runs every frame inside animate(), so a null context there
// would take the entire game down over a purely decorative overlay.
const minimapCanvas = document.getElementById('minimap');
const minimapCtx = minimapCanvas ? minimapCanvas.getContext('2d') : null;
// Recomputed once per buildTrack() rather than every frame: it depends only on
// track geometry, which changes on import/rebuild, not on the ship's motion.
let minimapBounds = { minX: -1, maxX: 1, minZ: -1, maxZ: 1 };

function computeMinimapBounds() {
  let minX = Infinity, maxX = -Infinity, minZ = Infinity, maxZ = -Infinity;
  for (const p of paths) {
    for (const f of p.centerline) {
      minX = Math.min(minX, f.pos.x); maxX = Math.max(maxX, f.pos.x);
      minZ = Math.min(minZ, f.pos.z); maxZ = Math.max(maxZ, f.pos.z);
    }
  }
  for (const region of meshRegions) {
    const b = region.compiled.bounds;
    minX = Math.min(minX, b.minX); maxX = Math.max(maxX, b.maxX);
    minZ = Math.min(minZ, b.minZ); maxZ = Math.max(maxZ, b.maxZ);
  }
  minimapBounds = isFinite(minX) ? { minX, maxX, minZ, maxZ } : { minX: -1, maxX: 1, minZ: -1, maxZ: 1 };
}

// Pixels-per-world-unit that fits the WHOLE track's bounds into a w x h
// canvas, with margin, preserving aspect ratio (a stretched track would
// misrepresent turn angles). The map is ship-centred and rotates every frame
// (see drawMinimap), so this only ever sets the zoom level, never a
// translation -- a fixed offset from the track's bounding-box centre would be
// meaningless once the anchor is the ship instead.
function computeMinimapScale(w, h) {
  const { minX, maxX, minZ, maxZ } = minimapBounds;
  const margin = 10;
  const spanX = (maxX - minX) || 1, spanZ = (maxZ - minZ) || 1;
  return Math.min((w - 2 * margin) / spanX, (h - 2 * margin) / spanZ);
}

function minimapTracePolygon(ctx, toScreen, loop) {
  loop.forEach((p, i) => {
    const s = toScreen(p.x, p.z);
    i ? ctx.lineTo(s.x, s.y) : ctx.moveTo(s.x, s.y);
  });
  ctx.closePath();
}

// The road's actual drivable width, not just its centerline: reconstructs the
// same left/right edge offsets collision uses (pos + edgeRight * sLeft/sRight),
// so what the minimap shows as "road" matches what the ship can drive on.
function minimapTraceRoad(ctx, toScreen, path) {
  const cl = path.centerline;
  if (cl.length < 2) return;
  ctx.beginPath();
  cl.forEach((f, i) => {
    const s = toScreen(f.pos.x + f.edgeRight.x * f.sLeft, f.pos.z + f.edgeRight.z * f.sLeft);
    i ? ctx.lineTo(s.x, s.y) : ctx.moveTo(s.x, s.y);
  });
  for (let i = cl.length - 1; i >= 0; i--) {
    const f = cl[i];
    const s = toScreen(f.pos.x + f.edgeRight.x * f.sRight, f.pos.z + f.edgeRight.z * f.sRight);
    ctx.lineTo(s.x, s.y);
  }
  ctx.closePath();
  ctx.fill();
}

function drawMinimap() {
  if (!minimapCtx || !playerShip) return;
  const physics = playerShip.physics;
  const dpr = window.devicePixelRatio || 1;
  const rect = minimapCanvas.getBoundingClientRect();
  const w = rect.width, h = rect.height;
  if (w <= 0 || h <= 0) return;   // e.g. display:none while hidden
  const pixelW = Math.round(w * dpr), pixelH = Math.round(h * dpr);
  if (minimapCanvas.width !== pixelW) minimapCanvas.width = pixelW;
  if (minimapCanvas.height !== pixelH) minimapCanvas.height = pixelH;
  minimapCtx.setTransform(dpr, 0, 0, dpr, 0, 0);
  minimapCtx.clearRect(0, 0, w, h);

  // Heading-up: the ship sits fixed at the canvas centre and everything else
  // is rotated around it by the ship's own heading, so the ship's forward
  // direction always renders as screen-up, whichever way it's actually
  // pointed in the world. Built from a proper rotation, not just cancelling
  // heading -- the world (x, z) plane maps directly to screen (x, y) with no
  // flip elsewhere in this file, so a naive "rotate by -heading" (see the
  // derivation this replaced) came out mirrored: physics.right rendered on
  // the LEFT instead of the right. Solving for the transform that sends both
  // fwd -> screen-up AND physics.right -> screen-right simultaneously
  // (fwd_xz, right_xz form an orthonormal basis of the plane, so any point's
  // screen offset is just its projection onto each, placed on the matching
  // screen axis) gets both of those right.
  //
  // The map is then mirrored left/right on top of that -- negating offX is
  // the one line that does it. This was tried first as a CSS
  // `transform: scaleX(-1)` on the canvas element, which had no visible
  // effect, so it's done directly in the pixels drawn instead: that can't be
  // silently absorbed by compositing, a stale stylesheet, or anything else
  // between this code and the screen.
  const scale = computeMinimapScale(w, h);
  const shipX = physics.groundPos.x, shipZ = physics.groundPos.z;
  const heading = physics.heading;
  const sinH = Math.sin(heading), cosH = Math.cos(heading);
  const cx = w / 2, cy = h / 2;
  const toScreen = (x, z) => {
    const rx = x - shipX, rz = z - shipZ;
    const offX = -(rx * cosH - rz * sinH);
    const offY = -(rx * sinH + rz * cosH);
    return { x: cx + offX * scale, y: cy + offY * scale };
  };

  // Mesh regions first: backdrop surfaces, drawn beneath the roads.
  minimapCtx.fillStyle = 'rgba(120,90,180,0.55)';
  for (const region of meshRegions) {
    minimapCtx.beginPath();
    for (const poly of region.compiled.polygons) {
      minimapTracePolygon(minimapCtx, toScreen, poly.outer);
      for (const hole of poly.holes) minimapTracePolygon(minimapCtx, toScreen, hole);
    }
    minimapCtx.fill('evenodd');
  }

  minimapCtx.fillStyle = 'rgba(127,180,212,0.85)';
  for (const p of paths) minimapTraceRoad(minimapCtx, toScreen, p);

  // AI markers use their deterministic hull colours; draw the larger player
  // marker last so pole-position overlap never hides it.
  for (const other of ships) {
    if (other === playerShip) continue;
    const marker = toScreen(other.physics.groundPos.x, other.physics.groundPos.z);
    minimapCtx.beginPath(); minimapCtx.arc(marker.x, marker.y, 3, 0, Math.PI * 2);
    minimapCtx.fillStyle = '#' + other.color.toString(16).padStart(6, '0'); minimapCtx.fill();
    minimapCtx.lineWidth = 1; minimapCtx.strokeStyle = '#101820'; minimapCtx.stroke();
  }
  const marker = toScreen(physics.groundPos.x, physics.groundPos.z);
  minimapCtx.beginPath(); minimapCtx.arc(marker.x, marker.y, 4.5, 0, Math.PI * 2);
  minimapCtx.fillStyle = '#ffcc33'; minimapCtx.fill();
  minimapCtx.lineWidth = 1.5; minimapCtx.strokeStyle = '#3a2400'; minimapCtx.stroke();
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

// Modules export nothing to the page, so expose a small read-only handle for
// debugging from the console and for browser smoke tests.
window.__game = {
  get meshRegions() { return meshRegions; },
  get paths() { return paths; },
  get zones() { return zones; },
  get triggers() { return triggers; },
  get ships() { return ships; },
  get race() { return playerShip && playerShip.race; },
  get physics() { return playerShip && playerShip.physics; },
  get trackFloorY() { return trackFloorY; },
  respawn
};

// ---------- Main loop ----------
const clock = new THREE.Clock();
function animate() {
  requestAnimationFrame(animate);
  const dt = Math.min(clock.getDelta(), 0.05);
  for (const ship of ships) updateShip(ship, dt, ship.controller.intent(ship, dt));
  updateCamera(dt);
  updateTriggerDebug(dt);
  updateRaceHud();
  drawMinimap();
  renderer.render(scene, camera);
}
animate();
