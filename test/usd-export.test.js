import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { Mesh, Vector2 } from '@willpower/geometry';
import * as TrackMesh from '../js/track-mesh.js';
import { buildUsdScene, exportTrackToUSDA, sanitizeFileStem, sanitizeUsdIdentifier } from '../js/usd-export.js';

function loadTrackCore() {
  const src = readFileSync(new URL('../track-core.js', import.meta.url), 'utf8');
  const fakeWindow = {};
  new Function('window', src)(fakeWindow);
  return fakeWindow.TrackCore;
}
const TrackCore = loadTrackCore();

function squareMeshJSON() {
  const mesh = new Mesh();
  mesh.addPolygon([new Vector2(0, 0), new Vector2(10, 0), new Vector2(10, 10), new Vector2(0, 10)]);
  return TrackMesh.meshToJSON(mesh);
}

test('USD export writes an ASCII Y-up scene with default Track prim and materials', () => {
  const text = exportTrackToUSDA(TrackCore.cloneTrack(TrackCore.STARTER_TRACK), { TrackCore });
  assert.match(text, /^#usda 1\.0/);
  assert.match(text, /defaultPrim = "Track"/);
  assert.match(text, /metersPerUnit = 1/);
  assert.match(text, /upAxis = "Y"/);
  assert.match(text, /def Xform "Track"/);
  assert.match(text, /def Material "RoadSurface"/);
  assert.match(text, /def Mesh "Path_0"/);
});

test('closed curve export shares the seam ring instead of duplicating it', () => {
  const track = TrackCore.cloneTrack(TrackCore.STARTER_TRACK);
  track.samples = 12;
  const scene = buildUsdScene(track, { TrackCore });
  // An extruded curve exports as two prims: the road surface, then its shell.
  assert.equal(scene.meshes.length, 2);
  assert.equal(scene.meshes[0].material, 'RoadSurface');
  // A closed loop wraps: exactly `samples` longitudinal joins, each built from
  // the (possibly adaptive) union of its two rings' v-breakpoints -- with the
  // STARTER_TRACK's flat cross section (curvature 0), every ring collapses to
  // just its two edges, so each join is a single quad (2 triangles).
  assert.equal(scene.meshes[0].faces.length, 12 * 2);
});

function openStraightTrack() {
  return TrackCore.parseTrack(JSON.stringify({
    version: 5,
    name: 'Open',
    samples: 8,
    paths: [{
      closed: false,
      points: [
        { type: 'position', id: 'a', pos: [0, 0, 0], weight: 1 },
        { type: 'position', id: 'b', pos: [30, 0, 0], weight: 1 },
        { type: 'position', id: 'c', pos: [60, 0, 0], weight: 1 },
        { type: 'position', id: 'd', pos: [90, 0, 0], weight: 1 },
        { type: 'width', t: 0, width: 10 },
        { type: 'width', t: 1, width: 10 }
      ]
    }]
  }));
}

/* A dead-straight, flat, level road of a known width, crowned by a curved cross
 * section. Because it is flat and level, every exported vertex's Y is exactly
 * the cross-section height at that point across the road, which is what lets
 * the test below compare the exporter against TrackCore directly. */
function crownedStraightTrack(curvature, tightness) {
  return TrackCore.parseTrack(JSON.stringify({
    version: 5,
    name: 'Crowned',
    samples: 8,
    paths: [{
      closed: false,
      points: [
        { type: 'position', id: 'a', pos: [0, 0, 0], weight: 1 },
        { type: 'position', id: 'b', pos: [30, 0, 0], weight: 1 },
        { type: 'position', id: 'c', pos: [60, 0, 0], weight: 1 },
        { type: 'position', id: 'd', pos: [90, 0, 0], weight: 1 },
        { type: 'width', t: 0, width: 10 },
        { type: 'width', t: 1, width: 10 },
        { type: 'crossSection', t: 0, curvature, tightness },
        { type: 'crossSection', t: 1, curvature, tightness }
      ]
    }]
  }));
}

/* The exporter must build the road from TrackCore's cross-section profile, not
 * a formula of its own. It once had its own -- a triangular tent where the game
 * and editor draw a semicircular arc -- so an exported track silently stopped
 * matching the one that was authored and driven. The two agree at the centre
 * and both edges no matter what the profile is, so only intermediate v values
 * catch that class of drift. The exporter now also picks its own adaptive
 * v-breakpoints per ring (TrackCore.crossSectionBreakpoints), so this checks
 * every vertex the exporter actually placed -- including whatever intermediate
 * ones the adaptive algorithm added -- against TrackCore directly, rather than
 * assuming a fixed segment count. */
for (const [curvature, tightness] of [[1, 1], [1, 2.5], [-0.6, 0.4]]) {
  test(`export builds the road from TrackCore's cross-section (curvature ${curvature}, tightness ${tightness})`, () => {
    const scene = buildUsdScene(crownedStraightTrack(curvature, tightness), { TrackCore });
    const surface = scene.meshes[0];
    // Every point at texV (path-distance coordinate) 0 belongs to the very
    // first ring; every one of those must sit exactly on TrackCore's own
    // profile at that ring's v.
    const firstRing = new Map();
    surface.points.forEach((p, idx) => {
      const [v, texV] = surface.uvs[idx];
      if (texV === 0) firstRing.set(v, p[1]);
    });
    // Guard the guard: fewer than the base 3 breakpoints (0, 0.5, 1) would mean
    // the adaptive algorithm never even ran, and a profile that never leaves
    // the chord would pass the per-point comparison below trivially.
    assert.ok(firstRing.size >= 3, `expected at least the base breakpoints, got ${firstRing.size}`);
    for (const [v, y] of firstRing) {
      assert.equal(Number(y.toFixed(6)), Number(TrackCore.crossSectionHeight(curvature, tightness, v, 10).toFixed(6)));
    }
    assert.ok(Math.abs(firstRing.get(0.5)) > 1, 'expected a genuinely crowned road');
  });
}

test('a flat cross section leaves the road on its chord', () => {
  const scene = buildUsdScene(crownedStraightTrack(0, 1), { TrackCore });
  for (const p of scene.meshes[0].points) assert.equal(p[1], 0);
});

test('open curve export has no end caps', () => {
  const scene = buildUsdScene(openStraightTrack(), { TrackCore });
  const surface = scene.meshes[0];
  // No crossSection points are authored (curvature 0, flat), so every ring
  // collapses to just its two edges and each of this open path's 7
  // longitudinal joins is exactly one quad (4 points, 2 faces); an open curve
  // draws no wrap-around join and no end caps.
  assert.equal(surface.faces.length, 7 * 2);
  assert.equal(surface.points.length, 7 * 4);
});

test('curve export includes generated square-ish texture coordinates', () => {
  const scene = buildUsdScene(openStraightTrack(), { TrackCore });
  const mesh = scene.meshes[0];
  assert.equal(mesh.uvs.length, mesh.points.length);
  const us = [...new Set(mesh.uvs.map(uv => uv[0]))].sort((a, b) => a - b);
  assert.deepEqual(us, [0, 1], 'a flat road has no interior cross-section breakpoints');
  assert.equal(mesh.uvs[0][1], 0);
  assert.ok(mesh.uvs.at(-1)[1] > 8 && mesh.uvs.at(-1)[1] < 10, `expected path length / width scale, got ${mesh.uvs.at(-1)[1]}`);
  assert.match(scene.text, /texCoord2f\[\] primvars:st/);
  assert.match(scene.text, /uniform token primvars:st:interpolation = "vertex"/);
});

/* A flat, level, straight open road: its normals are +Y, so the extruded shell's
 * underside must sit exactly `thickness` below the surface in Y. That makes the
 * geometry checkable by inspection rather than by reimplementing the maths. */
function slabTrack(thickness, closed = false) {
  return TrackCore.parseTrack(JSON.stringify({
    version: TrackCore.TRACK_SCHEMA_VERSION, name: 'slab', samples: 6,
    paths: [{ closed, points: [
      { type: 'position', id: 'a', pos: [0, 0, 0], weight: 1 },
      { type: 'position', id: 'b', pos: [40, 0, 0], weight: 1 },
      { type: 'position', id: 'c', pos: [80, 0, 0], weight: 1 },
      { type: 'position', id: 'd', pos: [120, 0, 0], weight: 1 },
      { type: 'width', t: 0, width: 20 }, { type: 'width', t: 1, width: 20 },
      { type: 'crossSection', t: 0, curvature: 0, tightness: 1, thickness },
      { type: 'crossSection', t: 1, curvature: 0, tightness: 1, thickness }
    ] }]
  }));
}
// Y component of a face's normal; negative means the face looks downward.
function faceNormalY(points, f) {
  const a = points[f[0]], b = points[f[1]], c = points[f[2]];
  return (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]);
}

