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

/* crossSectionBreakpoints() drives adaptive cross-section tessellation in both
 * js/track-game.js and js/usd-export.js: it picks the v-breakpoints for one
 * ring by recursively bisecting wherever the true profile's chord-sagitta
 * deviation exceeds a fixed tolerance, capped at a fixed recursion depth. */
test('a flat cross-section collapses to just its two edges', () => {
  const breaks = TrackCore.crossSectionBreakpoints(0, 1, 24);
  assert.deepEqual(breaks, [0, 1]);
});

test('breakpoints always include both edges and the midpoint', () => {
  for (const [curvature, tightness] of [[1, 1], [-1, 1], [0.3, 2], [-0.6, 0.4]]) {
    const breaks = TrackCore.crossSectionBreakpoints(curvature, tightness, 20);
    assert.equal(breaks[0], 0);
    assert.equal(breaks.at(-1), 1);
    assert.ok(breaks.includes(0.5), `expected a midpoint breakpoint for curvature ${curvature}, tightness ${tightness}`);
  }
});

test('breakpoints are sorted and strictly increasing (no duplicate or out-of-order vertices)', () => {
  for (const [curvature, tightness] of [[1, 1], [1, 3], [-0.8, 0.3], [0.05, 1]]) {
    const breaks = TrackCore.crossSectionBreakpoints(curvature, tightness, 30);
    for (let i = 1; i < breaks.length; i++) assert.ok(breaks[i] > breaks[i - 1], `not strictly increasing at ${i}`);
  }
});

test('a larger curvature magnitude (taller profile, same shape) gets more breakpoints', () => {
  // curvature scales the profile's height linearly (crossSectionHeight), so a
  // larger magnitude bends through more world units at every v and needs more
  // subdivision to stay within the same absolute sagitta tolerance.
  let prevCount = 1;
  for (const curvature of [0.05, 0.2, 0.5, 1]) {
    const count = TrackCore.crossSectionBreakpoints(curvature, 1, 24).length;
    assert.ok(count >= prevCount, `expected curvature ${curvature} to need >= as many breakpoints as smaller curvatures, got ${count} < ${prevCount}`);
    prevCount = count;
  }
});

test('recursion is capped: breakpoint count stays bounded even for an extreme profile', () => {
  const breaks = TrackCore.crossSectionBreakpoints(1, 4, 400);   // huge chordWidth exaggerates sagitta everywhere
  // Base partition of 2 cells, capped at 5 levels of recursion each: at most
  // 2 * 2^5 = 64 cells, i.e. 65 breakpoints.
  assert.ok(breaks.length <= 65, `expected a hard cap, got ${breaks.length} breakpoints`);
});

test('every breakpoint below the recursion cap sits within a sagitta tolerance of the true profile', () => {
  // The whole point of the algorithm: no cell should deviate (at its midpoint)
  // from a straight line between its two ends by more than the tolerance --
  // UNLESS it has already been bisected down to the minimum cell width the
  // recursion cap allows, which is exactly what lets a steep edge (infinite
  // slope at v=0/1 for some tightness values) stay bounded instead of
  // recursing forever without ever satisfying the tolerance.
  const MIN_WIDTH = 0.5 / 2 ** 5;   // base half-cell (0.5), halved 5 times
  for (const [curvature, tightness] of [[1, 1], [1, 2.5], [-0.9, 0.6], [0.4, 3]]) {
    const chordWidth = 40;
    const breaks = TrackCore.crossSectionBreakpoints(curvature, tightness, chordWidth);
    const height = v => TrackCore.crossSectionHeight(curvature, tightness, v, chordWidth);
    for (let i = 1; i < breaks.length; i++) {
      const v0 = breaks[i - 1], v1 = breaks[i], vMid = (v0 + v1) / 2;
      if (v1 - v0 <= MIN_WIDTH + 1e-9) continue;   // hit the recursion cap
      const deviation = Math.abs(height(vMid) - (height(v0) + height(v1)) / 2);
      assert.ok(deviation < 0.15, `cell [${v0}, ${v1}] deviates by ${deviation} for curvature ${curvature}, tightness ${tightness}`);
    }
  }
});

