# Native Track Editor — File I/O & Follow-On Plan

Status: **not started**. This plan is written for a future session to pick up; nothing here has
been implemented. It covers the four gaps identified after `EDITOR_CPP_PORT_PLAN.md`'s M0-M7c
landed (all planned milestones from that plan are complete): save/load, mesh asset import, a
texture file picker, and a full validation pass. See `EDITOR_CPP_PORT_PLAN.md` for everything
already built (`cpp/editor/`, target `track_editor`) and the conventions this plan continues —
"reuse core as a black box," Windows/MSVC-only, ImGui docking, no comments beyond non-obvious WHY.

Three of these four milestones (M8-M10) are genuinely new native-only work: `editor.html`'s
versions all lean on browser primitives (`<input type=file>`, `Blob`/`URL.createObjectURL`,
`navigator.clipboard`) that have no native equivalent yet in this codebase. **Read the "Open
decisions" section below and resolve them (ideally via the `grilling` skill) before writing code**
— this plan identifies the choices but does not lock them in, the way `EDITOR_CPP_PORT_PLAN.md`'s
own "Decisions locked in for this port" section did after its interview.

## Open decisions (resolve before implementing)

- **File dialog API.** Native has no file picker at all today (`Export USD` just writes a hardcoded
  `track_export.usda` in the working directory). Two options:
  - Legacy `GetOpenFileNameW`/`GetSaveFileNameW` (`comdlg32.lib`) — a handful of lines per call,
    matches the project's existing "keep it simple, Windows-only" posture (`NOMINMAX`/
    `WIN32_LEAN_AND_MEAN` already handled in `cpp/editor/CMakeLists.txt`).
  - Modern `IFileOpenDialog`/`IFileSaveDialog` (COM, Vista+) — more ceremony (COM init, interface
    querying, `Release()`), buys resizable/customizable dialogs and better Explorer integration.
  - **Recommendation:** the legacy API. This project has no other COM usage, and the payoff (nicer
    dialog chrome) doesn't offset the extra plumbing for a single-developer tool.
- **Clipboard API for mesh paste.** `OpenClipboard`/`GetClipboardData(CF_UNICODETEXT)`/
  `CloseClipboard` (all `<windows.h>`, no new dependency) is the direct native analogue of
  `navigator.clipboard.readText()`. Low-risk, but flagging it since nothing in `cpp/editor` touches
  the clipboard yet.
