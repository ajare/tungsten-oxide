# Native Track Editor — JS Parity Audit & Fix List

Status: **findings 1–12 (all "Bugs") fixed and verified; the "Functional gaps" section is still
open** (that section was always separate scope, not defects). This is the output of a differential
audit of the native editor (`cpp/editor/`, target `track_editor`) against `js/editor.js` +
`editor.html`, run after `EDITOR_NATIVE_FILE_IO_PLAN.md`'s M8–M11 landed. Every item under "Bugs"
was reproduced, not inferred; the evidence is quoted inline. Verification: the JSON round-trip
diff harness (recreated temporarily) now reports 7/7 matched, up from 0/7; the state-mutation
probes confirm the id collision, start-point drift, and orphaned-asset findings are gone. See
"Fix notes" at the end of each finding below for what actually changed, and the new
"Parity-fix smoke check" in `main.cpp` (findings 1, 4, 5) for the coverage that's now permanent.

`js/editor.js` and `track-core.js` are the reference. `cpp/core` (the runtime loader/baker) is a
black box both sides feed, and is **not** implicated in any finding here — every bug is in
`cpp/editor/`.

## How this was tested

Two harnesses, both temporary (reverted; recreate as needed):

1. **JSON round-trip diff.** A headless `--roundtrip <in.json> <out.json>` mode was added to
   `main.cpp` (parse with `editor::fromFile`, write back with `editor::toFile`). A Node script then
   ran the same input through `track-core.js`'s `parseTrack`/`serializeTrack` and structurally
   diffed the two normalized tracks, tolerancing floats at `1e-9` relative so formatting
   differences don't mask real ones. Corpus: `TrackCore.STARTER_TRACK`, `TrackCore.DEFAULT_TRACK`,
   and all five committed fixtures in `test/fixtures/random-track-mesh/`.

   **Result: 0/7 matched.** Findings 2, 5, 6, 10 come from this.

2. **State-mutation probes.** A headless `--idprobe` mode driving `EditorState` directly (the same
   methods `TopDownCanvas.cpp` calls), then baking the result through `tox::Track::fromJson` to see
   what the runtime actually receives. Findings 1, 4, 5 come from this.

Note that `track-core.js` is a classic browser script (an IIFE assigning `window.TrackCore`), so the
Node harness must evaluate its source against a stand-in `window` rather than importing it — the
same contract `test/` already uses.

## Bugs

### 1. CRITICAL — ID collision silently destroys newly-drawn paths

`EditorState::nextId_` (`EditorState.hpp`) starts at `1` and is **never seeded from a loaded
track**. `finishCreateDraft` mints `"path" + nextId_++` and `"p" + nextId_++`; `placeMeshAsset`
mints `"mesh" + nextId_++`. Loading a track never advances the counter, so freshly created ids
collide with ids already in the file.

Reproduction — load a track whose points carry `p1..p12` (exactly what `js/editor.js` writes), then
draw a four-point path in Create mode:

```
path id=starter-path: 'p1' 'p2' 'p3' 'p4' 'p5' 'p6' 'p7' 'p8' 'p9' 'p10' 'p11' 'p12'
path id=path1:        'p2' 'p3' 'p4' 'p5'          <-- collides
```

`cpp/core/src/TrackLoader.cpp`'s `normalize()` aliases duplicate position-point ids
(`if (found != pointsById.end()) point = found->second;`), so on bake the new path's geometry
becomes the *old* path's:

```
drew:  (3000,0) (3000,500) (3500,500) (3500,0)
baked: (1154,666) (666,1154) (0,1333) (-666,1154)   <-- the starter path's points
```

The user's drawn shape is destroyed with no error, no warning, and an undo step that restores a
track that was already correct.

**Fix.** Seed `nextId_` past every id present in the track — in the `EditorState` constructor and
in `replaceTrackKeepHistory` (which covers New / Import / Random / undo / redo). Alternatively mint
by scanning for the first unused id, which is what JS does (`newMeshPlacementId`,
`js/editor.js:367`; `newId`, `js/editor.js:587`) and is collision-proof by construction. Prefer the
JS approach so the two can't drift again.

