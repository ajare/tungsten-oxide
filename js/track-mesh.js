/* track-mesh.js — shared mesh-region math for the game (track.html) and the
 * editor (editor.html). This is the mesh-world counterpart to track-core.js,
 * split out because it depends on the geometry-js ES module while track-core.js
 * is deliberately dependency-free.
 *
 * A track may carry MESH REGIONS: flat, drivable areas imported from the
 * geometry-js editor, used for plazas, junction pads and arenas that a swept
 * spline ribbon cannot express.
 *
 *   track.meshAssets = { <assetId>: <geometry-js mesh JSON> }
 *   track.meshes     = [ { id, asset, x, z, rotation, elevation }, ... ]
 *
 * An ASSET is the geometry: a 2D geometry-js mesh whose local (x, y) maps to
 * world (x, z). Rail flags live on the asset's edge `attributes.rail`, so every
 * placement of an asset is railed identically -- place the same file twice
 * under two ids if you need two railings.
 *
 * A PLACEMENT is a rigid transform of an asset: translation in X/Z, a yaw
 * rotation about the vertical axis, and a single `elevation` (the whole region
 * is horizontal, so its surface normal is always +Y). Because the transform is
 * rigid, a triangulation computed in asset space stays valid for every
 * placement -- assets are triangulated once, not per placement or per frame.
 *
 * compile(mesh, placement) bakes a placement into world-space loops, triangles
 * and rail segments; the game samples those directly and the editor draws them.
 *
 * Rail semantics: an edge flagged `rail` is a solid wall of finite height that
 * the ship slides along and can clear when airborne. An unflagged boundary edge
 * is a ledge -- crossing it drops the ship into the existing ballistic code.
 * Holes follow exactly the same rules as the outer boundary.
 */
import { SerializerMeshChunk } from '@willpower/geometry';

export const DEFAULT_RAIL_HEIGHT = 3;
export const RAIL_ATTRIBUTE = 'rail';

// --- assets ----------------------------------------------------------------

export function meshFromJSON(data) {
  const mesh = SerializerMeshChunk.fromJSON(data);
  if (!mesh.polygons.size) throw new Error('Mesh contains no polygons');
  return mesh;
}

export function meshToJSON(mesh) { return SerializerMeshChunk.toJSON(mesh); }

// Local-space extents, used to centre a freshly imported asset on the view.
export function assetBounds(mesh) {
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
  for (const v of mesh.vertices.values()) {
    minX = Math.min(minX, v.position.x); maxX = Math.max(maxX, v.position.x);
    minY = Math.min(minY, v.position.y); maxY = Math.max(maxY, v.position.y);
  }
  if (!isFinite(minX)) return { minX: 0, minY: 0, maxX: 0, maxY: 0, cx: 0, cy: 0 };
  return { minX, minY, maxX, maxY, cx: (minX + maxX) / 2, cy: (minY + maxY) / 2 };
}

// Derive a unique asset id from a filename, so re-importing the same file
// always yields a new asset rather than disturbing existing placements.
export function uniqueAssetId(filename, taken) {
  const base = String(filename || 'mesh').replace(/\.[^.]*$/, '').replace(/[^a-zA-Z0-9_-]+/g, '-')
    .replace(/^-+|-+$/g, '').toLowerCase() || 'mesh';
  if (!taken.has(base)) return base;
  for (let i = 2; ; i++) if (!taken.has(`${base}-${i}`)) return `${base}-${i}`;
}

// --- rails -----------------------------------------------------------------

export function isRailEdge(mesh, edgeId) {
  return !!mesh.getEdge(edgeId)?.attributes?.[RAIL_ATTRIBUTE];
}

export function setRailEdge(mesh, edgeId, on) {
  const edge = mesh.getEdge(edgeId);
  if (!edge) return false;
  if (on) edge.attributes[RAIL_ATTRIBUTE] = true;
  else delete edge.attributes[RAIL_ATTRIBUTE];
  return true;
}

export function toggleRailEdge(mesh, edgeId) {
  return setRailEdge(mesh, edgeId, !isRailEdge(mesh, edgeId));
}

/* An edge is on the region's rim exactly when a single polygon claims it:
 * two owners means it is an interior seam between neighbouring faces, and
 * zero means it is dangling geometry bounding nothing drivable. Hole rims
 * count -- a hole's edges belong to the one polygon that owns the hole -- so
 * this rails the inside of a hole too, which is what makes an imported hole a
 * walled-off pillar rather than a pit. */
export function isBoundaryEdge(mesh, edgeId) {
  return mesh.getEdge(edgeId)?.polygons?.size === 1;
}

