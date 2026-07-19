# tungsten-oxide

A browser-based racing track editor and driving game, built with plain HTML/JS and [three.js](https://threejs.org/) (loaded via CDN). No build step, no dependencies to install for the main app.

## Running it

Just open the HTML files in a browser, or serve the repo root with any static file server:

```sh
npx serve .
```

- **`track.html`** — the driving game. Drive with W/A/S/D or arrow keys, import a track JSON, or open the editor.
- **`editor.html`** — the track editor. Author tracks in a top-down + elevation view, export/import as JSON.
- **`index.html`** — an unrelated scratch demo (spinning cube), not part of the track app.

## How it's structured

- `track-core.js` — shared track math (spline evaluation, control points, serialization). Used by both the game and the editor so their geometry can never drift apart.
- `js/track-game.js` — three.js scene, track mesh generation, car physics, collisions.
- `js/editor.js` — editor state, undo/redo, canvas rendering and interaction.
- `ext/geoemetry-js/` — a git submodule ([`@willpower/geometry`](https://github.com/ajare/geoemetry-js)), a standalone geometry/mesh library with its own tests and React editor. Linked into this project as a local npm dependency (`package.json` -> `"@willpower/geometry": "file:ext/geoemetry-js"`) so track code can `import` it.

See `CLAUDE.md` for a deeper dive into the track data model and editor/game conventions.

### Cloning with the submodule

```sh
git clone --recurse-submodules <repo-url>
# or, if already cloned:
git submodule update --init --recursive
npm install
```