**Fix applied.** `nextId_` removed entirely; `EditorState` now mints `path`/`p`/`m`-prefixed ids by
scanning for the first unused `"<prefix><N>"`, matching JS. Implementing this surfaced a second,
undocumented gap in the same area: `buildStarterTrack()` (and `New`/Random, which reuse it) never
ran the id-backfill fromJson performs, so the *live app* started with every point id empty —
id-scanning alone can't help if nothing has an id to collide with yet, and finding 4's fix (below)
depends on ids existing to match by. `EditorTrackDefinition.hpp` gained a standalone
`backfillPointIds()` (the same logic `normalize()` already ran, factored out), and `EditorState`'s
constructor and `replaceTrackKeepHistory` now call it unconditionally — mirroring js/editor.js's
own `ensureTrackIds()`, called after every track construction/replacement there. Verified via
`--idprobe`: a path drawn after loading a 12-point track now gets ids `p13..p16` (no collision) and
bakes with the coordinates actually drawn, not an aliased older path's.

### 2. HIGH — `fromJson` never assigns position-point ids; `toJson` writes `"id": ""`

`track-core.js`'s `parseTrack` backfills `p1..pN` for any position point lacking an id
(`track-core.js:1665-1678`). `editor::fromJson` does not, and `toJson` then serializes the empty
string:

```json
{ "id": "", "pos": [1332.907, 0.0, 0.0], "type": "position", "weight": 1.0 }
```

Both loaders treat `""` as absent (`if (!p.id)` in JS, `point.id.empty()` in
`TrackLoader.cpp`), so this recovers rather than corrupting — but the editor's saved files carry no
stable point identity at all, which means shared/disjoint points, junctions and `start.point` cannot
survive a save, and it is what makes finding 1 reachable in the first place.

**Fix.** Backfill ids during `normalize()` in `EditorTrackDefinition.cpp`, mirroring the loop
`TrackLoader.cpp` already runs. Omit the key entirely rather than writing `""` when an id is
genuinely absent.