test('a wider road (same curvature/tightness) needs at least as much subdivision', () => {
  // The sagitta tolerance is a fixed WORLD-UNIT constant, and curvature scales
  // the profile's height linearly with chordWidth (CLAUDE.md: curvature and
  // tightness are scale-invariant, but the resulting rise is still a length)
  // -- so a wider road bends through more world units for the same curvature,
  // and needs the same or more subdivision to stay within tolerance.
  const narrow = TrackCore.crossSectionBreakpoints(1, 2, 10);
  const wide = TrackCore.crossSectionBreakpoints(1, 2, 500);
  assert.ok(wide.length >= narrow.length, `expected wide >= narrow, got ${wide.length} vs ${narrow.length}`);
});

/* crossSectionStitchPoint() is what actually prevents the crack between two
 * differently-adaptive rings: a ring shared by two neighboring strips can be
 * asked for a "foreign" v neither its own adaptive pass, nor the OTHER
 * neighbor, chose. Evaluating the true analytic surface there (the obvious,
 * WRONG fix) draws that ring's edge differently depending on which neighbor
 * asked. The correct behaviour: a v the ring owns returns the exact point;
 * any other v is linearly interpolated between the ring's own two REAL
 * bracketing vertices, so the ring's rendered edge is always the one fixed
 * polyline defined solely by its own breakpoints. */
test('crossSectionStitchPoint returns the exact point for an owned breakpoint', () => {
  const pointAt = v => [v * 10, v * v, 0];
  const owned = [0, 0.5, 1];
  for (const v of owned) {
    assert.deepEqual(TrackCore.crossSectionStitchPoint(owned, v, pointAt), pointAt(v));
  }
});

test('crossSectionStitchPoint linearly interpolates a foreign v between its bracketing owned vertices, not the true surface', () => {
  // A curved pointAt (y = v^2) so the true surface value at v=0.25 differs
  // from a straight-line interpolation between v=0 and v=0.5.
  const pointAt = v => [v * 10, v * v, 0];
  const owned = [0, 0.5, 1];
  const stitched = TrackCore.crossSectionStitchPoint(owned, 0.25, pointAt);
  const trueSurface = pointAt(0.25);
  const lerped = [
    (pointAt(0)[0] + pointAt(0.5)[0]) / 2,
    (pointAt(0)[1] + pointAt(0.5)[1]) / 2,
    0
  ];
  assert.deepEqual(stitched, lerped);
  assert.notDeepEqual(stitched, trueSurface, 'expected the stitched point to differ from the true (curved) surface value');
});

test('crossSectionStitchPoint: a ring renders the same edge no matter which neighbor asks for extra points', () => {
  // Simulates the actual bug: ring i's own breakpoints are [0, 0.5, 1]. Its
  // left neighbor's strip asks ring i for v=0.25 (a foreign v); its right
  // neighbor's strip asks for v=0.75 (a different foreign v). Both requests
  // must be consistent with ring i's OWN fixed polyline -- in particular,
  // asking for 0.25 must not change what a request for v=0 or v=0.5 returns.
  const pointAt = v => [Math.sin(v * 5) * 20, Math.cos(v * 3) * 8, v * 2];
  const owned = [0, 0.5, 1];
  const fromLeftStrip = v => TrackCore.crossSectionStitchPoint(owned, v, pointAt);
  const fromRightStrip = v => TrackCore.crossSectionStitchPoint(owned, v, pointAt);
  for (const v of [0, 0.5, 1]) {
    assert.deepEqual(fromLeftStrip(v), fromRightStrip(v), `ring's own vertex at v=${v} must be identical regardless of context`);
  }
  // The foreign points themselves must be collinear with (not deviating from)
  // the segment of the ring's own polyline they fall inside.
  const a = pointAt(0), b = pointAt(0.5);
  const foreign = fromLeftStrip(0.25);
  const t = 0.5; // (0.25 - 0) / (0.5 - 0)
  assert.deepEqual(foreign, [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t]);
});

