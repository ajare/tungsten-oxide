# Native ImGui Track Editor — Port Plan

Status: **M7b complete** (texture assets: image loading, GL thumbnails, tile-grid picker, per-path
binding; M7c's full random-track generation remains out of scope -- see the M7 milestone note
below). `track_editor` builds under the combined
`cpp/` configure, opens an
SDL2/OpenGL window with a docking-enabled ImGui frame, round-trips an in-memory starter track
through `editor::TrackDefinition`'s JSON (de)serialization and `tox::Track::fromJson` on startup
(verified OK), and renders that track's baked road/centerline plus authored control points in a
top-down `ImDrawList` canvas with working pan (right-drag) and zoom (scroll). `EditorState` covers
point editing: select/drag/delete position points in Edit mode, click-to-add/close/finish a new
path in Create mode, `edit | create | rails` mode switching with E/C/R shortcuts, and Ctrl+Z/Ctrl+Y
undo/redo. M4 added mesh region placement (reordered ahead of the original plan's rails-mode
milestone, since rails need a mesh region to flag edges on): select/drag/shift+drag-rotate/delete a
placed mesh, rendered and hit-tested via core's own baked `tox::Track::meshRegions` (no
reimplemented placement-transform math). There's no asset-import UI yet, so a single hardcoded
rectangle asset is the only placeable mesh. M5 added Rails mode: clicking near an edge toggles its
rail flag on the shared MeshAsset (so every placement of that asset picks up the change at once),
falling back to panning on a miss, exactly like `editor.js`'s modal Rails-mode click handling. Since
core only bakes the already-flagged rail subset (what physics needs), Rails-mode picking/rendering
is the one path in the editor that computes its own local-to-world edge transform from the
authored mesh asset rather than reusing a core bake -- documented inline where that happens. M6
adds the elevation profile side view: a second `ImDrawList` canvas showing the current path's
baked Y profile plus draggable position-point elevation markers, collapsible via a "Show" checkbox.
Its x-axis places points by authored order rather than editor.js's true spline-parametrized arc
length -- a documented simplification, exact for every path this editor can currently produce.
M7a adds USD export and random-track generation. `USDExport.hpp/.cpp` is NOT a port of
`js/usd-export.js`: that module re-derives road/shell surface geometry itself from scratch, because
in the browser it's the only baked geometry available; here, core already bakes exactly this into
`tox::Track::geometry` (renderer-neutral `GeometryBatch`es) for exactly this purpose, so the
exporter just walks those batches into USD Mesh prims. `RandomTrack.hpp/.cpp` ports
`generateRandomTrack`'s closed-loop branch (N-turn loop, calibrated driven length, rolling hills,
curvature-based banking, boost zones) bit-for-bit including its `mulberry32` PRNG; the
mesh-section/ramp/jump-platform branch (~180 more lines, an iterative spline-endpoint-blend solve)
is deferred as future work, so every generated track today is the single-loop variant. M7b adds
texture assets: `TextureCache.hpp/.cpp` decodes PNGs with a vendored `stb_image.h` (single-header,
public domain -- see `include/stb/README-VENDORED.md`) and uploads one `GL_TEXTURE_2D` per unique
file path, cached for the process lifetime, since decoding is display-only decoration for the tile
picker and never touches physics/baking. `TexturePanel.hpp/.cpp` is the asset list + tile-grid
picker UI (`ImGui::ImageButton` per tile), backed by new `EditorState` methods
(`addTextureAsset`/`deleteTextureAsset`/`setTextureTileSize`/`assignPathTexture`/
`clearPathTexture`) that mirror editor.js's texture-panel functions one-for-one -- the schema
(`TextureAsset`/`TextureBinding` in `EditorTrackDefinition.hpp`, JSON (de)serialization in
`EditorTrackDefinition.cpp`) already existed unchanged since M1, so this milestone was scoped
entirely to loading/UI, no schema work. "Load Bundled Textures" scans `assets/track/manifest.json`
the same way editor.js's `loadBundledTextureAssets` does, but reads straight off disk instead of
`fetch()`; `findAssetsDir()` locates the repo's checked-in `assets/` directory by walking up from
the process's working directory, since the built exe's cwd sits several levels below the repo root
(`cpp/build/editor/Release/...`), unlike the browser editor's page-relative URLs. All milestones'
logic through M7b is verified via in-process smoke checks exercising the exact methods the UI
calls (every check OK across all of them), plus screenshots confirming the UI renders without
crashing. This document
records the plan to port `editor.html`/`js/editor.js`
(the browser-based 2D/elevation track editor, ~4,700 lines) to a native C++ application,
`cpp/editor` (target `track_editor`), sitting alongside `cpp/core` and `cpp/willpower` per
`cpp/CMakeLists.txt`.

Related completed plans: `CPP_PORT_PLAN.md`, `MESH_CPP_PORT_PLAN.md`, `NATIVE_GAME_RUNTIME_PLAN.md`.
Those ported the *runtime* (loading, baking, simulation). This plan ports the *authoring tool* that
produces the schema-10 JSON those runtimes consume. `cpp/core` is reused as a black box for preview
baking; nothing in `core`'s public API changes because of this work.

## Decisions locked in for this port

- **Directory/target.** New `cpp/editor/` directory, sibling to `core/` and `willpower/`, added via
  `add_subdirectory(editor)` from the root `cpp/CMakeLists.txt`. Executable target: `track_editor`.
