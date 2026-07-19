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

test('open curve export has no end caps', () => {
  const track = TrackCore.parseTrack(JSON.stringify({
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
  const scene = buildUsdScene(track, { TrackCore, crossSectionSegments: 2 });
  assert.equal(scene.meshes[0].points.length, 8 * 3);
  assert.equal(scene.meshes[0].faces.length, 7 * 2 * 2);
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
