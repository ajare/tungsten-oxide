# tungsten-oxide

A browser-based racing track editor and driving game, built with plain HTML/JS and [three.js](https://threejs.org/) (loaded via CDN). No build step, no dependencies to install for the main app.

## Running it

Just open the HTML files in a browser, or serve the repo root with any static file server:

```sh
npx serve .
```

- **`track.html`** — the driving game. Drive with W/A/S/D or arrow keys, import a track JSON, or open the editor. `G` toggles rail rendering, `H` wireframe, `R` respawns.
- **`editor.html`** — the track editor. Author tracks in a top-down + elevation view, export/import as JSON. `E`/`C`/`R` switch between Edit, Create and Rails modes.

### Mesh regions

Beyond spline paths, a track can contain **mesh regions**: flat drivable areas — plazas, junction pads, arenas — authored in the [geometry-js](https://github.com/ajare/geoemetry-js) editor and imported as JSON.

In the editor, **Import Mesh** loads a `.json` file and drops it into the middle of the current view. **Paste Mesh** reads the clipboard instead — pair it with **Copy JSON** in the geometry-js editor — and places the region at the world origin, so its authored coordinates are preserved. In Edit mode you can also right-click the top-down view: when the clipboard holds something, the menu grows an **Add mesh → From clipboard** entry that drops the region centred on where you clicked. Click to select it, drag to move, shift+drag to rotate about its origin, and set its X/Z, elevation and rotation in the properties panel (or drag its line in the elevation panel to match a ribbon's height). Switch the mode dropdown to **Rails** (or press `R`) and click individual edges to toggle them:

- a **railed** edge is a solid wall the ship slides along, and can clear when airborne;
- a **bare** edge is a ledge — drive over it and you fall.

An imported region arrives **fully railed**: every rim edge is a wall, so it is drivable straight away and you open ledges by clicking them off. Edges shared between two polygons are interior seams and are never railed — you drive across those. Holes are rims too, so an imported hole starts as a walled pillar; unrail it to turn it into a pit. Regions are exported inside the track JSON and are drivable in the game.

### A note on units

Track files record a schema `version`. Schema 5 doubled the world's unit scale — a road that was 12 units wide is now 24 — and the ship, speeds and gravity were scaled to match, so tracks look and drive exactly as they did. Older files are converted automatically the first time they're loaded; re-exporting saves them in the new units.
- **`index.html`** — an unrelated scratch demo (spinning cube), not part of the track app.

## How it's structured

- `track-core.js` — shared track math (spline evaluation, control points, serialization). Used by both the game and the editor so their geometry can never drift apart.
- `js/track-mesh.js` — shared mesh-region math (triangulation, containment, rail collision), built on geometry-js.
- `js/track-game.js` — three.js scene, track mesh generation, car physics, collisions.
- `js/editor.js` — editor state, undo/redo, canvas rendering and interaction.
- `ext/geoemetry-js/` — a git submodule ([`@willpower/geometry`](https://github.com/ajare/geoemetry-js)), a standalone geometry/mesh library with its own tests and React editor. Linked into this project as a local npm dependency (`package.json` -> `"@willpower/geometry": "file:ext/geoemetry-js"`) so track code can `import` it.

See `CLAUDE.md` for a deeper dive into the track data model and editor/game conventions.

## Tests

```sh
npm test                                   # pure logic, no browser
node tools/browser-smoke.mjs               # drives the real pages in headless Chromium
```

The browser suite needs Playwright, which is not a project dependency:

```sh
npm install --no-save playwright && npx playwright install chromium
```

### Cloning with the submodule

```sh
git clone --recurse-submodules <repo-url>
# or, if already cloned:
git submodule update --init --recursive
npm install
```