test('an extruded curve exports a shell prim beneath its surface', () => {
  const scene = buildUsdScene(slabTrack(6), { TrackCore });
  assert.equal(scene.meshes.length, 2);
  const [surface, shell] = scene.meshes;
  assert.equal(surface.material, 'RoadSurface');
  assert.equal(shell.material, 'RoadShell');
  assert.match(scene.text, /def Mesh "Path_0_Shell"/);
  assert.match(scene.text, /def Material "RoadShell"/);

  // Flat cross-section (curvature 0): every ring collapses to just its two
  // edges, so each of this open path's 5 longitudinal joins contributes 2
  // bottom triangles (3 points apiece) plus 2 side quads (4 points apiece),
  // and the 2 open ends cap with one more quad each.
  assert.equal(shell.points.length, 5 * (2 * 3 + 2 * 4) + 2 * 4);
  assert.equal(shell.faces.length, 5 * (2 + 2 * 2) + 2 * 2);
  assert.deepEqual([...new Set(surface.points.map(p => +p[1].toFixed(6)))], [0]);
  assert.deepEqual([...new Set(shell.points.map(p => +p[1].toFixed(6)))].sort((x, y) => x - y), [-6, 0],
    'underside sits exactly `thickness` below the road');
});

test('a zero-thickness curve exports no shell at all', () => {
  const scene = buildUsdScene(slabTrack(0), { TrackCore });
  assert.equal(scene.meshes.length, 1, 'the old zero-thickness sheet is still available');
  assert.equal(scene.meshes[0].material, 'RoadSurface');
  // The material is declared unconditionally in the Materials scope, exactly as
  // MeshRegionSurface already is for tracks with no regions; what must be absent
  // is the shell PRIM.
  assert.doesNotMatch(scene.text, /def Mesh "Path_0_Shell"/);
});

