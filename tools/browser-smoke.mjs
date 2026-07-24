/* Browser smoke tests for the mesh-region feature.
 *
 * These drive real pages in headless Chromium: they catch ESM/import-map
 * breakage, runtime errors, and physics regressions that the pure-logic tests
 * in track-mesh.test.js cannot see. They are NOT part of `npm test` because
 * they need a browser:
 *
 *     npm install --no-save playwright && npx playwright install chromium
 *     node tools/browser-smoke.mjs
 */
import { chromium } from 'playwright';
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { extname, normalize, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = fileURLToPath(new URL('..', import.meta.url));
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.json': 'application/json' };

const server = createServer(async (req, res) => {
  try {
    const rel = normalize(decodeURIComponent(req.url.split('?')[0])).replace(/^[\\/]+/, '');
    const body = await readFile(join(ROOT, rel));
    res.writeHead(200, { 'Content-Type': TYPES[extname(rel)] || 'application/octet-stream' });
    res.end(body);
  } catch { res.writeHead(404); res.end('nope'); }
});
await new Promise(r => server.listen(8123, r));

const browser = await chromium.launch();
let failures = 0;

async function visit(name, path, after) {
  const page = await browser.newPage();
  const errors = [];
  page.on('console', m => { if (m.type() === 'error') errors.push('console: ' + m.text()); });
  page.on('pageerror', e => errors.push('pageerror: ' + e.message));
  page.on('requestfailed', r => errors.push('requestfailed: ' + r.url()));

  await page.goto(`http://localhost:8123/${path}`, { waitUntil: 'networkidle' });
  await page.waitForTimeout(1200);
  let extra = '';
  try { extra = after ? await after(page) : ''; } catch (e) { errors.push('interaction: ' + e.message); }

  if (errors.length) { failures++; console.log(`FAIL ${name}`); for (const e of errors) console.log('   ' + e); }
  else console.log(`ok   ${name}${extra ? ' — ' + extra : ''}`);
  await page.close();
}

// A geometry-js style mesh file: 30x30 pad with a 10x10 hole.
const meshFile = JSON.stringify({
  vertices: [
    { id: 0, position: { x: 0, y: 0 } }, { id: 1, position: { x: 30, y: 0 } },
    { id: 2, position: { x: 30, y: 30 } }, { id: 3, position: { x: 0, y: 30 } },
    { id: 4, position: { x: 10, y: 10 } }, { id: 5, position: { x: 20, y: 10 } },
    { id: 6, position: { x: 20, y: 20 } }, { id: 7, position: { x: 10, y: 20 } }
  ],
  edges: [
    { id: 0, vertices: [0, 1] }, { id: 1, vertices: [1, 2] }, { id: 2, vertices: [2, 3] }, { id: 3, vertices: [3, 0] },
    { id: 4, vertices: [4, 5] }, { id: 5, vertices: [5, 6] }, { id: 6, vertices: [6, 7] }, { id: 7, vertices: [7, 4] }
  ],
  polygons: [
    { id: 0, edges: [{ edge: 0, v0: 0, v1: 1 }, { edge: 1, v0: 1, v1: 2 }, { edge: 2, v0: 2, v1: 3 }, { edge: 3, v0: 3, v1: 0 }], holes: [1], hole: false },
    { id: 1, edges: [{ edge: 4, v0: 4, v1: 5 }, { edge: 5, v0: 5, v1: 6 }, { edge: 6, v0: 6, v1: 7 }, { edge: 7, v0: 7, v1: 4 }], holes: [], hole: true }
  ]
});

await visit('editor: grid visibility toggle and shortcut disable snapping', 'editor.html', async (page) => {
  await page.check('#snapGridChk');
  await page.keyboard.press('g');
  let state = await page.evaluate(() => ({
    shown: document.getElementById('showGridChk').checked,
    snapChecked: document.getElementById('snapGridChk').checked,
    snapDisabled: document.getElementById('snapGridChk').disabled,
    sizeDisabled: document.getElementById('gridSizeSelect').disabled
  }));
  if (state.shown || !state.snapChecked || !state.snapDisabled || !state.sizeDisabled)
    throw new Error('G did not hide the grid and suspend its controls: ' + JSON.stringify(state));
  await page.keyboard.press('g');
  state = await page.evaluate(() => ({
    shown: document.getElementById('showGridChk').checked,
    snapChecked: document.getElementById('snapGridChk').checked,
    snapDisabled: document.getElementById('snapGridChk').disabled
  }));
  if (!state.shown || !state.snapChecked || state.snapDisabled)
    throw new Error('G did not restore the grid and prior snap preference: ' + JSON.stringify(state));
  return 'G hides the grid, suspends snap, and restores the prior snap preference';
});

await visit('editor: bundled and browsed textures save references, not embedded image data', 'editor.html', async (page) => {
  await page.waitForFunction(() => Object.values(window.__editor.track.textureAssets || {})
    .some(asset => asset.path === 'assets/track/wipeout_seamless_track_texture_512x512.png'));
  await page.click('#texturesBtn');
  await page.setInputFiles('#textureFileInput', join(ROOT, 'assets/test-1.png'));
  await page.waitForTimeout(300);
  const result = await page.evaluate(() => {
    const assets = Object.values(window.__editor.track.textureAssets || {});
    const json = window.TrackCore.serializeTrack(window.__editor.track);
    return { assets, json };
  });
  if (!result.assets.some(asset => asset.path === 'test-1.png'))
    throw new Error('file dialog selection did not store its filename reference: ' + JSON.stringify(result.assets));
  if (!result.assets.some(asset => asset.path === 'assets/track/wipeout_seamless_track_texture_512x512.png'))
    throw new Error('bundled track texture was not loaded: ' + JSON.stringify(result.assets));
  if (result.assets.some(asset => 'dataUrl' in asset) || result.json.includes('data:image') || result.json.includes('dataUrl'))
    throw new Error('texture image data leaked into track JSON');
  return 'auto-loaded the bundled texture and saved file references without image bytes';
});

let generatedRandomTrackJSON, generatedRampTrackJSON;
await visit('editor: random generator preserves textures and builds deterministic mesh gaps, checkpoints, and boosts', 'editor.html', async (page) => {
  await page.setInputFiles('#textureFileInput', join(ROOT, 'assets/test-1.png'));
  await page.waitForTimeout(200);
  await page.click('#randomRangesBtn');
  await page.evaluate(() => {
    const values = {
      rrMeshChanceMin: 100, rrMeshChanceMax: 100, rrSequenceChance: 0, rrMaxMeshSections: 2,
      rrMeshLengthMin: 180, rrMeshLengthMax: 220, rrEndDropMin: 20, rrEndDropMax: 20,
      rrBoostMin: 2, rrBoostMax: 2
    };
    for (const [id, value] of Object.entries(values)) {
      const el = document.getElementById(id); el.value = value; el.dispatchEvent(new Event('change', { bubbles: true }));
    }
  });
  const generate = async () => {
    await page.evaluate(() => {
      const seed = document.getElementById('randomSeed');
      seed.value = 424242; seed.dispatchEvent(new Event('change', { bubbles: true }));
    });
    await page.waitForTimeout(300);
    return page.evaluate(() => ({
      json: window.TrackCore.serializeTrack(window.__editor.track),
      paths: window.__editor.track.paths.map(p => ({ id: p.id, closed: p.closed })),
      positionPoints: window.__editor.track.paths.reduce((sum, p) => sum + p.points.filter(point => point.type === 'position').length, 0),
      tightTurnGrades: window.__editor.track.paths.flatMap(path => {
        if (path.id.startsWith('random-ramp-')) return [];
        const cps = path.points.filter(point => point.type === 'position');
        const grades = [];
        for (let i = 1; i < cps.length - 1; i++) {
          const a = cps[i - 1].pos, b = cps[i].pos, c = cps[i + 1].pos;
          const ix = b[0] - a[0], iz = b[2] - a[2], ox = c[0] - b[0], oz = c[2] - b[2];
          const il = Math.hypot(ix, iz) || 1, ol = Math.hypot(ox, oz) || 1;
          const angle = Math.acos(Math.max(-1, Math.min(1, (ix * ox + iz * oz) / (il * ol))));
          if (angle >= Math.PI / 8) grades.push(Math.max(Math.abs(b[1] - a[1]) / il, Math.abs(c[1] - b[1]) / ol));
        }
        return grades;
      }),
      drops: window.__editor.track.paths.map((path, i, paths) => {
        const endpoint = (p, end) => { const pp = window.TrackCore.splitPoints(p.points); const ev = window.TrackCore.makeEvaluator(pp.controlPoints, false); return ev.evalTrack(end ? ev.CP_N - 1 : 0).pos; };
        return endpoint(path, true).y - endpoint(paths[(i + 1) % paths.length], false).y;
      }),
      meshes: window.__editor.track.meshes.length,
      meshCenterErrors: window.__editor.track.meshes.map((mesh, i) => {
        const endpoint = (p, end) => { const pp = window.TrackCore.splitPoints(p.points); const ev = window.TrackCore.makeEvaluator(pp.controlPoints, false); return ev.evalTrack(end ? ev.CP_N - 1 : 0).pos; };
        const a = endpoint(window.__editor.track.paths[i], true), b = endpoint(window.__editor.track.paths[(i + 1) % window.__editor.track.meshes.length], false);
        return Math.hypot(mesh.x - (a.x + b.x) / 2, mesh.z - (a.z + b.z) / 2);
      }),
      rails: window.__editor.compiledMeshes().map(m => m.compiled.rails.length),
      zones: window.__editor.track.zones,
      texturePaths: Object.values(window.__editor.track.textureAssets || {}).map(asset => asset.path).sort(),
      checkpoints: window.__editor.track.triggers.filter(t => t.type === 'checkpoint')
    }));
  };
  const first = await generate(), second = await generate();
  generatedRandomTrackJSON = first.json;
  if (first.json !== second.json) throw new Error('same random seed and settings did not reproduce identical JSON');
  if (!first.texturePaths.includes('test-1.png') || !first.texturePaths.includes('assets/track/wipeout_seamless_track_texture_512x512.png'))
    throw new Error('random generation unloaded currently-added textures: ' + JSON.stringify(first.texturePaths));
  if (first.meshes !== 2 || first.paths.length < 2 || first.paths.some(p => p.closed !== false))
    throw new Error('expected two mesh-connected open path sections: ' + JSON.stringify(first));
  if (first.positionPoints > 20) throw new Error(`random track authored ${first.positionPoints} position controls; expected at most 20`);
  if (!first.tightTurnGrades.length) throw new Error('random-track fixture contains no tight turn to exercise grade suppression');
  if (first.tightTurnGrades.some(grade => grade > 1e-6))
    throw new Error('tight horizontal turn was combined with an elevation change: ' + first.tightTurnGrades);
  if (first.rails.some(n => n !== 2)) throw new Error('generated platforms must rail only their two sides: ' + first.rails);
  if (first.meshCenterErrors.some(error => error > 1e-6)) throw new Error('single platforms must be centered evenly between their open ends: ' + first.meshCenterErrors);
  if (first.drops.some(drop => drop <= 0)) throw new Error('receiving path must be below every outgoing path: ' + first.drops);
  if (first.zones.length !== 2 || first.zones.some(z => z.effect !== 'velocityChange'))
    throw new Error('expected two generated boost pads: ' + JSON.stringify(first.zones));
  const finish = first.checkpoints.filter(t => t.role === 'finish'), intermediate = first.checkpoints.filter(t => t.role === 'intermediate');
  if (finish.length !== 1 || intermediate.length !== 2 || first.checkpoints.some(t => t.direction !== 'forward'))
    throw new Error('generated checkpoint route is incomplete: ' + JSON.stringify(first.checkpoints));
  const expectedCheckpointPaths = [first.paths[1].id, first.paths[0].id];
  if (intermediate.some((checkpoint, i) => checkpoint.host.pathId !== expectedCheckpointPaths[i]))
    throw new Error('intermediate checkpoints are not ordered along the driven route: ' + JSON.stringify(intermediate));

  await page.evaluate(() => {
    const values = { rrMeshChanceMin: 100, rrMeshChanceMax: 100, rrSequenceChance: 100, rrMaxMeshSections: 1,
      rrMeshLengthMin: 300, rrMeshLengthMax: 300, rrEndDropMin: 15, rrEndDropMax: 15 };
    for (const [id, value] of Object.entries(values)) {
      const el = document.getElementById(id); el.value = value; el.dispatchEvent(new Event('change', { bubbles: true }));
    }
  });
  let rampCase = null;
  for (let seedValue = 1; seedValue <= 12 && !rampCase; seedValue++) {
    await page.evaluate(seedValue => {
      const seed = document.getElementById('randomSeed');
      seed.value = seedValue; seed.dispatchEvent(new Event('change', { bubbles: true }));
    }, seedValue);
    rampCase = await page.evaluate(() => {
      const track = window.__editor.track;
      const ramps = track.paths.filter(p => p.id.startsWith('random-ramp-')).length;
      if (!ramps) return null;
      const ordinary = track.paths.find(p => !p.id.startsWith('random-ramp-'));
      const pp = window.TrackCore.splitPoints(ordinary.points), ev = window.TrackCore.makeEvaluator(pp.controlPoints, false);
      const a = ev.evalTrack(ev.CP_N - 1).pos, b = ev.evalTrack(0).pos;
      const dx = b.x - a.x, dz = b.z - a.z, distance = Math.hypot(dx, dz), ux = dx / distance, uz = dz / distance;
      const platforms = track.meshes.map(mesh => {
        const vertices = track.meshAssets[mesh.asset].mesh.vertices;
        const xs = vertices.map(v => v.position.x);
        return { along: (mesh.x - a.x) * ux + (mesh.z - a.z) * uz, length: Math.max(...xs) - Math.min(...xs) };
      }).sort((x, y) => x.along - y.along);
      const gaps = [platforms[0].along - platforms[0].length / 2];
      for (let i = 1; i < platforms.length; i++) gaps.push(platforms[i].along - platforms[i].length / 2 - (platforms[i - 1].along + platforms[i - 1].length / 2));
      gaps.push(distance - (platforms.at(-1).along + platforms.at(-1).length / 2));
      return { ramps, meshes: track.meshes.length, spacingError: Math.max(...gaps) - Math.min(...gaps), json: window.TrackCore.serializeTrack(track) };
    });
  }
  if (!rampCase || rampCase.meshes < 2 || rampCase.meshes > 4)
    throw new Error('platform sequences did not generate a launch ramp: ' + JSON.stringify(rampCase));
  if (rampCase.spacingError > 1e-6)
    throw new Error('platform sequence gaps are not evenly distributed between the open ends: ' + JSON.stringify(rampCase));
  generatedRampTrackJSON = rampCase.json;
  return `${first.positionPoints} route controls, 2 side-railed gaps, 2 boosts, ordered checkpoints, and a ${rampCase.meshes}-platform ramp sequence`;
});

await visit('game: generated mesh section is traversable into the receiving path', 'track.html', async (page) => {
  await page.evaluate(json => localStorage.setItem('web3d.currentTrack', json), generatedRandomTrackJSON);
  await page.reload({ waitUntil: 'networkidle' });
  await page.waitForTimeout(500);
  const built = await page.evaluate(() => ({ paths: window.__game.paths.length, meshes: window.__game.meshRegions.length, zones: window.__game.zones.length }));
  if (built.paths < 2 || built.meshes !== 2 || built.zones !== 2) throw new Error('generated world did not compile: ' + JSON.stringify(built));
  await page.evaluate(() => {
    const game = window.__game, path = game.paths[0], p = game.physics;
    const frame = path.centerline[Math.max(0, path.centerline.length - 10)];
    p.groundPos.copy(frame.pos); p.visualGroundPos.copy(frame.pos);
    p.forward.copy(frame.tangent); p.moveDir.copy(frame.tangent); p.up.copy(frame.normal); p.visualUp.copy(frame.normal);
    p.speed = p.maxSpeed * 0.6; p.airborne = false; p.verticalVel = 0;
    window.dispatchEvent(new KeyboardEvent('keydown', { code: 'KeyW' }));
  });
  let closestLanding = Infinity, closestAny = Infinity, landingSpeed = 0;
  for (let i = 0; i < 60; i++) {
    await page.waitForTimeout(100);
    const sample = await page.evaluate(() => {
      const game = window.__game, p = game.physics, receiving = game.paths[1].centerline;
      let nearest = Infinity;
      for (const frame of receiving) nearest = Math.min(nearest, frame.pos.distanceTo(p.groundPos));
      return { airborne: p.airborne, nearest, speed: p.speed };
    });
    closestAny = Math.min(closestAny, sample.nearest);
    if (!sample.airborne && sample.nearest < closestLanding) { closestLanding = sample.nearest; landingSpeed = sample.speed; }
  }
  await page.evaluate(() => window.dispatchEvent(new KeyboardEvent('keyup', { code: 'KeyW' })));
  if (closestLanding > 45) throw new Error('ship did not reach the receiving path: ' + JSON.stringify({ closestLanding, closestAny }));
  return `landed on receiving path at ${landingSpeed.toFixed(1)} m/s`;
});

await visit('game: generated ramp sequence can return to its receiving path', 'track.html', async (page) => {
  await page.evaluate(json => localStorage.setItem('web3d.currentTrack', json), generatedRampTrackJSON);
  await page.reload({ waitUntil: 'networkidle' });
  await page.waitForTimeout(500);
  await page.evaluate(() => {
    const game = window.__game, path = game.paths[0], p = game.physics;
    const frame = path.centerline[Math.max(0, path.centerline.length - 10)];
    p.groundPos.copy(frame.pos); p.visualGroundPos.copy(frame.pos);
    p.forward.copy(frame.tangent); p.moveDir.copy(frame.tangent); p.up.copy(frame.normal); p.visualUp.copy(frame.normal);
    p.speed = p.maxSpeed * 0.6; p.airborne = false; p.verticalVel = 0;
    window.dispatchEvent(new KeyboardEvent('keydown', { code: 'KeyW' }));
  });
  let closestLanding = Infinity, closestAny = Infinity, landingSpeed = 0;
  let closestMeshes = await page.evaluate(() => window.__game.meshRegions.map(() => Infinity));
  for (let i = 0; i < 65; i++) {
    await page.waitForTimeout(100);
    const sample = await page.evaluate(() => {
      const game = window.__game, p = game.physics, receiving = game.paths[0].centerline;
      let nearestStart = Infinity;
      for (let j = 0; j < Math.min(80, receiving.length); j++) nearestStart = Math.min(nearestStart, receiving[j].pos.distanceTo(p.groundPos));
      return { airborne: p.airborne, nearestStart, speed: p.speed,
        meshDistances: game.meshRegions.map(r => Math.hypot(r.compiled.placement.x - p.groundPos.x, r.compiled.placement.z - p.groundPos.z)) };
    });
    closestMeshes = closestMeshes.map((d, j) => Math.min(d, sample.meshDistances[j]));
    closestAny = Math.min(closestAny, sample.nearestStart);
    if (!sample.airborne && sample.nearestStart < closestLanding) { closestLanding = sample.nearestStart; landingSpeed = sample.speed; }
  }
  await page.evaluate(() => window.dispatchEvent(new KeyboardEvent('keyup', { code: 'KeyW' })));
  if (closestLanding > 25) throw new Error('ship did not complete generated ramp sequence: ' + JSON.stringify({ closestLanding, closestAny, closestMeshes }));
  return `completed ramp sequence at ${landingSpeed.toFixed(1)} m/s`;
});

await visit('editor: both halves of a deleted segment accept textures independently', 'editor.html', async (page) => {
  await page.click('#newBtn');
  await page.waitForTimeout(200);
  await page.setInputFiles('#textureFileInput', join(ROOT, 'assets/test-1.png'));
  await page.waitForTimeout(200);
  await page.click('#texturesBtn');
  await page.waitForFunction(() => document.querySelector('#textureAssetList .tile'));
  await page.click('#textureAssetList .tile');
  await page.click('#closeTexturePanelBtn');
  const point = await page.evaluate(() => {
    const cps = window.TrackCore.splitPoints(window.__editor.track.paths[0].points).controlPoints;
    const cp = cps[Math.floor(cps.length / 2) - 1];
    const s = window.__editor.worldToScreen(cp.pos[0], cp.pos[2]);
    const r = document.getElementById('topCanvas').getBoundingClientRect();
    return { x: r.left + s.x, y: r.top + s.y, count: cps.length };
  });
  if (point.count < 8) throw new Error('starter track needs at least 8 controls for this split test');
  await page.mouse.click(point.x, point.y);
  await page.waitForTimeout(100);
  await page.click('#delSegmentBtn'); // closed loop -> one open path
  await page.waitForTimeout(150);
  const interior = await page.evaluate(() => {
    const cps = window.TrackCore.splitPoints(window.__editor.track.paths[0].points).controlPoints;
    const cp = cps[3];
    const s = window.__editor.worldToScreen(cp.pos[0], cp.pos[2]);
    const r = document.getElementById('topCanvas').getBoundingClientRect();
    return { x: r.left + s.x, y: r.top + s.y };
  });
  await page.mouse.click(interior.x, interior.y);
  await page.waitForTimeout(100);
  await page.click('#delSegmentBtn'); // open path -> two open paths
  await page.waitForTimeout(200);
  if (await page.evaluate(() => window.__editor.track.paths.length) !== 2) throw new Error('deleting the interior segment did not split the path');

  const assignments = await page.evaluate(() => window.__editor.track.paths.map(path => path.texture || null));
  if (assignments.some(texture => !texture)) throw new Error('splitting a textured path dropped one or both assignments: ' + JSON.stringify(assignments));
  if (assignments[0].asset !== assignments[1].asset) throw new Error('split paths should retain the same texture asset');
  return 'both split paths inherited the original texture assignment';
});

await visit('editor: import mesh, move, rail, export', 'editor.html', async (page) => {
  await page.setInputFiles('#meshFileInput', { name: 'pad.json', mimeType: 'application/json', buffer: Buffer.from(meshFile) });
  await page.waitForTimeout(400);

  const imported = await page.evaluate(() => ({
    assets: Object.keys(window.__editor.track.meshAssets ?? {}),
    placements: (window.__editor.track.meshes ?? []).length
  }));
  if (imported.placements !== 1) throw new Error('expected 1 placement, got ' + JSON.stringify(imported));

  // Import rails every rim edge, so a fresh region is enclosed and drivable
  // immediately: 4 outer + 4 hole edges here, with no interior seams.
  const railedOnImport = await page.evaluate(() => window.__editor.railCount(window.__editor.track.meshes[0].asset));
  if (railedOnImport !== 8) throw new Error('expected 8 railed edges on import, got ' + railedOnImport);

  // Rails mode: click the midpoint of the pad's south edge. Since import
  // railed it, the click toggles it OFF -- that is how you open a ledge.
  await page.selectOption('#editModeSelect', 'rails');
  await page.waitForTimeout(200);
  const pt = await page.evaluate(() => {
    const p = window.__editor.track.meshes[0];
    const w = { x: p.x + 15, z: p.z + 0 };
    const s = window.__editor.worldToScreen(w.x, w.z);
    const r = document.getElementById('topCanvas').getBoundingClientRect();
    return { x: s.x + r.left, y: s.y + r.top };
  });
  await page.mouse.click(pt.x, pt.y);
  await page.waitForTimeout(300);

  const railed = await page.evaluate(() => window.__editor.railCount(window.__editor.track.meshes[0].asset));
  if (railed !== 7) throw new Error('expected 7 railed edges after unrailing one, got ' + railed);

  const json = await page.evaluate(() => window.TrackCore.serializeTrack(window.__editor.track));
  if (!json.includes('"meshAssets"') || !json.includes('"rail"')) throw new Error('export missing mesh/rail data');
  return `asset=${imported.assets[0]}, rails ${railedOnImport}->${railed}, export ${json.length}b`;
});

// Regression: opening the context menu must not read the clipboard.
//
// An ungranted readText() pops the browser's OWN native "Paste" confirmation
// bubble. Probing on right-click therefore means a system popup nobody asked
// for, layered over our menu, every single time -- plus a second one when the
// paste actually runs. This page deliberately has no clipboard permission,
// which is what a real first-time user has; granting it in the test would
// hide the very thing being tested.
await visit('editor: right-click does not read the clipboard', 'editor.html', async (page) => {
  await page.addInitScript(() => {
    window.__reads = 0;
    const orig = navigator.clipboard.readText.bind(navigator.clipboard);
    navigator.clipboard.readText = () => { window.__reads++; return orig(); };
  });
  await page.reload({ waitUntil: 'networkidle' });
  await page.waitForTimeout(400);

  const state = await page.evaluate(async () => {
    try { return (await navigator.permissions.query({ name: 'clipboard-read' })).state; }
    catch { return 'unavailable'; }
  });
  if (state === 'granted') throw new Error('this test proves nothing with clipboard permission already granted');

  const box = await page.locator('#topCanvas').boundingBox();
  await page.mouse.click(Math.round(box.x + box.width * 0.75), Math.round(box.y + box.height * 0.25), { button: 'right' });
  await page.waitForTimeout(400);

  const shown = (id) => page.evaluate(i => getComputedStyle(document.getElementById(i)).display !== 'none', id);
  if (!await shown('addPointMenu')) throw new Error('right-click did not open the add-point menu');
  // Un-prompted permission still offers the option -- it just does not probe.
  if (!await shown('pasteMeshSection')) throw new Error('paste option should still be offered when permission is un-prompted');

  const afterMenu = await page.evaluate(() => window.__reads);
  if (afterMenu !== 0) throw new Error(`opening the menu read the clipboard ${afterMenu}x, should be 0`);

  // Choosing it reads exactly once: the single prompt the user actually asked for.
  await page.locator('#pasteMeshSection button').click();
  await page.waitForTimeout(500);
  const afterPaste = await page.evaluate(() => window.__reads);
  if (afterPaste !== 1) throw new Error(`pasting read the clipboard ${afterPaste}x, should be 1`);

  return `menu 0 reads, paste 1 read (permission: ${state})`;
});

await visit('game: player remains at its starting-grid pose while parked', 'track.html', async (page) => {
  await page.evaluate(() => {
    localStorage.setItem('web3d.currentTrack',
      window.TrackCore.serializeTrack(window.TrackCore.cloneTrack(window.TrackCore.DEFAULT_TRACK)));
  });
  await page.reload({ waitUntil: 'networkidle' });
  await page.waitForTimeout(1200);
  const displacement = await page.evaluate(() => {
    const ship = window.__game.ships[0];
    const delta = ship.physics.groundPos.clone().sub(ship.startPose.pos);
    const rendered = ship.group.position.clone().addScaledVector(ship.startPose.up, -1).sub(ship.startPose.pos);
    return { along: delta.dot(ship.startPose.forward), renderedAlong: rendered.dot(ship.startPose.forward), speed: ship.physics.speed };
  });
  if (displacement.speed !== 0) throw new Error(`parked player gained speed ${displacement.speed}`);
  if (Math.abs(displacement.along) > 0.01) throw new Error(`parked player drifted ${displacement.along.toFixed(3)} m along the track`);
  if (Math.abs(displacement.renderedAlong) > 0.01) throw new Error(`rendered player drifted ${displacement.renderedAlong.toFixed(3)} m along the track`);
  return 'physics and rendered pose remained fixed at zero speed';
});

await visit('game: creates one player and seven independently simulated idle AI ships', 'track.html', async (page) => {
  await page.evaluate(() => {
    localStorage.setItem('web3d.currentTrack',
      window.TrackCore.serializeTrack(window.TrackCore.cloneTrack(window.TrackCore.DEFAULT_TRACK)));
  });
  await page.reload({ waitUntil: 'networkidle' });
  await page.waitForTimeout(500);
  const before = await page.evaluate(() => window.__game.ships.map(s => ({
    id: s.id, kind: s.controllerKind, color: s.color,
    pos: s.physics.groundPos.toArray(), groupPos: s.group.position.toArray(),
    bodyColor: s.group.children[0].material.color.getHex()
  })));
  if (before.length !== 8) throw new Error('expected 8 ships, got ' + before.length);
  if (before[0].id !== 'player' || before[0].kind !== 'player') throw new Error('first ship is not the player');
  if (before.slice(1).some((s, i) => s.id !== `ai-${i + 1}` || s.kind !== 'ai')) throw new Error('AI ids/controller kinds are wrong');
  if (new Set(before.slice(1).map(s => s.color)).size !== 7) throw new Error('AI colours are not unique');
  if (before.some(s => s.color !== s.bodyColor)) throw new Error('runtime colour does not match rendered hull');

  await page.evaluate(() => window.dispatchEvent(new KeyboardEvent('keydown', { code: 'KeyW' })));
  await page.waitForTimeout(2000);
  await page.evaluate(() => window.dispatchEvent(new KeyboardEvent('keyup', { code: 'KeyW' })));
  const after = await page.evaluate(() => window.__game.ships.map(s => ({
    pos: s.physics.groundPos.toArray(), groupPos: s.group.position.toArray()
  })));
  const moved = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
  if (moved(before[0].pos, after[0].pos) < 5) throw new Error('player controls did not move the player');
  for (let i = 1; i < after.length; i++) {
    const distance = moved(before[i].pos, after[i].pos);
    if (distance > 0.1) throw new Error(`${before[i].id} physics moved ${distance.toFixed(3)} m under player input`);
    const visualDistance = moved(before[i].groupPos, after[i].groupPos);
    if (visualDistance > 0.001) throw new Error(`${before[i].id} rendered group moved ${visualDistance.toFixed(3)} m while idle`);
  }
  return '8 ships, unique colours, AI remained idle';
});

await visit('game: zones and triggers keep independent per-ship state', 'track.html', async (page) => {
  await page.evaluate(() => {
    const t = window.TrackCore.cloneTrack(window.TrackCore.STARTER_TRACK);
    t.zones = [{ id: 'player-zone', effect: 'startGrid', width: 2, length: 4,
      host: { kind: 'path', pathId: 'starter-path', t: 0, lateral: -2.5 } }];
    localStorage.setItem('web3d.currentTrack', window.TrackCore.serializeTrack(t));
  });
  await page.reload({ waitUntil: 'networkidle' });
  await page.waitForTimeout(400);
  const zones = await page.evaluate(() => window.__game.ships.map(s => ({ id: s.id, inside: s.zoneInside.get('player-zone') })));
  if (!zones[0].inside) throw new Error('player did not independently enter its start zone');
  if (zones.slice(1).some(s => s.inside)) throw new Error('an AI ship inherited the player zone state: ' + JSON.stringify(zones));

  await page.evaluate(() => {
    const game = window.__game, ai = game.ships[1];
    const finish = game.triggers.find(t => t.role === 'finish');
    ai.prevTriggerPos.copy(finish.center).addScaledVector(finish.fwd, -1);
    ai.physics.groundPos.copy(finish.center).addScaledVector(finish.fwd, 1);
    ai.physics.visualGroundPos.copy(ai.physics.groundPos);
  });
  await page.waitForTimeout(150);
  const triggerState = await page.evaluate(() => {
    const game = window.__game, finish = game.triggers.find(t => t.role === 'finish');
    return {
      playerArmed: game.ships[0].triggerStates.get(finish.id).armed,
      playerCheckpoint: game.ships[0].lastCheckpoint.valid,
      aiArmed: game.ships[1].triggerStates.get(finish.id).armed,
      aiCheckpoint: game.ships[1].lastCheckpoint.valid
    };
  });
  if (!triggerState.aiCheckpoint || triggerState.aiArmed) throw new Error('AI trigger state did not fire independently: ' + JSON.stringify(triggerState));
  if (triggerState.playerCheckpoint || !triggerState.playerArmed) throw new Error('AI crossing changed player trigger state: ' + JSON.stringify(triggerState));
  return 'zone and checkpoint state remained ship-local';
});

await visit('track.html loads and builds a mesh region', 'track.html', async (page) => {
  await page.evaluate((mesh) => {
    const t = window.TrackCore.cloneTrack(window.TrackCore.DEFAULT_TRACK);
    t.meshAssets = { pad: { name: 'pad', railHeight: 3, mesh: JSON.parse(mesh) } };
    t.meshes = [{ id: 'm1', asset: 'pad', x: 600, z: 0, rotation: 0, elevation: 0 }];
    localStorage.setItem('web3d.currentTrack', window.TrackCore.serializeTrack(t));
  }, meshFile);
  await page.reload({ waitUntil: 'networkidle' });
  await page.waitForTimeout(1500);
  const n = await page.evaluate(() => window.__game.meshRegions.length);
  if (n !== 1) throw new Error('expected 1 mesh region in the scene, got ' + n);
  return `${n} region built, ship y=${(await page.evaluate(() => window.__game.physics.groundPos.y)).toFixed(2)}`;
});


// Physics: park the ship on a railed pad far from the ribbon, drive into a
// rail, and confirm it is held on the flat surface instead of falling off.
await visit('game: drives on a mesh region and is stopped by rails', 'track.html', async (page) => {
  await page.evaluate((mesh) => {
    const m = JSON.parse(mesh);
    for (const e of m.edges) e.attributes = { rail: true };
    const t = window.TrackCore.cloneTrack(window.TrackCore.DEFAULT_TRACK);
    t.meshAssets = { pad: { name: 'pad', railHeight: 3, mesh: m } };
    // Far from the ribbon so the corridor cannot claim the ship.
    t.meshes = [{ id: 'm1', asset: 'pad', x: 600, z: 0, rotation: 0, elevation: 5 }];
    localStorage.setItem('web3d.currentTrack', window.TrackCore.serializeTrack(t));
  }, meshFile);
  await page.reload({ waitUntil: 'networkidle' });
  await page.waitForTimeout(800);

  const rails = await page.evaluate(() => window.__game.meshRegions[0].compiled.rails.length);
  if (rails !== 8) throw new Error('expected 8 railed edges, got ' + rails);

  // Drop the ship in the middle of the pad heading toward the south rail (-Z).
  await page.evaluate(() => {
    const p = window.__game.physics;
    p.groundPos.set(605, 5, 5);
    p.airborne = false; p.verticalVel = 0;
    p.heading = Math.PI; p.forward.set(0, 0, -1); p.moveDir.copy(p.forward); p.speed = 60;
  });
  await page.waitForTimeout(1500);

  const after = await page.evaluate(() => {
    const p = window.__game.physics;
    const r = window.__game.meshRegions[0];
    return {
      x: p.groundPos.x, y: p.groundPos.y, z: p.groundPos.z,
      airborne: p.airborne,
      inside: window.__game.meshRegions.length === 1 &&
        r.compiled.polygons[0].outer.length > 0
    };
  });
  if (after.airborne) throw new Error('ship fell off a RAILED pad: ' + JSON.stringify(after));
  if (Math.abs(after.y - 5) > 1e-6) throw new Error('ship left the flat surface, y=' + after.y);
  if (after.z < 0.5) throw new Error('ship pushed through the south rail, z=' + after.z);
  if (after.z > 4) throw new Error('ship never reached the rail, z=' + after.z);
  return `held at y=${after.y.toFixed(2)}, z=${after.z.toFixed(2)} (rail stopped it)`;
});

// Same pad with NO rails: driving off the edge must become a fall, then the
// auto-respawn must recover at the authored start (no checkpoint was crossed).
await visit('game: bare edge is a ledge, and respawn recovers', 'track.html', async (page) => {
  await page.evaluate((mesh) => {
    const t = window.TrackCore.cloneTrack(window.TrackCore.DEFAULT_TRACK);
    t.meshAssets = { pad: { name: 'pad', railHeight: 3, mesh: JSON.parse(mesh) } };
    t.meshes = [{ id: 'm1', asset: 'pad', x: 600, z: 0, rotation: 0, elevation: 5 }];
    localStorage.setItem('web3d.currentTrack', window.TrackCore.serializeTrack(t));
  }, meshFile);
  await page.reload({ waitUntil: 'networkidle' });
  await page.waitForTimeout(800);

  if (await page.evaluate(() => window.__game.meshRegions[0].compiled.rails.length) !== 0)
    throw new Error('expected no rails on this pad');

  const start = await page.evaluate(() => {
    const p = window.__game.physics.groundPos;
    return { x: p.x, y: p.y, z: p.z };
  });
  await page.evaluate(() => {
    const p = window.__game.physics;
    p.groundPos.set(605, 5, 5);
    p.airborne = false; p.verticalVel = 0;
    p.heading = Math.PI; p.forward.set(0, 0, -1); p.moveDir.copy(p.forward); p.speed = 60;
  });
  await page.waitForTimeout(400);
  const mid = await page.evaluate(() => ({ airborne: window.__game.physics.airborne, y: window.__game.physics.groundPos.y }));
  if (!mid.airborne && mid.y === 5) throw new Error('ship never left the bare edge: ' + JSON.stringify(mid));

  // Let it fall past the respawn threshold and confirm it is recovered.
  await page.waitForTimeout(4000);
  const after = await page.evaluate(() => {
    const p = window.__game.physics;
    return { airborne: p.airborne, x: p.groundPos.x, y: p.groundPos.y, z: p.groundPos.z };
  });
  if (after.y < -100) throw new Error('never respawned, y=' + after.y);
  if (Math.hypot(after.x - start.x, after.y - start.y, after.z - start.z) > 1)
    throw new Error('respawn should return to the authored start before any checkpoint: ' + JSON.stringify({ start, after }));
  if (after.airborne) throw new Error('still airborne after respawn');
  return `fell off bare edge, respawned at the authored start y=${after.y.toFixed(2)}`;
});


// A hole is a void: parked over one, the ship must not be supported.
await visit('game: a hole in a mesh region is not drivable', 'track.html', async (page) => {
  await page.evaluate((mesh) => {
    const m = JSON.parse(mesh);
    for (const e of m.edges) e.attributes = { rail: true };
    const t = window.TrackCore.cloneTrack(window.TrackCore.DEFAULT_TRACK);
    t.meshAssets = { pad: { name: 'pad', railHeight: 3, mesh: m } };
    t.meshes = [{ id: 'm1', asset: 'pad', x: 600, z: 0, rotation: 0, elevation: 5 }];
    localStorage.setItem('web3d.currentTrack', window.TrackCore.serializeTrack(t));
  }, meshFile);
  await page.reload({ waitUntil: 'networkidle' });
  await page.waitForTimeout(800);

  const onPad = await page.evaluate(() => window.__game.meshRegions[0].compiled.polygons[0].holes.length);
  if (onPad !== 1) throw new Error('expected the pad to have 1 hole, got ' + onPad);

  // local (5,5) is solid pad; local (15,15) is the middle of the hole.
  const solid = await page.evaluate(() => {
    const r = window.__game.meshRegions[0];
    return { pad: r.compiled.polygons[0].outer.length > 0 };
  });
  const supported = await page.evaluate(() => {
    const p = window.__game.physics;
    p.groundPos.set(605, 5, 5); p.airborne = false; p.verticalVel = 0; p.speed = 0;
    return true;
  });
  await page.waitForTimeout(300);
  const onSolid = await page.evaluate(() => window.__game.physics.groundPos.y);
  if (Math.abs(onSolid - 5) > 1e-6) throw new Error('solid part of pad should support the ship, y=' + onSolid);

  await page.evaluate(() => {
    const p = window.__game.physics;
    p.groundPos.set(615, 5, 15); p.airborne = false; p.verticalVel = 0; p.speed = 0;
  });
  await page.waitForTimeout(500);
  const overHole = await page.evaluate(() => window.__game.physics.groundPos.y);
  if (Math.abs(overHole - 5) < 1e-6) throw new Error('hole must not support the ship, y=' + overHole);
  return `solid y=${onSolid.toFixed(2)}, over hole y=${overHole.toFixed(2)} (fell through)`;
});


// Regression: a plain mesh-free track must drive exactly as before, including
// staying grounded through the ribbon's hills and banking.
await visit('regression: mesh-free track still drives normally', 'track.html', async (page) => {
  await page.evaluate(() => {
    localStorage.setItem('web3d.currentTrack',
      window.TrackCore.serializeTrack(window.TrackCore.cloneTrack(window.TrackCore.DEFAULT_TRACK)));
  });
  await page.reload({ waitUntil: 'networkidle' });
  await page.waitForTimeout(800);
  if (await page.evaluate(() => window.__game.meshRegions.length) !== 0)
    throw new Error('expected no mesh regions');

  const start = await page.evaluate(() => {
    const p = window.__game.physics;
    return { x: p.groundPos.x, z: p.groundPos.z };
  });

  // Hold the throttle for a few seconds and make sure it laps without falling.
  await page.evaluate(() => window.dispatchEvent(new KeyboardEvent('keydown', { code: 'KeyW' })));
  let everAirborne = false;
  for (let i = 0; i < 25; i++) {
    await page.waitForTimeout(120);
    if (await page.evaluate(() => window.__game.physics.airborne)) everAirborne = true;
  }
  await page.evaluate(() => window.dispatchEvent(new KeyboardEvent('keyup', { code: 'KeyW' })));

  const end = await page.evaluate(() => {
    const p = window.__game.physics;
    return { x: p.groundPos.x, y: p.groundPos.y, z: p.groundPos.z, speed: p.speed, airborne: p.airborne };
  });
  if (everAirborne) throw new Error('ship left the ribbon on a plain track');
  if (end.speed < 10) throw new Error('ship did not accelerate, speed=' + end.speed);
  const travelled = Math.hypot(end.x - start.x, end.z - start.z);
  if (travelled < 20) throw new Error('ship barely moved, travelled=' + travelled.toFixed(1));
  // DEFAULT_TRACK spans roughly +/-1700 in current units; far outside means it flew off.
  if (Math.hypot(end.x, end.z) > 2200) throw new Error('ship left the track area: ' + JSON.stringify(end));
  // "Keep the feel" means the HUD readout is unchanged by the unit rescale:
  // raw speed doubled, so its display factor was halved to compensate.
  // Read both in one evaluate: sampling them separately skews while decelerating.
  const { kmh, raw } = await page.evaluate(() => ({
    kmh: parseInt(document.getElementById('speed').textContent, 10),
    raw: Math.abs(window.__game.physics.speed)
  }));
  const expected = Math.round(raw * 3.6);
  if (Math.abs(kmh - expected) > 2) throw new Error(`HUD ${kmh} != expected ${expected} for raw ${raw}`);
  return `lapped ${travelled.toFixed(0)}u at ${end.speed.toFixed(0)} u/s, HUD ${kmh} km/h, never airborne`;
});


// An OPEN curve's end must launch the ship ballistically. Regression test for a
// bug where lateral-only containment let a far-back segment claim a point that
// was past the end, so `best` was never the terminal segment, offEnd never
// fired, and the ship was reprojected backwards instead of flying off.
const openTrack = {
  version: 5, name: 'Open Straight',
  start: { path: 0, point: 0, reverse: false },
  disjointSeams: [], junctions: [], meshAssets: {}, meshes: [],
  paths: [{
    id: 'p1', closed: false,
    points: [
      { type: 'position', id: 'a', pos: [0, 0, -200], weight: 1 },
      { type: 'position', id: 'b', pos: [0, 0, -100], weight: 1 },
      { type: 'position', id: 'c', pos: [0, 0, 0], weight: 1 },
      { type: 'position', id: 'd', pos: [0, 0, 100], weight: 1 },
      { type: 'position', id: 'e', pos: [0, 0, 200], weight: 1 },
      { type: 'roll', t: 0, roll: 0 }, { type: 'roll', t: 1, roll: 0 },
      { type: 'width', t: 0, width: 24 }, { type: 'width', t: 1, width: 24 }
    ]
  }]
};

await visit('game: spline guard rails reflect velocity instead of sticking the ship', 'track.html', async (page) => {
  await page.evaluate((t) => localStorage.setItem('web3d.currentTrack', JSON.stringify(t)), openTrack);
  await page.reload({ waitUntil: 'networkidle' });
  await page.waitForTimeout(300);
  await page.evaluate(() => {
    const p = window.__game.physics;
    p.groundPos.set(0, 0, 0); p.visualGroundPos.copy(p.groundPos);
    p.airborne = false; p.verticalVel = 0;
    p.forward.set(Math.SQRT1_2, 0, Math.SQRT1_2); p.moveDir.copy(p.forward); p.speed = 80;
  });
  const samples = [];
  for (let i = 0; i < 45; i++) {
    await page.waitForTimeout(12);
    samples.push(await page.evaluate(() => {
      const p = window.__game.physics;
      return { x: p.groundPos.x, z: p.groundPos.z, moveX: p.moveDir.x };
    }));
  }
  if (Math.max(...samples.map(s => s.x)) < 9) throw new Error('ship never reached the guard rail');
  if (Math.min(...samples.map(s => s.moveX)) > -0.45) throw new Error('rail bounce did not send the ship strongly enough toward the reflection vector');
  if (Math.max(...samples.map(s => s.z)) < 15) throw new Error('ship stuck at the rail instead of continuing after impact');
  return 'velocity strongly reflected and motion continued after impact';
});

async function driveOffEnd(page, { fromZ, heading, expectBeyond }) {
  await page.evaluate(({ fromZ, heading }) => {
    const p = window.__game.physics;
    p.groundPos.set(0, 0, fromZ);
    p.airborne = false; p.verticalVel = 0; p.landingBounce = 0; p.landingBounceVel = 0;
    p.heading = heading; p.forward.set(Math.sin(heading), 0, Math.cos(heading)); p.moveDir.copy(p.forward); p.speed = 60;
  }, { fromZ, heading });
  await page.evaluate(() => window.dispatchEvent(new KeyboardEvent('keydown', { code: 'KeyW' })));
  let launched = false, maxReach = fromZ;
  for (let i = 0; i < 14 && !launched; i++) {
    await page.waitForTimeout(120);
    const st = await page.evaluate(() => {
      const p = window.__game.physics;
      return { air: p.airborne, z: p.groundPos.z, y: p.groundPos.y };
    });
    maxReach = expectBeyond > 0 ? Math.max(maxReach, st.z) : Math.min(maxReach, st.z);
    if (st.air) launched = true;
  }
  await page.evaluate(() => window.dispatchEvent(new KeyboardEvent('keyup', { code: 'KeyW' })));
  return { launched, maxReach };
}

await visit('game: a ship can fly off the end of an open curve', 'track.html', async (page) => {
  await page.evaluate((t) => localStorage.setItem('web3d.currentTrack', JSON.stringify(t)), openTrack);
  await page.reload({ waitUntil: 'networkidle' });
  await page.waitForTimeout(700);

  const far = await driveOffEnd(page, { fromZ: 170, heading: 0, expectBeyond: 1 });
  if (!far.launched) throw new Error(`ship never left the far end (reached z=${far.maxReach.toFixed(1)} of 200)`);

  // And it must keep going, not be snapped back onto the ribbon.
  await page.waitForTimeout(700);
  const after = await page.evaluate(() => {
    const p = window.__game.physics;
    return { air: p.airborne, z: p.groundPos.z, y: p.groundPos.y };
  });
  if (!after.air) throw new Error('ship was recaptured by the track after leaving');
  if (after.z <= 200) throw new Error('ship did not travel past the end, z=' + after.z);
  if (after.y >= 0) throw new Error('ship is not falling, y=' + after.y);

  // The START end must launch too (driving backwards off point `a`).
  const near = await driveOffEnd(page, { fromZ: -170, heading: Math.PI, expectBeyond: -1 });
  if (!near.launched) throw new Error(`ship never left the start end (reached z=${near.maxReach.toFixed(1)} of -200)`);

  return `launched off both ends, then fell to y=${after.y.toFixed(1)} at z=${after.z.toFixed(0)}`;
});

await browser.close();
server.close();
console.log(failures ? `\n${failures} page(s) failed` : '\nall pages clean');
process.exit(failures ? 1 : 0);
