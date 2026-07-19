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
  const scene = buildUsdScene(track, { TrackCore, crossSectionSegments: 4 });
  assert.equal(scene.meshes.length, 1);
  assert.equal(scene.meshes[0].points.length, 12 * (4 + 1));
  assert.equal(scene.meshes[0].faces.length, 12 * 4 * 2);
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
 * catch that class of drift. */
for (const [curvature, tightness] of [[1, 1], [1, 2.5], [-0.6, 0.4]]) {
  test(`export builds the road from TrackCore's cross-section (curvature ${curvature}, tightness ${tightness})`, () => {
    const segments = 4;
    const scene = buildUsdScene(crownedStraightTrack(curvature, tightness), { TrackCore, crossSectionSegments: segments });
    const ring = scene.meshes[0].points.slice(0, segments + 1);

    assert.deepEqual(
      ring.map(p => Number(p[1].toFixed(6))),
      Array.from({ length: segments + 1 }, (_, j) =>
        Number(TrackCore.crossSectionHeight(curvature, tightness, j / segments, 10).toFixed(6)))
    );
    // Guard the guard: a profile that never leaves the chord would pass the
    // comparison above trivially.
    assert.ok(Math.abs(ring[segments / 2][1]) > 1, 'expected a genuinely crowned road');
  });
}

test('a flat cross section leaves the road on its chord', () => {
  const scene = buildUsdScene(crownedStraightTrack(0, 1), { TrackCore, crossSectionSegments: 4 });
  for (const p of scene.meshes[0].points) assert.equal(p[1], 0);
});

test('open curve export has no end caps', () => {
  const scene = buildUsdScene(openStraightTrack(), { TrackCore, crossSectionSegments: 2 });
  assert.equal(scene.meshes[0].points.length, 8 * 3);
  assert.equal(scene.meshes[0].faces.length, 7 * 2 * 2);
});

test('curve export includes generated square-ish texture coordinates', () => {
  const scene = buildUsdScene(openStraightTrack(), { TrackCore, crossSectionSegments: 2 });
  const mesh = scene.meshes[0];
  assert.equal(mesh.uvs.length, mesh.points.length);
  assert.deepEqual(mesh.uvs.slice(0, 3).map(uv => uv[0]), [0, 0.5, 1]);
  assert.equal(mesh.uvs[0][1], 0);
  assert.ok(mesh.uvs.at(-1)[1] > 8 && mesh.uvs.at(-1)[1] < 10, `expected path length / width scale, got ${mesh.uvs.at(-1)[1]}`);
  assert.match(scene.text, /texCoord2f\[\] primvars:st/);
  assert.match(scene.text, /uniform token primvars:st:interpolation = "vertex"/);
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
