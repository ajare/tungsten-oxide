# Native ImGui Track Editor — Port Plan

Status: **M5 complete — all originally planned milestones (M0-M5) landed.** `track_editor` builds
under the combined `cpp/` configure, opens an
SDL2/OpenGL window with a docking-enabled ImGui frame, round-trips an in-memory starter track
through `editor::TrackDefinition`'s JSON (de)serialization and `tox::Track::fromJson` on startup
(verified OK), and renders that track's baked road/centerline plus authored control points in a
top-down `ImDrawList` canvas with working pan (right-drag) and zoom (scroll). `EditorState` covers
point editing: select/drag/delete position points in Edit mode, click-to-add/close/finish a new
path in Create mode, `edit | create | rails` mode switching with E/C/R shortcuts, and Ctrl+Z/Ctrl+Y
undo/redo. M4 adds mesh region placement (reordered ahead of the original plan's rails-mode
milestone, since rails need a mesh region to flag edges on): select/drag/shift+drag-rotate/delete a
placed mesh, rendered and hit-tested via core's own baked `tox::Track::meshRegions` (no
reimplemented placement-transform math). There's no asset-import UI yet, so a single hardcoded
rectangle asset is the only placeable mesh. M5 adds Rails mode: clicking near an edge toggles its
rail flag on the shared MeshAsset (so every placement of that asset picks up the change at once),
falling back to panning on a miss, exactly like `editor.js`'s modal Rails-mode click handling. Since
core only bakes the already-flagged rail subset (what physics needs), Rails-mode picking/rendering
is the one path in the editor that computes its own local-to-world edge transform from the
authored mesh asset rather than reusing a core bake -- documented inline where that happens. All
four milestones' logic (M1/M3/M4/M5) is verified via in-process smoke checks exercising the exact
methods the UI calls (every check OK across all of them), plus screenshots confirming the UI
renders without crashing. This document records the plan to port `editor.html`/`js/editor.js`
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
- **M7 — Texture assets, random-track generation, USD export.** Texture thumbnail loading, the
  random-track panel/localStorage-equivalent ranges, `usd-export.js` parity.

Each milestone should build and be manually exercised (open the app, drive the feature) before moving
to the next; this plan doc's Status line should be updated as milestones land.
