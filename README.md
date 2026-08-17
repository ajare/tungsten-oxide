# tungsten-oxide

A native C++ (CMake/MSVC) racing track editor and driving game engine.

## Running it

Build from an MSVC Developer prompt, from the repo root:

```sh
git submodule update --init --recursive
cmake -S ext/willpower -B ext/willpower/build
cmake --build ext/willpower/build --config Release
cmake --build ext/willpower/build/_deps/massive-poly-pusher-build --config Release --target MppResourceParsers MppAppSupport assimp
cmake -S cpp -B cpp/build
cmake --build cpp/build --config Release
ctest --test-dir cpp/build -C Release --output-on-failure
```

- **`cpp/editor`** builds `track_editor`, the native ImGui/SDL3/OpenGL track editor: author tracks in a top-down + elevation view, export/import as JSON. `E`/`C`/`R` switch between Edit, Create and Rails modes.
- **`cpp/tungsten-monoxide`** builds the playable driving game.
- **`cpp/app`** builds `track_runner`, a headless CLI session host for a compiled track.

### Mesh regions

Beyond spline paths, a track can contain **mesh regions**: flat drivable areas — plazas, junction pads, arenas — authored in the [geometry-js](https://github.com/ajare/geoemetry-js) editor and imported as JSON.

In the editor, **Import Mesh** loads a `.json` file and drops it into the middle of the current view. **Paste Mesh** reads the clipboard instead — pair it with **Copy JSON** in the geometry-js editor — and places the region at the world origin, so its authored coordinates are preserved. In Edit mode you can also right-click the top-down view: when the clipboard holds something, the menu grows an **Add mesh → From clipboard** entry that drops the region centred on where you clicked. Click to select it, drag to move, shift+drag to rotate about its origin, and set its X/Z, elevation and rotation in the properties panel (or drag its line in the elevation panel to match a ribbon's height). Switch the mode dropdown to **Rails** (or press `R`) and click individual edges to toggle them:

- a **railed** edge is a solid wall the ship slides along, and can clear when airborne;
- a **bare** edge is a ledge — drive over it and you fall.

An imported region arrives **fully railed**: every rim edge is a wall, so it is drivable straight away and you open ledges by clicking them off. Edges shared between two polygons are interior seams and are never railed — you drive across those. Holes are rims too, so an imported hole starts as a walled pillar; unrail it to turn it into a pit. Regions are exported inside the track JSON and are drivable in the game.

### Road thickness

A track's **cross-section** points control the road's profile across its width — how much it crowns, and how thick it is. `Thickness` extrudes the road downward into a solid shell with an underside and side walls, so an elevated section reads as a slab rather than a paper sheet. It is authored per cross-section point, so it can taper along a curve, and it is purely cosmetic: you still drive on the top surface exactly as before. Set it to `0` for the original zero-thickness look.

### A note on units

Track files record a schema `version`. Schema 5 doubled the world's unit scale — a road that was 12 units wide is now 24 — and the ship, speeds and gravity were scaled to match, so tracks look and drive exactly as they did. The native loader accepts schema 10/11 only.

## How it's structured

- `cpp/core/` — native C++20 track engine: strict schema-10/11 loading, spline/mesh baking, renderer-neutral geometry and complete physics. See `docs/core.md`.
- `cpp/editor/` — the native track editor: state, undo/redo, canvas rendering and interaction. See `docs/editor.md`.
- `cpp/model-tool/` — standalone 3D-model import/preview/`.mppmodel`-save utility, unrelated to mesh regions above. See `docs/model-tool.md`.
- `cpp/tungsten-monoxide/` — the playable driving game runtime. See `docs/tungsten-monoxide.md`.
- `ext/willpower/` — the [Willpower](https://github.com/ajare/willpower) submodule; its nested MassivePolyPusher checkout supplies rendering dependencies to every native app.
- `assets/` (bundled textures) is shared across the native subprojects and stays at the repo root, as does the `ext/geoemetry-js` submodule.
- `ext/geoemetry-js/` — a git submodule ([`@willpower/geometry`](https://github.com/ajare/geoemetry-js)), a standalone geometry/mesh library with its own tests and React editor, and the only JavaScript codebase in this repo. It has no bearing on `cpp/` at runtime.

See `CLAUDE.md` for a deeper dive into the track data model and editor/game conventions, and `docs/core.md`/`docs/editor.md`/`docs/model-tool.md`/`docs/tungsten-monoxide.md` for module-by-module feature and physics documentation.

## Native C++ engine

The C++20 core independently loads complete current-schema track JSON, bakes spline paths and placed mesh assets, exposes graphics-API-neutral geometry, and simulates the full corridor/mesh physics. Schema version 10 is the oldest accepted; older schemas are not migrated.

The combined build imports the prebuilt `ext/willpower/build` libraries and their MassivePolyPusher build tree; it does not build either dependency. A standalone `cmake -S cpp/core ...` configuration is also supported. See `cpp/README.md`.

## Tests

```sh
ctest --test-dir cpp/build -C Release --output-on-failure
```

The committed physics corpus (`cpp/test-data/`) has two layers: byte-identical baked-world traces that isolate runtime math, and raw schema-10 tracks independently loaded and baked. A separate seeded-random corpus compares complete renderer-neutral track geometry from generated JSON. See `cpp/test-data/traces/raw/README.md` and `cpp/test-data/fixtures/random-track-mesh/README.md`.

### Cloning with the submodule

```sh
git clone --recurse-submodules <repo-url>
# or, if already cloned:
git submodule update --init --recursive
```
