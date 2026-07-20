import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

// track-core.js is deliberately a classic browser script (an IIFE assigning
// window.TrackCore), so it is evaluated against a stand-in window rather than
// imported. See CLAUDE.md.
function loadTrackCore() {
  const src = readFileSync(new URL('../track-core.js', import.meta.url), 'utf8');
  const fakeWindow = {};
  new Function('window', src)(fakeWindow);
  return fakeWindow.TrackCore;
}
const TrackCore = loadTrackCore();

const N = TrackCore.N_DEFAULT;   // the sample count the game and editor really use
const finitePoint = p => !!p && Number.isFinite(p.x) && Number.isFinite(p.y) && Number.isFinite(p.z);

function bake(raw, samples = N) {
  const track = TrackCore.parseTrack(JSON.stringify(raw));
  const path = track.paths[0];
  const parts = TrackCore.splitPoints(path.points);
  const closed = path.closed !== false;
  const frames = TrackCore.buildCenterline(
    parts.controlPoints, samples, closed, parts.rollPoints, parts.widthPoints, parts.crossSectionPoints);
  return { frames, closed };
}

/* An OPEN curve whose last two control points very nearly coincide, on a road
 * wide enough that the inner edge is still folded back on itself at the FINAL
 * segment. That is an ordinary authoring slip -- dragging a point onto its
 * neighbour, or a stray click while extending a curve -- and it used to throw
 * straight out of buildEdges:
 *
 *   trimEdge scans forward for the run of folded segments with
 *   `while (len < segCount && !fwd[i + len])`. For an open path the index is a
 *   bare `i + len`, so once it passes the end fwd[] yields undefined, and
 *   !undefined is true -- the scan ran on until len === segCount, `e` overshot
 *   the last segment, and pts[nextIdx(e)] was undefined.
 *
 * Nothing caught it: the game builds its scene at module scope and the editor
 * bakes edges inside drawTop, so a track shaped like this blanked the game and
 * froze the editor. Closed paths were always immune -- they index fwd[] modulo
 * segCount -- which is why the bound has to be on `i + len`, not `len`. */
const foldedTail = () => ({
  version: 5, name: 'folded tail',
  paths: [{
    id: 'a', closed: false, points: [
      { type: 'position', id: 'p1', pos: [-214, 0, -205], weight: 1 },
      { type: 'position', id: 'p2', pos: [-210, 0, 193], weight: 1 },
      { type: 'position', id: 'p3', pos: [102, 0, -40], weight: 5 },
      { type: 'position', id: 'p4', pos: [-225, 0, -60], weight: 1 },
      { type: 'position', id: 'p5', pos: [-227, 0, -61], weight: 1 },   // ~2 units from p4
      { type: 'width', t: 0, width: 115 },
      { type: 'width', t: 1, width: 85 }
    ]
  }]
});

test('an open curve still folded at its final segment builds edges', () => {
  const { frames, closed } = bake(foldedTail());
  const edges = TrackCore.buildEdges(frames, closed);   // used to throw
  assert.equal(edges.left.length, frames.length, 'one left edge point per frame');
  assert.equal(edges.right.length, frames.length, 'one right edge point per frame');
  assert.ok(edges.left.every(finitePoint), 'left edge points are finite');
  assert.ok(edges.right.every(finitePoint), 'right edge points are finite');
});

test('the folded tail survives self-intersection cleanup too', () => {
  const { frames, closed } = bake(foldedTail());
  const cleaned = TrackCore.removeLocalEdgeSelfIntersections(TrackCore.buildEdges(frames, closed), closed, false);
  assert.ok(cleaned.left.every(finitePoint) && cleaned.right.every(finitePoint));
  assert.equal(cleaned.left.length, frames.length);
});

test('the same shape closed builds edges as well', () => {
  // Closed paths took a different index path through trimEdge and were never
  // broken; pinned so a future change to the bound cannot regress them.
  const raw = foldedTail();
  raw.paths[0].closed = true;
  const { frames, closed } = bake(raw);
  const edges = TrackCore.buildEdges(frames, closed);
  assert.equal(edges.left.length, frames.length);
  assert.ok(edges.left.every(finitePoint) && edges.right.every(finitePoint));
});

/* One fixture only pins one shape. Edge trimming is fiddly index arithmetic
 * over folded runs, and the failure above was found by generating tracks rather
 * than by reading, so the sweep is the part that actually guards the class.
 * Seeded, so it is deterministic. */
test('generated tracks bake edges without throwing', () => {
  let seed = 7919;
  const rnd = () => (seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296;
  const range = (lo, hi) => lo + rnd() * (hi - lo);

  let built = 0, open = 0, closedCount = 0;
  for (let i = 0; i < 600; i++) {
    const closed = rnd() < 0.5;
    const cpCount = 4 + Math.floor(rnd() * 9);
    const points = [];
    for (let k = 0; k < cpCount; k++) {
      const angle = (k / cpCount) * Math.PI * 2;
      const radius = range(5, 400);                    // wildly varying curvature
      points.push({
        type: 'position', id: 'p' + k,
        pos: [Math.cos(angle) * radius, range(-60, 60), Math.sin(angle) * radius],
        weight: range(0.01, 8)
      });
    }
    const scalarCount = 1 + Math.floor(rnd() * 5);
    for (let k = 0; k < scalarCount; k++) {
      const t = closed ? k / scalarCount : k / Math.max(1, scalarCount - 1);
      points.push({ type: 'width', t, width: range(1, 120) });   // roads wider than the turns
    }

    let baked;
    try { baked = bake({ version: 5, paths: [{ id: 'a', closed, points }] }, 120); }
    catch { continue; }   // rejected by the schema, not our concern here
    built++;
    closed ? closedCount++ : open++;

    const label = `${closed ? 'closed' : 'open'} track #${i}`;
    const edges = TrackCore.buildEdges(baked.frames, baked.closed);
    assert.equal(edges.left.length, baked.frames.length, `${label}: left edge length`);
    assert.equal(edges.right.length, baked.frames.length, `${label}: right edge length`);
    assert.ok(edges.left.every(finitePoint), `${label}: finite left edge`);
    assert.ok(edges.right.every(finitePoint), `${label}: finite right edge`);
  }
  // Guard the guard: a sweep that silently built nothing would pass vacuously.
  assert.ok(built > 400, `expected most generated tracks to build, got ${built}`);
  assert.ok(open > 100 && closedCount > 100, `expected both kinds, got ${open} open / ${closedCount} closed`);
});