- **Mesh JSON parsing — the biggest unknown, research this first.** `editor.js`'s `parseMeshJSON`
  (`js/editor.js:375-383`) parses an arbitrary geometry-js `Mesh` export (from the *separate*
  `ext/geoemetry-js` editor's "Copy JSON" button) via `TrackMesh.meshFromJSON` and
  `TrackCore.normalizeMeshAssets`. It is genuinely unknown whether `Willpower.Geometry` (already
  linked into `core`, and therefore transitively available to `track_editor`) exposes an equivalent
  loader for that same JSON shape, or whether this milestone needs a new from-scratch parser in
  `cpp/editor` (which would NOT be "reusing core as a black box" — a real deviation from this
  port's guiding principle, worth surfacing to the user rather than deciding unilaterally).
  **Before writing any M9 code, check `cpp/willpower/willpower.geometry`'s public API for a JSON
  import path; if none exists, stop and ask how the user wants to handle it** (hand-roll a parser
  matching `js/track-mesh.js`'s `meshFromJSON`/`meshToJSON`, or scope M9 down to something else).
- **Default export filename sanitization.** `editor.js`'s `#exportBtn` handler
  (`js/editor.js:4463-4469`) does `(track.name || 'track').replace(/[^\w.-]+/g, '_') + '.json'`.
  Mirror this exactly for parity rather than inventing a different scheme.

## M8 — Save / Load

Mirrors `editor.html`'s `#newBtn`/`#exportBtn`/`#exportUsdBtn`/`#importBtn` toolbar row and its
`js/editor.js` handlers:

- **New** (`js/editor.js:3645-3654`, `#newBtn`, `editor.html:170`): push the current track onto
  undo history, then `editorState.replaceTrack(buildStarterTrack())` (the C++ equivalent already
  exists as a function; JS resets to `TrackCore.STARTER_TRACK` with zeroed elevation/roll and fresh
  ids — check whether the native starter track needs the same "zero elevation/roll" treatment or
  can just reuse `buildStarterTrack()` verbatim).
- **Export JSON** (`js/editor.js:4463-4472`, `#exportBtn`, `editor.html:184`): a Save dialog
  (filtered to `*.json`, default filename per the sanitization rule above), then
  `editor::toFile(editorState.track(), path)` (already exists).
- **Export USD** (`js/editor.js:4474-4487`, `#exportUsdBtn`, `editor.html:185`): extend the
  existing "Export USD" button (`main.cpp`, M7a) to use the same Save-dialog helper instead of the
  hardcoded `track_export.usda` path.
- **Import JSON** (`js/editor.js:4592-4607`, `#importBtn` -> hidden `#fileInput`,
  `editor.html:180,226`): an Open dialog (filtered to `*.json`), `editor::fromFile(path)`
  (already exists), push undo of the *prior* track only on successful parse (mirrors JS), show a
  status string on failure (mirrors the JS `alert(...)` — this editor already has a precedent for
  a status string, see `usdExportStatus` in `main.cpp`).

New shared module: a small `FileDialog.hpp/.cpp` (Open/Save wrappers per the decision above),
reused by M8, M9, and M10.

## M9 — Mesh Asset Import

Mirrors `editor.html`'s `#importMeshBtn`/`#pasteMeshBtn` and the right-click "paste mesh" menu
item — **blocked on the mesh-JSON-parsing open decision above**:

- **Import Mesh (file)** (`js/editor.js:4611-4616`, `#importMeshBtn` -> hidden `#meshFileInput`,
  `editor.html:181,227`): Open dialog (filtered to `*.json`), read the file, run it through
  whatever parse path the open decision above settles on, then `addMeshAsset`-equivalent logic
  (`js/editor.js:389-421`: `railBoundaryEdges` defaults every new asset to fully walled, pushes
  undo, centers the placement on the current view unless a world position was given).
  `EditorState` needs a new method for this (there's no "register a new mesh asset" method yet —
  M4 only ever placed the one hardcoded `test-rect` asset).
- **Paste Mesh (clipboard)** (`js/editor.js:4619`, `#pasteMeshBtn`, `editor.html:182`): same parse
  path, sourced from the clipboard (see the clipboard-API decision above) instead of a file.
- **Right-click context menu**: `TopDownCanvas.cpp` currently treats every right-click as a pan
  gesture (no context menu exists at all, unlike `editor.js`'s `#addPointMenu`,
  `js/editor.js:3056-3153`/`editor.html:305`). This milestone needs at least a minimal ImGui popup
  (`ImGui::OpenPopupOnItemClick`/`BeginPopup`) offering "Paste Mesh" (centered on the click's world
  position, mirroring `js/editor.js:3128-3131`), without breaking the existing right-drag-to-pan
  gesture on a miss. Scope creep risk: `editor.js`'s real context menu has many more entries (add
  point, add mesh, etc.) — keep this milestone to just what M9 needs unless asked to do more.

## M10 — Texture File Picker

Mirrors `editor.html`'s `#browseTextureBtn` (`js/editor.js:4624-4629`,
`editor.html:235` -> hidden `#textureFileInput`, `editor.html:228`, `accept="image/*"`). This is
the smallest of the three UI milestones — M7b already built everything except the picker itself:

- Add a "Browse..." button to `TexturePanel.cpp` next to the existing "Load Bundled Textures"
  button.
- Open dialog (filtered to whatever `stb_image` actually decodes — at minimum `*.png;*.jpg;*.jpeg;
  *.bmp`, matching what `TextureCache.cpp`'s `stbi_load` already supports; no need to widen the
  filter to stb_image's full format list unless asked).
- On selection: `editor::readImageSize(path, w, h)` + `state.addTextureAsset(...)` — both already
  exist (M7b), so this is almost entirely wiring, not new logic.
- Unlike `editor.js`, which stores an in-memory object-URL preview for files outside
  `assets/track/` (`texturePreviewUrls` in `js/editor.js:192,272`), the native `TextureCache`
  already loads by an absolute/relative *path* and re-reads it lazily on demand — no separate
  preview-URL bookkeeping needed; just store whatever path the dialog returns as `TextureAsset.path`
  directly (relative paths outside the repo's `assets/` tree are fine, since `TextureCache::get`
  takes any path, not just repo-relative ones).

## M11 — Full Validation Pass

Not a feature milestone — a checklist to run once, to confirm the M0-M7c editor work (which only
ever touched files under `cpp/editor/`) hasn't regressed the runtime/physics/mesh-baking side of
the project it deliberately left alone. This has **not been run this session** (only the native
editor's own in-process smoke checks have) and should not be assumed clean:

- `npm test` — Node's built-in runner over `test/` (`track-core.test.js`, `track-mesh.test.js`,
  `track-mesh-physics.test.js`, `track-physics.test.js`, `track-render-geometry.test.js`,
  `track-loader-fixtures.test.js`, `usd-export.test.js`, `vec3.test.js`, `ship-grid.test.js`,
  `parity.test.js`, `random-track-mesh-parity.test.js`).
- `npm run parity` — `tools/parity.mjs`, the JS-side parity tool.
- `ctest --test-dir cpp/build -C Release --output-on-failure` (from an MSVC Developer prompt, after
  `cmake --build cpp/build --config Release`) — covers `track_tests`, `parity`, `raw_parity`,
  `random_geometry_parity`, `raw_session_init_parity`, `raw_session_step_parity`
  (`cpp/core/CMakeLists.txt:102-139`).
- Re-run `track_editor.exe` itself and confirm all of its own smoke-check lines (M1, M3-M7c) still
  print `OK` — a regression here would mean this milestone's own changes (or an accidental edit to
  a shared file like `EditorTrackDefinition.hpp`) broke something the earlier milestones already
  covered.

Expected outcome: a clean pass, since M0-M10 above only add new files/UI under `cpp/editor/` and
don't modify `track-core.js`, `js/*`, `cpp/core`, or `cpp/willpower` — but this is a prediction to
verify, not a formality to skip.
