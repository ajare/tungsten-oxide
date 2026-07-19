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
- `js/track-mesh.js` — shared mesh-region math (see below). The mesh-world counterpart to `track-core.js`, split out because it depends on geometry-js while `track-core.js` stays dependency-free.
- `ext/geoemetry-js/` — a git submodule (`@willpower/geometry`, https://github.com/ajare/geoemetry-js), a separate self-contained ES-module mesh/geometry library with its own `package.json`, tests, and a React/Vite editor. It's linked into the root project as a local npm dependency (root `package.json` -> `"@willpower/geometry": "file:ext/geoemetry-js"`, installed via `npm install`, resolved as a symlink at `node_modules/@willpower/geometry`) so track code can `import` it as `@willpower/geometry`. See `ext/geoemetry-js/README.md` for its own commands (`npm test`, `npm --prefix editor/ui run dev`, etc.) and its own codebase map.

## Running / testing

No build step: open `track.html` or `editor.html` directly, or serve the repo root statically. Run `npm install` once (after `git submodule update --init --recursive`) to link the `@willpower/geometry` local dependency.

- `npm test` — Node's built-in runner over `test/*.test.js` (pure logic: mesh geometry, rail collision, schema round trips). Fast, no browser.
- `node tools/browser-smoke.mjs` — drives the real pages in headless Chromium. Catches ESM/import-map breakage, runtime errors and physics regressions the unit tests can't see. Needs `npm install --no-save playwright && npx playwright install chromium`. Deliberately outside `test/` so `node --test` doesn't try to run it.

For the `geoemetry-js` submodule, its own commands apply (`npm test`, `npm --prefix ext/geoemetry-js/editor/ui run dev`) — see `ext/geoemetry-js/README.md`.

**The `.js` files are ES modules** (root `package.json` has `"type": "module"`), except `track-core.js`, which is deliberately a classic browser script — an IIFE assigning `window.TrackCore`. Tests load it by evaluating its source with a stand-in `window`, not by importing it.

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

## Architecture: mesh regions (`js/track-mesh.js`)

Flat, drivable areas imported from the geometry-js editor — plazas, junction pads, arenas — for shapes a swept spline ribbon cannot express. Introduced in schema 4:

```
meshAssets: { <assetId>: { name, railHeight, mesh } }   // mesh = pristine geometry-js JSON
meshes:     [ { id, asset, x, z, rotation, elevation } ] // rigid placements
```

Design rules, all of which the code depends on:

- **Assets are geometry, placements are transforms.** A placement is rigid: X/Z translation, yaw rotation, and one `elevation`. There is no scale, and a region is always horizontal (normal `+Y`, no banking or cross-section). Because the transform is rigid, **a triangulation computed in asset space stays valid for every placement** — assets are triangulated once, never per frame.
- **Rail flags live on the asset**, as `attributes.rail` on geometry-js edges, so every placement of a shape is railed identically. Need different railing? Import the file again — imports always mint a new asset id (`pad`, `pad-2`), never disturbing existing placements.
- **Two import routes, one code path.** `parseMeshJSON()` validates and returns `{ mesh }` or `{ error }` without ever throwing or touching `track`, so a bad paste is reported before anything is mutated; `addMeshAsset(mesh, name, at)` then mints the asset and placement. File import (`Import Mesh`) centres the region on the view so an asset authored far from `(0,0)` still lands somewhere visible; clipboard import (`Paste Mesh`) places it at the world origin, preserving authored coordinates. `navigator.clipboard` needs a secure context, so the paste path degrades to an alert pointing at the file route. The Edit-mode right-click menu offers a third entry point, centring the region on the click; it opens *immediately* with the paste option hidden and reveals it only once an async `clipboardHasText()` resolves, since awaiting a clipboard read first would stall the menu behind a permission prompt on every right-click. A `menuToken` guards against a slow read from an already-dismissed menu revealing the option on a later one.
- **Import rails every rim edge** (`TrackMesh.railBoundaryEdges`), so a fresh region is enclosed and drivable immediately; you unrail edges to open ledges. "Rim" means `edge.polygons.size === 1` — an edge owned by two polygons is an interior seam you drive across, and railing it would wall a region down the middle. Hole rims are owned by the one polygon holding the hole, so they count as rim and an imported hole starts as a walled pillar rather than a pit.
- **A railed edge is a solid, finite-height wall**; an unflagged boundary edge is a **ledge** you drive off into the existing ballistic code. Holes follow exactly the same rules, so a bare hole is a pit you fall through.
- **Rails are collision everywhere now.** `G` is a pure rendering toggle and never changes what stops the ship.
- **Surface precedence is nearest-in-Y**, not simple containment. `surfaceOwnerAt()` picks whichever of the mesh region and the spline corridor sits closer to the ship's current Y, which is what lets a mesh flyover pass over a ribbon without hijacking the ship underneath.
- **`corridorContains()` is the real containment test, not `projectToSurface()`.** `projectToSurface` returns only a *lateral* offset `s`; a point far off the *end* of a segment projects onto that segment's clamped endpoint and reports a small, in-range `s` while being hundreds of units away. Ignoring this teleports the ship onto a distant ribbon when it leaves a mesh ledge. Always pair the lateral bounds with the along-tangent check.

### Lateral offset is not containment — the recurring trap

This has now caused two separate bugs, so it is worth stating as a rule: **anywhere you test whether a point is "on" a piece of track, a lateral/`s` bound alone is wrong.** Because `t` is clamped to `[0,1]`, a point beyond a segment's end projects *onto that end* and inherits its lateral offset — which, for a ship running down the middle of a straight road, is `0` for every segment on the path no matter how distant.

Two places guard against it, and both are load-bearing:

- `SEGMENT_ALONG_TOL` in `sampleTrack()`'s `bestUnder` test. Without it, a point past an **open curve's end** is claimed by a far-back segment, so `best` is never the terminal segment, `offEnd` can never fire, and the ship is reprojected backwards instead of launching off the end — it becomes impossible to leave a curve. Covered by a browser test that verifies both ends.
- `CORRIDOR_ALONG_TOL` in `corridorContains()`, for the mesh-exit and airborne-landing paths.

Physics on a region is genuinely different code from the corridor: free 2D integration plus swept segment collision (`slideAlongRails`), because there is no lateral parameter to clamp on an arbitrary polygon. Collision is swept, not positional, so a fast ship cannot tunnel through a wall.

**Known limitation, by design:** a region is flat, so it only meets a ribbon cleanly where that ribbon is level and unbanked. Every other join is a visible step. Place regions accordingly.

## World units and schema migration

**Schema 5 doubled the world's unit scale.** Every length in a track — control point positions, widths, elevations, rail heights, mesh geometry — is twice what the same track measured under schema 4, and the game's ship, speeds, gravity, camera and thresholds were all scaled to match. **Nothing about how a track looks or drives changed; only the absolute units did.** The HUD's `speed × 4.5` factor was halved from `× 9` for exactly this reason, so the km/h readout is unchanged.

Rules if you touch this:

- **Only lengths scale.** Angles (`roll`, `rotation`), curve parameters (`t`), NURBS `weight`, cross-section `curvature` (dimensionless) and `tightness` (an exponent) are scale-invariant. Scaling any of them changes a track's *shape*, not its size. Likewise, rates (`turnRate` rad/s, `grip`, lerp factors, the bob frequency, the landing spring's `-55`) and ratios (`0.98` scrub, `1.5` step factor) stay put — only their length/velocity *caps* scale.
- **`scaleRawTrackData()` runs before normalization, deliberately.** Normalization injects defaults (`DEFAULT_WIDTH`, `DEFAULT_RAIL_HEIGHT`) that are already in current units; scaling afterwards would double those too and silently widen every old track that never authored an explicit width. There's a test pinning this.
- **Built-in tracks are authored in current units** and carry `version: TRACK_SCHEMA_VERSION`, so `cloneTrack(DEFAULT_TRACK)` — which bypasses `parseTrack` entirely — is never re-migrated.
- Migration is keyed off the file's `version` and is idempotent across save/load.