/* orientFacesUp cannot orient a shell -- it flips on the summed face normal, and
 * on a closed shell the top and bottom cancel. The underside decides instead, so
 * pin that it really does end up facing down and nothing ends up inverted. */
test('the shell underside faces downward', () => {
  const shell = buildUsdScene(slabTrack(6), { TrackCore }).meshes[1];
  const down = shell.faces.filter(f => faceNormalY(shell.points, f) < -1e-9).length;
  const up = shell.faces.filter(f => faceNormalY(shell.points, f) > 1e-9).length;
  assert.ok(down > 0, 'expected underside faces');
  assert.equal(up, 0, `no shell face may point up, got ${up}`);
});

test('an open shell is capped at both ends, a closed one is not', () => {
  const open = buildUsdScene(slabTrack(6, false), { TrackCore }).meshes[1];
  const closed = buildUsdScene(slabTrack(6, true), { TrackCore }).meshes[1];
  const rings = 6;   // track.samples
  // Flat cross-section: every ring collapses to just its two edges, so each
  // longitudinal join is 2 bottom triangles + 2 side quads (6 faces total).
  // closed: every ring joins the next, and it wraps shut, so no caps.
  assert.equal(closed.faces.length, rings * 6);
  // open: one fewer longitudinal join, plus two end caps of one quad each.
  assert.equal(open.faces.length, (rings - 1) * 6 + 2 * 2);
});

test('mesh placements are exported as separate baked Mesh prims', () => {
  const track = TrackCore.cloneTrack(TrackCore.STARTER_TRACK);
  track.paths = [];
  track.meshAssets = { pad: { name: 'Pad', railHeight: 6, mesh: squareMeshJSON() } };
  track.meshes = [{ id: 'pad one', asset: 'pad', x: 100, z: -20, rotation: 90, elevation: 7 }];

  const scene = buildUsdScene(track, { TrackCore });
  assert.equal(scene.meshes.length, 1);
  assert.equal(scene.meshes[0].material, 'MeshRegionSurface');
  assert.match(scene.text, /def Mesh "MeshRegion_pad_one"/);
  assert.match(scene.text, /\(100, 7, -20\)/);
});

