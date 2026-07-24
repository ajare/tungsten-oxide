
import * as TrackMesh from './track-mesh.js';
import { DEFAULT_SHIP_COUNT, gridSlot } from './ship-grid.js';
import { Vec3 } from './vec3.js';
// The physics core was extracted, verbatim, into track-physics.js (see
// CPP_PORT_PLAN.md milestone 0). This module keeps only the THREE rendering,
// input, track-building glue and the animate loop; every physics symbol below
// is imported. Stateful physics lives on `sim` (a Simulation), which buildTrack
// populates with the baked track data.
import {
  Simulation, MAX_PHYSICS_STEP, RESPAWN_FALL_DEPTH,
  createPhysicsState, createRaceState, applyHandling,
  curvedSurfaceFrame, projectToSurface, tangentize
} from './track-physics.js';

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
// Baked centerline vectors are Vec3 (js/vec3.js) now that the physics is
// THREE-free; Vec3 mirrors THREE.Vector3's method set, so the rendering code
// below consumes them unchanged.
const toVec = o => new Vec3(o.x, o.y, o.z);
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

// The stateful physics engine. buildTrack() populates its baked track data; the
// animate loop drives it. Mesh-region collision is delegated to TrackMesh (out
// of the C++ port's scope). Game-only trigger side effects — the console log and
// the player's checkpoint-flash — are injected here; the portable checkpoint/lap
// logic runs inside the Simulation itself.
const sim = new Simulation({
  TrackMesh,
  now: () => performance.now(),
  onTriggerFired: (ship, rec, dir, state) => {
    if (ship === playerShip) state.flash = TRIGGER_FLASH_TIME;
    console.log(`[trigger][${ship.id}] ${rec.id} fired (${dir})`);
  }
});

// Respawn wrapper: resolves the default/by-id ship the console and HUD use, then
// defers to the Simulation. stepPhysics calls sim.respawn(ship) directly.
function respawn(ship = playerShip) {
  if (typeof ship === 'string') ship = ships.find(s => s.id === ship);
  sim.respawn(ship);
}

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

  // Hand the freshly-baked, world-space track data to the physics engine. These
  // arrays are rebuilt wholesale on every buildTrack, so re-pointing the sim at
  // them here (after zones/triggers/floor are final, before the roster samples
  // the track) keeps the two in sync.
  sim.paths = paths;
  sim.meshRegions = meshRegions;
  sim.zones = zones;
  sim.triggers = triggers;
  sim.connectedEndpointIds = connectedEndpointIds;
  sim.trackFloorY = trackFloorY;

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
      const rows = strip.rows || strip.left.map((left, i) => [left, strip.right[i]]);
      for (let i = 0; i < rows.length - 1; i++) {
        const u0 = dist[i] * uScale, u1 = dist[i + 1] * uScale;
        const across = Math.min(rows[i].length, rows[i + 1].length);
        for (let j = 0; j < across - 1; j++) {
          const a = rows[i][j], b = rows[i][j + 1], c = rows[i + 1][j], d = rows[i + 1][j + 1];
          const v0 = vW * (j / (across - 1)), v1 = vW * ((j + 1) / (across - 1));
          pos.push(a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z);
          uv.push(u0, v0, u0, v1, u1, v0);
          pos.push(b.x, b.y, b.z, d.x, d.y, d.z, c.x, c.y, c.z);
          uv.push(u0, v1, u1, v1, u1, v0);
        }
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

// (Physics — shipParamG, effectiveMaxSpeed, boost, detectZoneTriggers — moved to
// track-physics.js; zone/boost detection now runs inside sim.stepPhysics.)

// ---------- Triggers / checkpoints ----------
// Vertical gate quads the ship passes THROUGH (see track-core.js). Never
// rendered in normal play; a debug view (J) draws each as a translucent quad
// with a direction arrow, coloured by armed state and flashing on fire. Dummy
// triggers only log; checkpoints drive lap progress and recovery.
let triggers = [];
let ships = [];
let playerShip = null;
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

// (Physics — fireTrigger, detectTriggers, resetTriggers — moved to
// track-physics.js. The portable checkpoint/lap logic runs inside the
// Simulation; the console log + player checkpoint-flash are the sim's
// onTriggerFired hook, wired at the top of this file.)

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

// (Physics — meshRegionAt, projectToSurface, corridorContains, surfaceOwnerAt,
//  applyHandling/weightRestitution/weightSpeedRetain/addImpactJolt, slideAlongRails,
//  curvedSurfaceHeight/Frame, sampleTrack — moved to track-physics.js.)

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
      canonical = sim.sampleTrack(surface.pos.x, surface.pos.y, surface.pos.z);
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

// (Physics — placeShipAtPose, respawn — moved to track-physics.js; a thin
// respawn wrapper resolving the default/by-id ship lives at the top of this
// file.)

function buildRoster(track, count = DEFAULT_SHIP_COUNT) {
  disposeShips();
  const now = performance.now();
  ships = Array.from({ length: count }, (_, i) => createShip(i, track, now));
  playerShip = ships[0] || null;
  const poses = startingGridPoses(ships.length);
  ships.forEach((ship, i) => {
    applyHandling(track, ship.physics);
    ship.startPose = poses[i];
    sim.placeShipAtPose(ship, ship.startPose);
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
// (createPhysicsState, tangentize/signedAngleAbout, beginAirborne/landOnSurface,
//  MAX_PHYSICS_STEP and stepPhysics moved to track-physics.js. stepPhysics is now
//  sim.stepPhysics; the animate loop still owns the fixed-size sub-stepping below.)

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
    const r = sim.stepPhysics(ship, sdt, throttle, brake, steer);
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