## Editor conventions (`js/editor.js`)

- Editor state lives in a single mutable `track` object (`TrackCore.cloneTrack(TrackCore.STARTER_TRACK)` initially).
- Undo/redo uses whole-track deep-clone snapshots (`undoStack`/`redoStack`, capped at `MAX_HISTORY`), not diffs. Every discrete mutation calls `pushUndo()` once *before* mutating, capturing pre-edit state. Continuous gestures (dragging a point, typing in a field) call `pushUndo()` once at gesture start, not per-tick, so one drag = one undo step — see `dragMutated` for how point-selection-without-dragging avoids recording a no-op step.
- Mesh regions: the JSON in `track` is authoritative (it is what undo snapshots and export read); `meshCache` holds a live geometry-js `Mesh` per asset purely to avoid reparsing. Rail edits mutate the live mesh and then `writeBackAsset()` immediately re-serializes it, so the two never drift. Undo/redo drops the cache entirely, since restored assets may differ.
- Dragging a mesh region in Edit mode moves it; shift+drag rotates it about its own placement origin instead (`dragging === 'meshRotate'`). The rotate branch records the offset between the drag start's angle-from-origin and the placement's current `rotation` at mousedown, then keeps applying that offset as the mouse moves, so the shape doesn't jump to face the cursor the instant the drag begins. Angle convention matches `TrackMesh.localToWorld`: `atan2(dz, dx)` from the placement's `(x, z)`, in degrees.
- The add-point/add-mesh popup (`#addPointMenu`) suppresses its own `contextmenu` event, separately from `topCanvas`'s. Right-click's `contextmenu` fires *after* mousedown, by which point the popup mousedown already opened is the frontmost element under the cursor -- so the browser hit-tests the popup, not the canvas beneath it, and `topCanvas`'s own handler never runs. Skipping this lets the native OS/browser context menu render on top of the custom one.
- The mode dropdown is `edit | create | rails`, with `E`/`C`/`R` shortcuts. All mode changes go through `setEditMode()` — it clears the abandoned create draft, drops the rail pick when leaving Rails mode, and syncs the dropdown, so the keys and the dropdown can't drift. Rails mode is modal on purpose: only mesh edges are pickable, so flagging a rail can't be confused with selecting anything else. Mesh regions are hit-tested *last* in edit mode, after every path handle, so a large region never steals a click from a control point drawn on top of it.
- Both modules expose a read-only `window.__editor` / `window.__game` handle for console debugging and the browser smoke tests, since ES modules leak nothing to the page.

## Game conventions (`js/track-game.js`)

- `buildTrack()` (re)generates the entire three.js scene/mesh from the current path list — importing new track JSON at runtime just calls it again rather than patching state incrementally.
- Track geometry/physics constants of note: `N_DEFAULT` centerline samples per path, `CROSS_SECTION_SEGMENTS` for curved cross-section sampling, `COLLISION_WALL_MARGIN` (defined in `track-core.js`).