- **Windowing/graphics.** SDL2 + OpenGL3, matching the brief. SDL2 is pulled via CMake `FetchContent`
  (built from source as part of this project — no vendored prebuilt libs, no system install
  assumed), unlike `cpp/willpower`'s `WILLPOWER_VENDOR_DIR` convention.
- **ImGui.** The **docking** branch (superset of `master`, adds `DockSpace`/multi-viewport), since
  the editor's panel layout (top-down canvas + elevation profile + property panels, see
  `editor.html`'s `grid-template-areas`) maps naturally onto dockable ImGui windows later. Per the
  brief, ImGui core + the SDL2 and OpenGL3 backend files are **copied** into
  `cpp/editor/include`/`cpp/editor/src`, not referenced as a submodule/FetchContent — same spirit as
  `cpp/core/third_party/nlohmann` being a vendored drop-in.
- **GL loader.** `gl3w` (the minimal loader historically shipped in ImGui's own
  `examples/libs/gl3w`), copied in alongside ImGui for the same reason.
- **2D rendering.** The top-down canvas and elevation profile are drawn with `ImDrawList` calls
  (`AddLine`, `AddCircle`, `AddConvexPolyFilled`, ...) inside ImGui child windows — no separate
  OpenGL 2D pipeline. This mirrors what `editor.js` does with `CanvasRenderingContext2D`.
- **Authoring model.** `cpp/editor` defines its own `TrackDefinition` struct (new header,
  `cpp/editor/include/EditorTrackDefinition.hpp`) that mirrors `cpp/core/include/TrackDefinition.hpp`
  field-for-field, plus any editor-only fields identified during the M1 schema audit. The editor owns
  both directions independently of `core`:
  - `fromJson`: parses raw schema-10 JSON directly into the editor struct — tolerant of a
    mid-edit/partial state, doesn't require the track to be fully bakeable to load, matching
    `editor.js`'s own lenient `parseTrack`.
  - `toJson`: serializes the editor struct back to schema-10 JSON, used for save/export.
  - **Live preview/bake still goes through the existing `tox::Track::fromJson(dump())` unmodified** —
    `core`'s loader/baker is reused as a black box, never forked or modified.
  - Undo/redo: deep-copied struct snapshots (plain-data struct, trivially copyable), mirroring
    `editor.js`'s whole-track-clone undo stack (`pushUndo()`/`MAX_HISTORY`).
- **Platform.** Windows/MSVC only, matching `cpp/willpower`'s existing constraint (no portability
  work attempted here).

## Milestones

- **M0 — Skeleton (this session).** `cpp/editor/CMakeLists.txt`; vendored ImGui (docking) + gl3w
  under `include`/`src`; SDL2 via `FetchContent`; minimal `main.cpp` that opens an SDL2/OpenGL window,
  creates an ImGui context (docking enabled), runs the demo window through one frame, and links
  `core` + `willpower` (even though nothing is called yet) to prove the combined build order works.
  Wired into root `cpp/CMakeLists.txt`. No editor logic yet.
- **M1 — Authoring model.** `EditorTrackDefinition.hpp` + `fromJson`/`toJson`; audit schema-10 fields
  against `cpp/core/include/TrackDefinition.hpp` to enumerate what's editor-only; load/save/export to
  file; undo/redo stack; live preview bake via `Track::fromJson`.
- **M2 — Top-down 2D view.** Render paths/points/mesh regions via `ImDrawList` in a child window;
  camera pan/zoom; grid.
  point add/select/drag; `pushUndo()`-per-gesture parity with `editor.js`.
- **M3 — Point editing.** Add/select/drag/delete points; edit-mode parity with `editor.js`
  (`edit | create | rails` modes, `E`/`C`/`R` shortcuts).
- **M4 — Mesh regions.** Place/drag/rotate mesh assets (shift+drag rotate about placement origin).
  Reordered ahead of the original plan's M4/M5: rails mode has nothing to flag until a mesh region
  exists, so mesh placement has to land first.
- **M5 — Rails mode.** Mesh-edge rail flagging, modal pickable-edges-only behavior.
- **M6 — Elevation profile.** Second canvas view + editing, collapsible panel.
- **M7a — Random-track generation + USD export.** `generateRandomTrack`'s closed-loop branch
  (mesh-section/ramp branch deferred); USD export rebuilt to walk core's own baked
  `tox::Track::geometry` batches rather than porting `usd-export.js`'s from-scratch surface
  derivation.
- **M7b — Texture assets.** `stb_image.h` vendored (`include/stb/`) for PNG decoding;
  `TextureCache` uploads/caches one GL texture per file path; `TexturePanel` lists texture assets,
  a tile-grid picker (`ImGui::ImageButton`) assigns/clears the current path's binding, tile
  width/height are editable per asset, "Load Bundled Textures" scans
  `assets/track/manifest.json`. Backed by new `EditorState` methods mirroring editor.js's texture
  panel functions; the schema itself needed no changes (already present since M1).
- **M7c — Full random-track generation (not started).** The mesh-section/ramp/jump-platform branch
  of `generateRandomTrack`: an iterative spline-endpoint-blend solve to land each drop exactly,
  generated platform mesh assets, launch ramps. ~180 lines of JS not yet ported; see
  `RandomTrack.hpp`'s header comment for the exact boundary.

Each milestone should build and be manually exercised (open the app, drive the feature) before moving
to the next; this plan doc's Status line should be updated as milestones land.