/* longitudinalBreakpoints() drives MESH-ONLY adaptive ring spacing along the
 * path (js/track-game.js's buildPath): denser on sharp turns/hills, sparser
 * on long straights. Physics never sees this -- it always bakes the fixed
 * TrackCore.buildCenterline frames. */
test('a straight, short span collapses to just its two ends', () => {
  const posAt = g => ({ x: g * 10, y: 0, z: 0 }); // dead straight
  const breaks = TrackCore.longitudinalBreakpoints(0, 1, posAt);
  assert.deepEqual(breaks, [0, 1]);
});

test('a straight but long span still gets a bounded max gap', () => {
  // Long enough that even zero sagitta must still be capped by
  // LONGITUDINAL_MAX_DISTANCE (40 world units).
  const posAt = g => ({ x: g * 1000, y: 0, z: 0 }); // 1000-unit straight
  const breaks = TrackCore.longitudinalBreakpoints(0, 1, posAt);
  assert.ok(breaks.length > 2, `expected subdivision from the distance cap alone, got ${breaks.length} breakpoints`);
  for (let i = 1; i < breaks.length; i++) {
    const a = posAt(breaks[i - 1]), b = posAt(breaks[i]);
    const gap = Math.hypot(b.x - a.x, b.y - a.y, b.z - a.z);
    assert.ok(gap <= TrackCore.LONGITUDINAL_MAX_DISTANCE + 1e-6, `gap ${gap} exceeds the max distance cap`);
  }
});

test('a curved span subdivides from sagitta even when well under the distance cap', () => {
  // A short circular arc: well under the 40-unit distance cap on its own, but
  // curved enough that a single chord would deviate from the true arc.
  const radius = 20;
  const posAt = g => ({ x: Math.cos(g) * radius, y: 0, z: Math.sin(g) * radius });
  const breaks = TrackCore.longitudinalBreakpoints(0, Math.PI / 2, posAt);
  assert.ok(breaks.length > 2, `expected sagitta-driven subdivision, got ${breaks.length} breakpoints`);
});

test('breakpoints are sorted, unique, and always include both ends', () => {
  const radius = 15;
  const posAt = g => ({ x: Math.cos(g) * radius, y: Math.sin(g * 2) * 3, z: Math.sin(g) * radius });
  const breaks = TrackCore.longitudinalBreakpoints(0.3, 2.1, posAt);
  assert.equal(breaks[0], 0.3);
  assert.equal(breaks.at(-1), 2.1);
  for (let i = 1; i < breaks.length; i++) assert.ok(breaks[i] > breaks[i - 1], `not strictly increasing at ${i}`);
});

/* buildAdaptiveMeshFrames() is the actual mesh-only consumer: it takes the
 * already-baked physics raw/edges and produces a separate, MESH-only
 * frame/edge array -- never fewer than the two endpoints, denser on curves,
 * sparser on straights, and byte-identical to physics at every frame a
 * self-intersection fold touched. */
function bakeFull(rawTrack, samples = N) {
  const track = TrackCore.parseTrack(JSON.stringify(rawTrack));
  const path = track.paths[0];
  const parts = TrackCore.splitPoints(path.points);
  const closed = path.closed !== false;
  const frames = TrackCore.buildCenterline(
    parts.controlPoints, samples, closed, parts.rollPoints, parts.widthPoints, parts.crossSectionPoints);
  const edges = TrackCore.buildEdges(frames, closed);
  return { parts, closed, frames, edges };
}

function longStraightTrack() {
  return {
    version: TrackCore.TRACK_SCHEMA_VERSION, name: 'long straight', samples: N,
    paths: [{
      closed: false,
      points: [
        { type: 'position', id: 'a', pos: [0, 0, 0], weight: 1 },
        { type: 'position', id: 'b', pos: [1000, 0, 0], weight: 1 },
        { type: 'position', id: 'c', pos: [2000, 0, 0], weight: 1 },
        { type: 'position', id: 'd', pos: [3000, 0, 0], weight: 1 },
        { type: 'width', t: 0, width: 20 }, { type: 'width', t: 1, width: 20 }
      ]
    }]
  };
}