**Fix applied.** `normalize()` now calls the shared `backfillPointIds()` (see finding 1's fix note)
after parsing paths. `pointToJson()` already only ever emits whatever string is in `point.id`, so
once it's never empty in practice, `"id": ""` stops appearing on its own. Verified: the differential
round-trip harness's starter/default/all-five-fixture corpus went from 0/7 to 7/7 matching JS.

### 3. HIGH — uncaught exception on Export JSON crashes the editor

`main.cpp:751-757`:

```cpp
if (ImGui::Button("Export JSON...")) {
  const editor::FileDialogResult picked = /* ... */;
  if (picked.ok) {
    editor::toFile(editorState.track(), picked.path);   // throws, uncaught
    fileIoStatus = "Wrote " + picked.path.string();
  }
}
```

`editor::toFile` throws `std::runtime_error("cannot open track file for writing: ...")` when the
stream won't open — read-only target, locked file, removed removable drive. Import JSON *does*
catch, and Export USD checks its `ofstream`; Export JSON is the only one that doesn't, and it is the
path that loses the user's unsaved work when it dies. (Introduced by M8.)

**Fix.** `try`/`catch` around the write, reporting through the existing `fileIoStatus` string.

**Fix applied.** Wrapped, matching Import JSON's existing pattern; failures now surface as
`"Export failed: ..."` in `fileIoStatus` instead of taking the process down.

### 4. HIGH — start point drifts and goes out of range on point deletion

`js/editor.js`'s `deleteSelected()` calls `preserveStartPoint()` (`js/editor.js:2914`) and
`deleteSelectedCurve()` calls `clampStart()` (`js/editor.js:3016`). `EditorState::deleteSelectedPoint`
does neither — it erases the point and clears the selection, leaving `track_.start` untouched.

```
start.point=5, delete index 1  -> start still 5, now refers to (-1333,0) not (-1154,666)   DRIFTED
start.point=11, delete 5 points -> 7 positions remain, start.point still 11                OUT OF RANGE
```

Core clamps an out-of-range start when baking, so this silently *moves the grid* rather than
failing — which is worse than an error.

**Fix.** Port `preserveStartPoint` (capture the start point object before the splice, re-derive its
index after) and `clampStart`.

**Fix applied.** Ported both, plus the position-index/raw-index split JS's `positionIndices()` and
`SelectedPoint::pointIndex` don't share (documented on the new `positionIndexToRaw` helper).
`deleteSelectedPoint` now captures the start point's id via `currentStartPointId()` before erasing,
then calls `preserveStartPoint(id)` after, which re-finds it by id (first match, same as
`findPointOccurrence`) or falls back to `clampStart()`. Also depends on finding 1's `backfillPointIds`
wiring -- without ids to match by, this degrades silently to clamp-only behavior, which is exactly
what the id-less `buildStarterTrack()` gap (finding 1) would have caused here too. Verified via
`--idprobe`: deleting an earlier point now moves `start.point` to keep pointing at the same
physical (unchanged) coordinates, and repeated deletion clamps into range rather than overrunning
it.

### 5. MEDIUM — orphaned mesh assets are never pruned on export

`serializeTrack` writes only `referencedMeshAssets(track)` (`track-core.js:1721`), dropping assets
no placement uses. `editor::toJson` writes every asset in the map:

```
place test-rect, then delete the placement:
  assets=1  placements=0  exported JSON still contains "test-rect"
```

Every imported-then-removed mesh accumulates in the file permanently.

**Fix.** Filter to referenced assets in `toJson`, mirroring `referencedMeshAssets`.

**Fix applied.** `toJson` now collects `placement.assetId` into a set and only serializes
`meshAssets` entries present in it. Verified: place-then-delete a mesh no longer leaves the asset
in exported JSON.

### 6. MEDIUM — mesh asset round-trip loses attributes

C++ drops `vertices[].attributes` and `polygons[].attributes` entirely, drops `holes` when empty,
and writes `edges[].attributes.rail: false` explicitly where JS omits the key:

```
.meshAssets.<id>.mesh.vertices[*].attributes:   MISSING IN CPP (js={})
.meshAssets.<id>.mesh.polygons[*].attributes:   MISSING IN CPP (js={})
.meshAssets.<id>.mesh.polygons[*].holes:        MISSING IN CPP (js=[])
.meshAssets.<id>.mesh.edges[*].attributes.rail: MISSING IN JS  (cpp=false)
```

Only `rail` is semantically used today, so this is currently harmless — but the native editor is a
lossy pass-through for any geometry-js mesh carrying vertex UVs, colours or per-polygon material
attributes, and `js/editor.js` deliberately preserves them (it stores the pristine mesh JSON and
re-serializes via `TrackMesh.meshToJSON`).

**Fix.** Retain unrecognized attribute objects verbatim per vertex/edge/polygon (an opaque
`nlohmann::json` blob on the record is enough) and emit them unchanged. At minimum, document the
loss in `EditorTrackDefinition.hpp`.

**Fix applied.** `MeshVertex`/`MeshEdge`/`MeshPolygon` each gained an `attributesJson` string field
(default `"{}"`, opaque serialized JSON text so the header doesn't need to depend on a JSON
library). `MeshEdge`'s holds everything *except* `rail`, which stays structured and gets merged
back in on write. `holes`/`attributes` are now always emitted (even empty), matching
geometry-js's `Mesh.toJSON`, which always writes both keys. Verified: all five random-mesh
fixtures now round-trip byte-identical to JS.

### 7. MEDIUM — `toWide()` mangles non-ASCII text and paths

`main.cpp:84`:

```cpp
std::wstring toWide(const std::string& text) { return std::wstring(text.begin(), text.end()); }
```

This widens *bytes*, not code points, so a UTF-8 track name (`Piste Français`) becomes mojibake in
the Save dialog's default filename. Symmetrically, `std::filesystem::path::string()` narrows to the
system ANSI codepage on MSVC, so non-ASCII paths returned by the file dialogs are corrupted before
they reach `stbi_load` (`TextureCache::get`) or the JSON writers — texture loading and saving both
fail for any path containing non-ASCII characters.

**Fix.** Use `MultiByteToWideChar(CP_UTF8, ...)` for the narrow→wide direction, and keep
`std::filesystem::path` in its native wide form end-to-end rather than round-tripping through
`.string()`. `TextureAsset.path` is a `std::string` in the schema, so it should hold UTF-8 and be
converted to wide at the `stbi_load`/`ifstream` boundary.

**Fix applied.** `FileDialog.hpp/.cpp` gained `utf8ToWide`/`wideToUtf8`/`pathToUtf8` (the last using
`path.native()` directly rather than round-tripping through the ACP, since a
`std::filesystem::path` already holds wide text on Windows). `toWide()` and every user-facing
`picked.path.string()` in `main.cpp`/`TexturePanel.cpp` now go through these. `TextureCache.cpp`
additionally defines `STBI_WINDOWS_UTF8` before including stb_image, so `stbi_load`/`stbi_info`
correctly interpret the now-UTF-8 `char const*` paths via `_wfopen` instead of ANSI `fopen`.
File-open calls themselves (`ifstream`/`ofstream` given a `std::filesystem::path` directly) needed
no change -- they already used the path's native representation, never `.string()`.

### 8. LOW — Import Mesh reports the wrong error for an unreadable file

`main.cpp`'s Import Mesh handler doesn't check the `ifstream`, so an unopenable file yields empty
text and falls through to `parseMeshAssetJson`'s clipboard-flavoured message: *"nothing to import
(the clipboard is empty)"*.

**Fix.** Check the stream and report a file-specific failure.

**Fix applied.** The `ifstream` is now checked before reading; an unopenable file reports
`"Mesh import failed: could not open <path>"` instead of falling through to the clipboard-flavoured
empty-text message.

### 9. LOW — `Browse…` texture failure is silent

`TexturePanel.cpp`'s M10 handler does nothing at all when `readImageSize` returns false (corrupt or
unsupported image) — no asset added, no status shown. `js/editor.js` surfaces an error.