/* Rail every rim edge, leaving interior seams bare. Used on import so a fresh
 * region is enclosed by default; individual edges are then unflagged in the
 * editor's Rails mode to open up ledges. Returns the number railed. */
export function railBoundaryEdges(mesh) {
  let n = 0;
  for (const [edgeId] of mesh.edges) {
    if (!isBoundaryEdge(mesh, edgeId)) continue;
    if (setRailEdge(mesh, edgeId, true)) n++;
  }
  return n;
}

// --- placement transform ---------------------------------------------------
// Asset-local (x, y) maps to world (x, z); `rotation` is a yaw in degrees
// applied about the placement origin before translation.

function placementTrig(placement) {
  const a = (placement?.rotation || 0) * Math.PI / 180;
  return { cos: Math.cos(a), sin: Math.sin(a) };
}

export function localToWorld(placement, lx, ly, trig = placementTrig(placement)) {
  return {
    x: lx * trig.cos - ly * trig.sin + (placement.x || 0),
    z: lx * trig.sin + ly * trig.cos + (placement.z || 0)
  };
}

export function worldToLocal(placement, wx, wz, trig = placementTrig(placement)) {
  const dx = wx - (placement.x || 0), dz = wz - (placement.z || 0);
  return { x: dx * trig.cos + dz * trig.sin, y: -dx * trig.sin + dz * trig.cos };
}

// --- compilation -----------------------------------------------------------

function loopPoints(mesh, polygon, placement, trig) {
  return polygon.vertexIndices().map(id => {
    const p = mesh.vertices.get(id).position;
    return localToWorld(placement, p.x, p.y, trig);
  });
}

function boundsOf(points, into) {
  for (const p of points) {
    into.minX = Math.min(into.minX, p.x); into.maxX = Math.max(into.maxX, p.x);
    into.minZ = Math.min(into.minZ, p.z); into.maxZ = Math.max(into.maxZ, p.z);
  }
  return into;
}

/* Bake one placement of an asset into world space. Returns loops (for drawing
 * and containment), triangles (for rendering), and rail segments with outward
 * normals (for collision). Hole polygons are never triangulated in their own
 * right -- they are subtracted from the polygon that owns them. */
export function compile(mesh, placement) {
  const trig = placementTrig(placement);
  const elevation = placement.elevation || 0;
  const bounds = { minX: Infinity, maxX: -Infinity, minZ: Infinity, maxZ: -Infinity };
  const polygons = [];
  const triangles = [];

  for (const [id, polygon] of mesh.polygons) {
    if (polygon.hole) continue;
    const outer = loopPoints(mesh, polygon, placement, trig);
    const holes = polygon.holes
      .map(h => mesh.polygons.get(h))
      .filter(Boolean)
      .map(h => loopPoints(mesh, h, placement, trig));
    polygons.push({ polygonId: id, outer, holes });
    boundsOf(outer, bounds);

    let tris = [];
    try {
      tris = polygon.triangulate(mesh);
    } catch (err) {
      console.warn(`track-mesh: polygon ${id} failed to triangulate`, err);
    }
    for (const tri of tris) {
      const pts = tri.map(vid => {
        const p = mesh.vertices.get(vid).position;
        return localToWorld(placement, p.x, p.y, trig);
      });
      if (pts.length === 3) triangles.push(pts);
    }
  }

  // Rail walls. The outward normal points away from the drivable interior so
  // the ship can be pushed back out along it; it is derived from the edge's
  // own midpoint containment rather than winding, which holes reverse.
  const rails = [];
  for (const [edgeId, edge] of mesh.edges) {
    if (!edge.attributes?.[RAIL_ATTRIBUTE]) continue;
    const p0 = mesh.vertices.get(edge.vertices[0])?.position;
    const p1 = mesh.vertices.get(edge.vertices[1])?.position;
    if (!p0 || !p1) continue;
    const a = localToWorld(placement, p0.x, p0.y, trig);
    const b = localToWorld(placement, p1.x, p1.y, trig);
    const dx = b.x - a.x, dz = b.z - a.z;
    const len = Math.hypot(dx, dz);
    if (len < 1e-9) continue;
    let nx = dz / len, nz = -dx / len;
    const probe = 0.01;
    const mx = (a.x + b.x) / 2, mz = (a.z + b.z) / 2;
    if (containsWorldPoint({ polygons }, mx + nx * probe, mz + nz * probe)) { nx = -nx; nz = -nz; }
    rails.push({ edgeId, a, b, nx, nz, len });
  }

  if (!isFinite(bounds.minX)) { bounds.minX = bounds.maxX = bounds.minZ = bounds.maxZ = 0; }
  return { id: placement.id, assetId: placement.asset, placement, elevation, polygons, triangles, rails, bounds };
}

// --- queries ---------------------------------------------------------------