test('a long straight uses fewer mesh frames than the fixed physics baking', () => {
  const { parts, closed, frames, edges } = bakeFull(longStraightTrack());
  const { frames: meshFrames } = TrackCore.buildAdaptiveMeshFrames(
    parts.controlPoints, closed, parts.rollPoints, parts.widthPoints, parts.crossSectionPoints, frames, edges);
  assert.ok(meshFrames.length < frames.length,
    `expected fewer mesh frames than the ${frames.length} physics frames, got ${meshFrames.length}`);
});

test('mesh frames and mesh edges stay the same length, and always include both endpoints exactly', () => {
  const { parts, closed, frames, edges } = bakeFull(longStraightTrack());
  const { frames: meshFrames, edges: meshEdges } = TrackCore.buildAdaptiveMeshFrames(
    parts.controlPoints, closed, parts.rollPoints, parts.widthPoints, parts.crossSectionPoints, frames, edges);
  assert.equal(meshFrames.length, meshEdges.left.length);
  assert.equal(meshFrames.length, meshEdges.right.length);
  assert.deepEqual(meshFrames[0].pos, frames[0].pos);
  assert.deepEqual(meshFrames.at(-1).pos, frames.at(-1).pos);
});

test('a self-intersection fold is preserved byte-for-byte in the mesh frames, never resampled', () => {
  const { parts, closed, frames, edges: rawEdges } = bakeFull(foldedTail());
  const edges = TrackCore.removeLocalEdgeSelfIntersections(rawEdges, closed, false);
  const { frames: meshFrames, edges: meshEdges } = TrackCore.buildAdaptiveMeshFrames(
    parts.controlPoints, closed, parts.rollPoints, parts.widthPoints, parts.crossSectionPoints, frames, edges);

  // Find the fold: at least one frame whose trimmed edge differs from the
  // plain untrimmed half-width offset (same test buildAdaptiveMeshFrames uses
  // internally to decide what must be preserved exactly).
  let foldIndices = 0;
  for (let i = 0; i < frames.length; i++) {
    const f = frames[i];
    const untrimmedLeftX = f.pos.x - f.edgeRight.x * f.halfW;
    if (Math.abs(edges.left[i].x - untrimmedLeftX) > 1e-6) {
      foldIndices++;
      // This exact frame must appear, unchanged, in the mesh output.
      const meshIdx = meshFrames.findIndex(mf => mf.pos === f.pos);
      assert.ok(meshIdx !== -1, `fold-affected frame ${i} was not carried through into the mesh frames`);
      assert.deepEqual(meshEdges.left[meshIdx], edges.left[i]);
      assert.deepEqual(meshEdges.right[meshIdx], edges.right[i]);
    }
  }
  assert.ok(foldIndices > 0, 'expected this fixture to actually contain a self-intersection fold');
});

test('a tightly curved closed loop gets MORE mesh frames than a coarse fixed physics baking', () => {
  // The opposite direction from the long-straight test: a sharp, fold-free
  // turn needs finer sampling than a coarse fixed baking to stay within the
  // sagitta tolerance, so adaptivity must add frames here, not remove them.
  const track = {
    version: TrackCore.TRACK_SCHEMA_VERSION, name: 'tight loop', samples: 8,
    paths: [{
      closed: true,
      points: [
        { type: 'position', pos: [30, 0, 0], weight: 1 },
        { type: 'position', pos: [0, 0, 30], weight: 1 },
        { type: 'position', pos: [-30, 0, 0], weight: 1 },
        { type: 'position', pos: [0, 0, -30], weight: 1 },
        { type: 'width', t: 0, width: 10 }, { type: 'width', t: 0.5, width: 10 }
      ]
    }]
  };
  const { parts, closed, frames, edges } = bakeFull(track, 8);
  const { frames: meshFrames } = TrackCore.buildAdaptiveMeshFrames(
    parts.controlPoints, closed, parts.rollPoints, parts.widthPoints, parts.crossSectionPoints, frames, edges);
  assert.ok(meshFrames.length > frames.length,
    `expected more mesh frames than the ${frames.length} coarse physics frames, got ${meshFrames.length}`);
});
