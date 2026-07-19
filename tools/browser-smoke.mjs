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

await visit('editor: import mesh, move, rail, export', 'editor.html', async (page) => {
  await page.setInputFiles('#meshFileInput', { name: 'pad.json', mimeType: 'application/json', buffer: Buffer.from(meshFile) });
  await page.waitForTimeout(400);

  const imported = await page.evaluate(() => ({
    assets: Object.keys(window.__editor.track.meshAssets ?? {}),
    placements: (window.__editor.track.meshes ?? []).length
  }));
  if (imported.placements !== 1) throw new Error('expected 1 placement, got ' + JSON.stringify(imported));

  // Rails mode: click the midpoint of the pad's south edge.
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
  if (railed !== 1) throw new Error('expected 1 railed edge after click, got ' + railed);

  const json = await page.evaluate(() => window.TrackCore.serializeTrack(window.__editor.track));
  if (!json.includes('"meshAssets"') || !json.includes('"rail"')) throw new Error('export missing mesh/rail data');
  return `asset=${imported.assets[0]}, rails=${railed}, export ${json.length}b`;
});

await visit('track.html loads and builds a mesh region', 'track.html', async (page) => {
  await page.evaluate((mesh) => {
    const t = window.TrackCore.cloneTrack(window.TrackCore.DEFAULT_TRACK);
    t.meshAssets = { pad: { name: 'pad', railHeight: 3, mesh: JSON.parse(mesh) } };
    t.meshes = [{ id: 'm1', asset: 'pad', x: 60, z: 0, rotation: 0, elevation: 0 }];
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
    t.meshes = [{ id: 'm1', asset: 'pad', x: 300, z: 0, rotation: 0, elevation: 5 }];
    localStorage.setItem('web3d.currentTrack', window.TrackCore.serializeTrack(t));
  }, meshFile);
  await page.reload({ waitUntil: 'networkidle' });
  await page.waitForTimeout(800);

  const rails = await page.evaluate(() => window.__game.meshRegions[0].compiled.rails.length);
  if (rails !== 8) throw new Error('expected 8 railed edges, got ' + rails);

  // Drop the ship in the middle of the pad heading toward the south rail (-Z).
  await page.evaluate(() => {
    const p = window.__game.physics;
    p.groundPos.set(305, 5, 5);
    p.airborne = false; p.verticalVel = 0;
    p.heading = Math.PI; p.velocityAngle = Math.PI; p.speed = 60;
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
  if (after.z > 2) throw new Error('ship never reached the rail, z=' + after.z);
  return `held at y=${after.y.toFixed(2)}, z=${after.z.toFixed(2)} (rail stopped it)`;
});

// Same pad with NO rails: driving off the edge must become a fall, then the
// auto-respawn must recover it.
await visit('game: bare edge is a ledge, and respawn recovers', 'track.html', async (page) => {
  await page.evaluate((mesh) => {
    const t = window.TrackCore.cloneTrack(window.TrackCore.DEFAULT_TRACK);
    t.meshAssets = { pad: { name: 'pad', railHeight: 3, mesh: JSON.parse(mesh) } };
    t.meshes = [{ id: 'm1', asset: 'pad', x: 300, z: 0, rotation: 0, elevation: 5 }];
    localStorage.setItem('web3d.currentTrack', window.TrackCore.serializeTrack(t));
  }, meshFile);
  await page.reload({ waitUntil: 'networkidle' });
  await page.waitForTimeout(800);

  if (await page.evaluate(() => window.__game.meshRegions[0].compiled.rails.length) !== 0)
    throw new Error('expected no rails on this pad');

  await page.evaluate(() => {
    const p = window.__game.physics;
    p.groundPos.set(305, 5, 5);
    p.airborne = false; p.verticalVel = 0;
    p.heading = Math.PI; p.velocityAngle = Math.PI; p.speed = 60;
  });
  await page.waitForTimeout(400);
  const mid = await page.evaluate(() => ({ airborne: window.__game.physics.airborne, y: window.__game.physics.groundPos.y }));
  if (!mid.airborne && mid.y === 5) throw new Error('ship never left the bare edge: ' + JSON.stringify(mid));

  // Let it fall past the respawn threshold and confirm it is recovered.
  await page.waitForTimeout(2500);
  const after = await page.evaluate(() => ({ airborne: window.__game.physics.airborne, y: window.__game.physics.groundPos.y }));
  if (after.y < -100) throw new Error('never respawned, y=' + after.y);
  if (Math.abs(after.y - 5) > 1e-6) throw new Error('respawn should restore the last grounded spot on the pad, y=' + after.y);
  if (after.airborne) throw new Error('still airborne after respawn');
  return `fell off bare edge, respawned onto the pad at y=${after.y.toFixed(2)}`;
});


// A hole is a void: parked over one, the ship must not be supported.
await visit('game: a hole in a mesh region is not drivable', 'track.html', async (page) => {
  await page.evaluate((mesh) => {
    const m = JSON.parse(mesh);
    for (const e of m.edges) e.attributes = { rail: true };
    const t = window.TrackCore.cloneTrack(window.TrackCore.DEFAULT_TRACK);
    t.meshAssets = { pad: { name: 'pad', railHeight: 3, mesh: m } };
    t.meshes = [{ id: 'm1', asset: 'pad', x: 300, z: 0, rotation: 0, elevation: 5 }];
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
    p.groundPos.set(305, 5, 5); p.airborne = false; p.verticalVel = 0; p.speed = 0;
    return true;
  });
  await page.waitForTimeout(300);
  const onSolid = await page.evaluate(() => window.__game.physics.groundPos.y);
  if (Math.abs(onSolid - 5) > 1e-6) throw new Error('solid part of pad should support the ship, y=' + onSolid);

  await page.evaluate(() => {
    const p = window.__game.physics;
    p.groundPos.set(315, 5, 15); p.airborne = false; p.verticalVel = 0; p.speed = 0;
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
  // DEFAULT_TRACK spans roughly +/-95; anything far outside means it flew off.
  if (Math.hypot(end.x, end.z) > 140) throw new Error('ship left the track area: ' + JSON.stringify(end));
  return `lapped ${travelled.toFixed(0)}u at ${end.speed.toFixed(0)} km/h, never airborne`;
});

await browser.close();
server.close();
console.log(failures ? `\n${failures} page(s) failed` : '\nall pages clean');
process.exit(failures ? 1 : 0);