function pointInLoop(loop, x, z) {
  let inside = false;
  for (let i = 0, j = loop.length - 1; i < loop.length; j = i++) {
    const a = loop[i], b = loop[j];
    if ((a.z > z) !== (b.z > z) && x < ((b.x - a.x) * (z - a.z)) / (b.z - a.z) + a.x) inside = !inside;
  }
  return inside;
}

/* True when X/Z lies on the drivable part of a compiled region: inside some
 * polygon's outer loop and outside every hole that polygon owns. */
export function containsWorldPoint(compiled, x, z) {
  for (const poly of compiled.polygons) {
    if (!pointInLoop(poly.outer, x, z)) continue;
    if (poly.holes.some(h => pointInLoop(h, x, z))) continue;
    return true;
  }
  return false;
}

export function withinBounds(compiled, x, z, pad = 0) {
  const b = compiled.bounds;
  return x >= b.minX - pad && x <= b.maxX + pad && z >= b.minZ - pad && z <= b.maxZ + pad;
}

export function closestPointOnSegment(ax, az, bx, bz, px, pz) {
  const dx = bx - ax, dz = bz - az;
  const len2 = dx * dx + dz * dz;
  if (len2 < 1e-12) return { x: ax, z: az, t: 0 };
  const t = Math.max(0, Math.min(1, ((px - ax) * dx + (pz - az) * dz) / len2));
  return { x: ax + dx * t, z: az + dz * t, t };
}

/* Segment/segment crossing, returning the parameter along the first segment,
 * or null. Rail collision is swept rather than positional so a fast ship cannot
 * tunnel straight through a wall between two frames. */
export function segmentCrossing(ax, az, bx, bz, cx, cz, dx, dz) {
  const rx = bx - ax, rz = bz - az;
  const sx = dx - cx, sz = dz - cz;
  const denom = rx * sz - rz * sx;
  if (Math.abs(denom) < 1e-12) return null;
  const t = ((cx - ax) * sz - (cz - az) * sx) / denom;
  const u = ((cx - ax) * rz - (cz - az) * rx) / denom;
  if (t < 0 || t > 1 || u < 0 || u > 1) return null;
  return t;
}

/* Move across a compiled region from `from` toward `to`, stopping at any railed
 * edge crossed on the way and sliding along it. `velocity` is mutated in place:
 * only the component pushing into the wall is cancelled, so the ship glides
 * along a rail rather than stopping dead -- the same behaviour the spline
 * corridor's wall clamp already produces. Repeats a few times so running into a
 * concave corner resolves against both walls instead of jittering between them.
 */
export function slideAlongRails(compiled, from, to, velocity, margin) {
  let cur = { x: from.x, z: from.z };
  let target = { x: to.x, z: to.z };
  let hit = false;

  for (let iter = 0; iter < 3; iter++) {
    let nearest = null;
    for (const rail of compiled.rails) {
      const t = segmentCrossing(cur.x, cur.z, target.x, target.z, rail.a.x, rail.a.z, rail.b.x, rail.b.z);
      if (t !== null && (!nearest || t < nearest.t)) nearest = { t, rail };
    }
    if (!nearest) break;
    hit = true;
    const { rail, t } = nearest;
    const hx = cur.x + (target.x - cur.x) * t;
    const hz = cur.z + (target.z - cur.z) * t;
    // Stop just inside the wall, then carry the remaining motion along it.
    cur = { x: hx - rail.nx * margin, z: hz - rail.nz * margin };
    const remX = target.x - hx, remZ = target.z - hz;
    const into = remX * rail.nx + remZ * rail.nz;
    target = { x: cur.x + remX - rail.nx * into, z: cur.z + remZ - rail.nz * into };
    const vInto = velocity.x * rail.nx + velocity.z * rail.nz;
    if (vInto > 0) { velocity.x -= rail.nx * vInto; velocity.z -= rail.nz * vInto; }
  }
  return { x: target.x, z: target.z, hit };
}

/* Every edge of an asset in world space, flagged with its current rail state.
 * The editor uses this for both drawing and click-picking in Rails mode. */
export function edgeSegments(mesh, placement) {
  const trig = placementTrig(placement);
  const out = [];
  for (const [edgeId, edge] of mesh.edges) {
    const p0 = mesh.vertices.get(edge.vertices[0])?.position;
    const p1 = mesh.vertices.get(edge.vertices[1])?.position;
    if (!p0 || !p1) continue;
    out.push({
      edgeId,
      rail: !!edge.attributes?.[RAIL_ATTRIBUTE],
      a: localToWorld(placement, p0.x, p0.y, trig),
      b: localToWorld(placement, p1.x, p1.y, trig)
    });
  }
  return out;
}