**Fix.** Add a status line to the texture panel.

**Fix applied.** Added a `static` status string to `DrawTexturePanel` (same single-instance-UI
pattern already used for `TopDownCanvas.cpp`'s context-menu state), shown next to the Browse button
on failure and cleared on success.

### 10. LOW — `samples` divergence

`serializeTrack` never emits `samples`; `editor::toJson` always writes `"samples": 400`. Arguably JS
is the one dropping user data here (it parses `samples` but won't write it back). Pick one and make
both match; note `N_DEFAULT` is only the floor for physics sampling, so the field is close to inert.

**Fix applied.** Chose to match JS (the more conservative direction: only remove an emission,
never add a new field the reference doesn't have). `toJson` no longer writes `"samples"` at all;
`TrackDefinition::samples` still holds a loaded value in memory for the current session's live
preview bake, it just never gets written back out.

### 11. LOW — mesh placement id prefix differs

JS mints `m1, m2, …` (`newMeshPlacementId`); C++ mints `mesh1, mesh2, …`. Cosmetic on its own, but
worth aligning while fixing finding 1, which touches the same code.

**Fix applied.** Folded into finding 1's fix: the new `newMeshPlacementId()` scan mints `"m" +
N`, not `"mesh" + N`.

### 12. LOW / latent — `SelectedPoint::valid()` doesn't bounds-check

`valid()` only tests `pathIndex >= 0 && pointIndex >= 0`. `dragSelectedTo` and
`dragSelectedElevationTo` then index `track_.paths[...].points[...]` unchecked. Every structural
mutation currently clears the selection, so this is unreachable today — but it is a latent
out-of-bounds write one careless edit away.

**Fix.** Have `valid()` (or the two drag methods) check against the current track's extents.

**Fix applied.** Added `EditorState::selectionInRange()` (bounds-checks against the *current*
track, not just non-negativity) and switched `dragSelectedTo`, `dragSelectedElevationTo`, and
`deleteSelectedPoint` to use it instead of `selection_.valid()`. `SelectedPoint::valid()` itself
was left as a cheap presence check (still used correctly elsewhere, e.g. to decide whether *any*
point is selected for the props panel) rather than folding the bounds check into it.

## Functional gaps

Present in `js/editor.js`/`editor.html`, absent from the native editor. These are unimplemented
scope, not defects — listed so the remaining distance is visible. Roughly by impact:

1. **Roll / width / cross-section control point editing** — add, drag, delete, convert between
   types. The native editor edits *only* position points, so banking, width and cross-section
   profile are entirely unauthorable. This is the single largest gap.
2. **Track name editing.** No input exists, so M8's `sanitizeFilenameStem` always resolves to
   `New_Track.json`.
3. **Zones** (boost / start grid) — add, select, drag, delete.
4. **Triggers** (dummy / checkpoint) — add, select, drag, delete, role and direction.
5. **Curve management** — curve selector, Delete Curve, Connect/join, make-disjoint/reconnect,
   junctions, self-intersection overrides.
6. **Direction toggle and start-point selection** (`#dirBtn`).
7. **Handling panel** — maxSpeed/accel/turnSpeed/weight. The schema fields round-trip; there is no
   UI.
8. **Random ranges panel** — JS exposes twelve configurable bounds with persistence; `RandomTrack.cpp`
   hardcodes them.
9. **Grid display, grid size, snap-to-grid.**
10. **Render modes** (banked / flat / elevation), point-type filters, physics-sample overlay.
11. **Segment selection, deletion and splitting; insert-point-on-segment.**
12. **Elevation view roll points** — the orange roll line and its draggable diamonds.
13. **Add-point context menu.** M9 added a minimal right-click popup with only "Paste Mesh"; JS
    offers position/roll/width/crossSection plus zone and trigger creation.
14. **Undo/redo button disabled state.** JS disables them when the stacks are empty; the native
    buttons always render active.

## Suggested order (as executed)

Findings 1–4 first (silent data corruption, and the crash-losing-unsaved-work case), then 5–7
(export fidelity/encoding), then 8–12 as cleanup — all now done, in that order, in one pass.
Finding 1's fix uncovered an undocumented second gap (`buildStarterTrack()`/New/Random never
backfilled ids the way `fromJson` now does) that finding 4's fix also depended on; see finding 1's
"Fix applied" note.

**Gaps 1 and 2** (roll/width/cross-section editing, track name editing) remain the largest
usability blockers and are still open — they're new scope, not bug fixes, and weren't attempted
here.

## Regression coverage

`main.cpp` gained a new startup "Parity-fix smoke check" (`runParitySmokeCheck`), following the
project's per-milestone smoke-check convention: it covers findings 1 (no id collision across a
load-then-create), 4 (start point survives a preceding deletion, and clamps into range after
several), and 5 (an orphaned mesh asset doesn't survive export) directly against `EditorState`, the
same way `TopDownCanvas.cpp` drives it. This is now permanent and runs every time `track_editor`
starts.

**Still worth adding**, not done here:

- A **committed** version of the JS↔C++ authoring round-trip harness used to verify this pass (see
  "How this was tested" above) — this is the check that would have caught findings 2, 5, 6 and 10
  the day they were introduced, and would catch a regression in any of them going forward. It fits
  the existing `npm run parity` pattern: a `tools/` script plus a headless `--roundtrip` mode on
  `track_editor` (removed after this session; trivial to re-add, see "How this was tested"), gated
  on `cpp/build` existing the way `tools/parity.mjs` already is.
- Finding 3's uncaught-exception fix and finding 7's encoding fix are UI/filesystem-error-path
  specific and don't have automated coverage; they were verified by code inspection and a clean
  rebuild, not a regression test.