test('invalid paths are skipped with USD warning comments', () => {
  const track = { name: 'Bad', samples: 4, paths: [{ closed: false, points: [] }], meshAssets: {}, meshes: [] };
  const scene = buildUsdScene(track, { TrackCore });
  assert.equal(scene.meshCount, 0);
  assert.equal(scene.warnings.length, 1);
  assert.match(scene.text, /# WARNING: Skipped path 0/);
});

test('sanitizers produce safe USD prim names and file stems', () => {
  assert.equal(sanitizeUsdIdentifier('123 weird-name!', 'Prim'), '_123_weird_name');
  assert.equal(sanitizeFileStem('Starter Track!'), 'starter-track');
});

/* Regression test for a real crack, not a hypothetical one: a ring is shared
 * by TWO neighboring strips, and each strip independently unions the ring's
 * own breakpoints with whichever OTHER ring it's paired with. If both strips
 * then evaluate this ring's TRUE analytic surface at their own "foreign" v's
 * (v's the ring didn't pick for itself), the ring ends up drawn as two
 * different polylines -- one per strip -- because a straight edge between two
 * sparse points is not the same shape as a finer polyline through the same
 * two endpoints plus real curve samples in between. The fix pins every
 * ring's edge to a single, fixed polyline (its own breakpoints only) by
 * linearly interpolating any foreign v between the ring's own two bracketing
 * REAL vertices instead of resampling the curve.
 *
 * This independently bakes the same centerline/edges the exporter does, so it
 * can compute the CORRECT reference height at every v the exporter actually
 * placed a vertex at (exact at the ring's own breakpoints, lerped between
 * them otherwise) and check every exported vertex against it directly. */
test('a ring shared between differently-adaptive neighbors renders a single consistent edge (no crack)', () => {
  const track = TrackCore.parseTrack(JSON.stringify({
    version: TrackCore.TRACK_SCHEMA_VERSION, name: 'crack-check', samples: 6,
    paths: [{ closed: false, points: [
      { type: 'position', id: 'a', pos: [0, 0, 0], weight: 1 },
      { type: 'position', id: 'b', pos: [40, 0, 0], weight: 1 },
      { type: 'position', id: 'c', pos: [80, 0, 0], weight: 1 },
      { type: 'position', id: 'd', pos: [120, 0, 0], weight: 1 },
      { type: 'position', id: 'e', pos: [160, 0, 0], weight: 1 },
      { type: 'width', t: 0, width: 30 }, { type: 'width', t: 1, width: 30 },
      { type: 'crossSection', t: 0, curvature: 1, tightness: 3.5 },
      { type: 'crossSection', t: 0.5, curvature: 0.4, tightness: 1 },
      { type: 'crossSection', t: 1, curvature: 1, tightness: 0.3 }
    ] }]
  }));

  // Bake the same centerline/edges the exporter does internally, purely to
  // independently compute each ring's own canonical breakpoints and a
  // reference "stitched" height at any v.
  const path = track.paths[0];
  const parts = TrackCore.splitPoints(path.points);
  const raw = TrackCore.buildCenterline(parts.controlPoints, 6, false, parts.rollPoints, parts.widthPoints, parts.crossSectionPoints);
  const edges = TrackCore.buildEdges(raw, false);
  const chordWidths = raw.map((_, i) => {
    const l = edges.left[i], r = edges.right[i];
    return Math.hypot(r.x - l.x, r.y - l.y, r.z - l.z) || 1;
  });
  const ringBreaks = raw.map((f, i) => TrackCore.crossSectionBreakpoints(f.crossSectionCurvature, f.crossSectionTightness, chordWidths[i]));
  // Guard the guard: if every ring happened to need the same breakpoints,
  // there would be no foreign v's anywhere and this test would pass vacuously.
  assert.ok(new Set(ringBreaks.map(b => b.length)).size > 1, `expected varying breakpoint counts, got ${ringBreaks.map(b => b.length)}`);

  const referenceHeight = (i, v) => {
    const own = ringBreaks[i];
    if (own.includes(v)) return TrackCore.crossSectionHeight(raw[i].crossSectionCurvature, raw[i].crossSectionTightness, v, chordWidths[i]);
    let lo = own[0], hi = own[own.length - 1];
    for (let k = 0; k < own.length - 1; k++) {
      if (own[k] <= v && v <= own[k + 1]) { lo = own[k]; hi = own[k + 1]; break; }
    }
    const hLo = TrackCore.crossSectionHeight(raw[i].crossSectionCurvature, raw[i].crossSectionTightness, lo, chordWidths[i]);
    const hHi = TrackCore.crossSectionHeight(raw[i].crossSectionCurvature, raw[i].crossSectionTightness, hi, chordWidths[i]);
    return hLo + (hHi - hLo) * (v - lo) / (hi - lo);
  };

  const distances = [0];
  for (let i = 1; i < raw.length; i++) {
    const a = raw[i - 1].pos, b = raw[i].pos;
    distances[i] = distances[i - 1] + Math.hypot(b.x - a.x, b.y - a.y, b.z - a.z);
  }
  const repWidth = chordWidths.reduce((s, w) => s + w, 0) / chordWidths.length;
  const texVToRing = new Map(distances.map((d, i) => [Number((d / repWidth).toFixed(9)), i]));

  const scene = buildUsdScene(track, { TrackCore });
  const surface = scene.meshes[0];
  let foreignChecked = 0;
  surface.points.forEach((p, idx) => {
    const [v, texV] = surface.uvs[idx];
    const ring = texVToRing.get(Number(texV.toFixed(9)));
    assert.ok(ring !== undefined, `couldn't map texV ${texV} back to a ring`);
    const expected = referenceHeight(ring, v);
    assert.ok(Math.abs(p[1] - expected) < 1e-6,
      `ring ${ring} at v=${v}: expected height ${expected} (stitched), got ${p[1]} -- a crack`);
    if (!ringBreaks[ring].includes(v)) foreignChecked++;
  });
  assert.ok(foreignChecked > 0, 'expected the union-stitching to actually exercise a foreign v somewhere');
});
