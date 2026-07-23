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

await visit('editor.html loads', 'editor.html');

await visit('editor: texture file dialog saves a reference, not embedded image data', 'editor.html', async (page) => {
  await page.click('#texturesBtn');
  await page.setInputFiles('#textureFileInput', join(ROOT, 'assets/test-1.png'));
  await page.waitForTimeout(300);
  const result = await page.evaluate(() => {
    const assets = Object.values(window.__editor.track.textureAssets || {});
    const json = window.TrackCore.serializeTrack(window.__editor.track);
    return { assets, json };
  });
  if (result.assets.length !== 1 || result.assets[0].path !== 'test-1.png')
    throw new Error('file dialog selection did not store its filename reference: ' + JSON.stringify(result.assets));
  if ('dataUrl' in result.assets[0] || result.json.includes('data:image') || result.json.includes('dataUrl'))
    throw new Error('texture image data leaked into track JSON');
  return 'saved test-1.png without image bytes';
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
