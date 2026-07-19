# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A browser-based, dependency-free (aside from three.js via CDN) racing track editor and driving game, built as plain HTML/JS with no build step. There is no root `package.json`, no bundler, no test runner for the top-level app — files are opened/served as static assets.

- `track.html` — the driving game. Loads three.js (CDN), `track-core.js`, `js/track-game.js`.
- `editor.html` — the 2D/elevation track editor UI. Loads `track-core.js`, `js/editor.js`.
- `index.html` — unrelated scratch demo (spinning cube in three.js), not part of the track app.
- `track-core.js` — shared track math, used by both the game and the editor (see architecture below).
- `js/track-game.js` — three.js scene, track mesh generation/rendering, car physics/controls, collisions.
- `js/editor.js` — editor state, undo/redo, canvas rendering/interaction for authoring tracks.
- `ext/geoemetry-js/` — a git submodule (`@willpower/geometry`, https://github.com/ajare/geoemetry-js), a separate self-contained ES-module mesh/geometry library with its own `package.json`, tests, and a React/Vite editor. It's linked into the root project as a local npm dependency (root `package.json` -> `"@willpower/geometry": "file:ext/geoemetry-js"`, installed via `npm install`, resolved as a symlink at `node_modules/@willpower/geometry`) so track code can `import` it as `@willpower/geometry`. See `ext/geoemetry-js/README.md` for its own commands (`npm test`, `npm --prefix editor/ui run dev`, etc.) and its own codebase map.

## Running / testing

No build step for the main app: open `track.html` or `editor.html` directly in a browser (or serve the repo root with any static file server). Run `npm install` once (after `git submodule update --init --recursive`) to link the `@willpower/geometry` local dependency. There is no lint or test command for the root project itself — verify changes manually in the browser.

For the `geoemetry-js` submodule specifically, its own commands apply (`npm test`, `npm --prefix ext/geoemetry-js/editor/ui ci/run dev/build`) — see `ext/geoemetry-js/README.md`. Don't assume those apply to the outer project.

## Architecture: track data model (`track-core.js`)

This is the core concept to understand before touching the game or editor — read the file header comment in `track-core.js` for the authoritative spec. Summary:

A **track** is `{ version, name, samples, paths: [...], disjointSeams: [...], start: {...} }`. Each **path** is either a closed loop or an open curve, and holds one ordered array of **typed control points**:

```
points: [
  { type: 'position', id: 'p1', pos: [x,y,z], weight },
  { type: 'roll',     t: 0..1, roll: <deg> },
  { type: 'width',    t: 0..1, width: <full width> },
  ...
]
```

- Each control-point type is independent — its own count, its own spacing — and only interacts with points of its own type.
- `position` points interpolate with a rational, uniformly-knotted cubic B-spline (NURBS); their order in the array *is* the path's shape sequence.
- `roll`/`width` points each interpolate with their own non-uniform Catmull-Rom/Hermite spline over their own `t` (a fraction of the path's parameter domain, independent of array order).
- All wrap for closed paths, clamp at the ends for open ones. Positive roll lifts the LEFT edge (banks into a right-hand turn).
- Use `TrackCore.splitPoints(path.points)` to get the three filtered, t-sorted arrays (`controlPoints`, `rollPoints`, `widthPoints`) that the math functions actually consume — these are filtered *views* (same objects, not copies), so holding onto one and later splicing it out of `points` is safe.
- Position point `id`s are stable editor identities. If the same position ID appears in multiple path occurrences, `parseTrack()` unifies them into the same in-memory object, so editing one moves every occurrence. `disjointSeams` lets the editor reverse hard-corner split/open operations; the game only needs point IDs plus seam pointIds to cut disjoint edges.
- `start: { path, point, reverse }` picks which position control point the player begins at (nearest baked sample) and facing direction relative to the path's natural parametric direction.

Public API exposed as `window.TrackCore`: `basis`/`basisDeriv` (B-spline basis + derivative), `splitPoints`, `makeEvaluator(cps, closed)` → `{ evalTrack(g), CP_N, closed }`, `buildCenterline`, `buildEdges`, `parseTrack`/`serializeTrack`, `cloneTrack`, `DEFAULT_TRACK`/`STARTER_TRACK`, `N_DEFAULT`.

Both `js/track-game.js` (3D mesh/physics) and `js/editor.js` (2D authoring UI) build on top of this same shared math so the editor's preview and the game's actual track geometry can never drift apart — if you change interpolation/eval behavior, change it once in `track-core.js`.

## Editor conventions (`js/editor.js`)

- Editor state lives in a single mutable `track` object (`TrackCore.cloneTrack(TrackCore.STARTER_TRACK)` initially).
- Undo/redo uses whole-track deep-clone snapshots (`undoStack`/`redoStack`, capped at `MAX_HISTORY`), not diffs. Every discrete mutation calls `pushUndo()` once *before* mutating, capturing pre-edit state. Continuous gestures (dragging a point, typing in a field) call `pushUndo()` once at gesture start, not per-tick, so one drag = one undo step — see `dragMutated` for how point-selection-without-dragging avoids recording a no-op step.

## Game conventions (`js/track-game.js`)

- `buildTrack()` (re)generates the entire three.js scene/mesh from the current path list — importing new track JSON at runtime just calls it again rather than patching state incrementally.
- Track geometry/physics constants of note: `N_DEFAULT` centerline samples per path, `CROSS_SECTION_SEGMENTS` for curved cross-section sampling, `COLLISION_WALL_MARGIN` (defined in `track-core.js`).
