# Track model list — implementation plan

Branch: `track-model-list` (not yet created; current branch `drivable-mesh-objects`
should land/merge first, since this plan renames and repoints its
`DrivableMeshObjectPlacementDefinition`).

## Goal

Replace a Track resource's single `<ModelFile>`/`<TrackData>` pair with a
`<Models>` list, each `<Model>` carrying a `.mppmodel` reference, optional
`TrackData`, and per-mesh `<Meshes><Mesh><Type>/<Visible></Mesh></Meshes>`
metadata — embedded directly inside the Track's own `<Definition>`, per
`D:\Code\Projects\example_track_def.xml`. `model-tool` gains the ability to
load/save a standalone `<Model>` XML fragment or one embedded in a Track
resource, and to author each mesh's `Type`/`Visible` metadata. The editor
gains the ability to load a full `<Track>`, render every embedded model, and
replace "Place Drivable Mesh Object" with "Load Model" — picking any `<Model>`
XML, embedding it into the current Track, and placing instances of it with
their own transform, referencing the embedded model by id.

This supersedes the requirements-analysis pass recorded in the grilling
session on 2026-08-04; all open decisions from that pass are resolved below
and are not re-litigated here.

## Decisions locked in (see conversation record, not repeated here in detail)

- **ADR 0001 D1 is reversed.** `model-tool` gains a real XML/Resource-file
  layer. A new ADR (`docs/adr/0003-model-xml-layer.md`, Milestone 0) records
  this supersession.
- **Models are embedded, not shared resources.** `<Model>` fragments live
  directly inside a Track's own `<Definition><Models>` list — never as a
  separate `<Resource type="Model">` that multiple Tracks reference by name.
  Each Track's XML is fully self-contained.
- **Standalone Model XML root is bare `<Model>`.** No `<Definition>`/
  `<Resources>` wrapper — the same fragment shape whether standalone or
  embedded.
- **Explicit `id` on embedded `<Model>` entries**, generated fresh by the
  editor at embed-time. Standalone Model XML files never carry an id.
  Placements reference a model by this id, not by path.
- **Mesh `Type` enum: `Track`, `Physical`, `Decorative`.** Any Model
  containing a `Type=Track` mesh must have `<TrackData>`; no cap on how many
  Track-type Models one Track resource embeds.
- **Multiple Track-type Models union into one `tox::Track`** — each
  contributes its own TrackData JSON's paths/zones/triggers/geometry, merged
  with per-source id namespacing.
- **`Visible=false` still renders in the editor, hidden only in the actual
  game/runtime.**
- **XML per-mesh metadata fully replaces the `~decorative` mesh-name-suffix
  flag.** `CollidableFlag.hpp`'s encoding is retired.
- **model-tool's Open dialog auto-detects** `.mppmodel` / standalone Model
  XML / Track resource XML (prompting which embedded `<Model>` to edit if
  more than one); Save writes back to wherever it was opened from.
- **Editor's "Load Model" embeds, dedups by ModelFile, and reuses** an
  already-embedded entry rather than duplicating it; the editor can itself
  edit an embedded Model's per-mesh metadata (not model-tool-exclusive).
- **The primary Track-type Model stays special-cased** — no placement/
  transform, it bakes directly into the track's own coordinate space. Only
  Physical/Decorative Models go through the placement/instance mechanism.
- **`DrivableMeshObjectPlacementDefinition` is renamed** to
  `ModelPlacementDefinition` (covers Physical + Decorative instances now, not
  just "drivable" ones).
- **Clean breaking change, no migration.** The old single-`ModelFile`+
  `TrackData` XML shape is no longer accepted. Existing sample/example XML
  files get hand-updated.

## Architecture notes (decided while starting this plan)

**Models never enter `cpp/core`.** `<Models>`/per-mesh `Type`/`Visible` is an
outer-XML-only concept, parsed independently by the editor
(`TrackResourceDocument.cpp`, TinyXML2) and the host
(`MapTungstenMonoxideDefinitionFactory.cpp`, `wp::XmlNode`) — the same "two
independent consumers, one documented format" split `DRIVABLE_MESH_OBJECTS_PLAN.md`
already established for the collidable-flag convention, now extended to the
whole `<Models>` shape. `cpp/core` keeps treating a placement's `modelId` as
an opaque authored string (architecture note in that plan: "`modelId` plus
its 6-DOF transform... nothing else") — only its *meaning* changes (an
embedded-Model id instead of a path), which is invisible to `core`. So `core`
needs exactly one new capability: unioning several already-resolved TrackData
JSON files into one `tox::Track`, described below. No `ModelDefinition`
struct, no `Type`/`Visible` enum, is added to `TrackDefinition.hpp`.

**Multi-TrackData union happens at the authored-JSON level, before baking.**
Rather than baking N times and merging compiled `Frame`/`Zone`/`Trigger`
records, a new entry point loads each source's `TrackDefinition` from JSON,
concatenates their `paths`/`zones`/`triggers`/etc. arrays (namespacing every
id with a `<source-index>:` prefix to keep them collision-free), and runs the
existing `normalize()`+bake pipeline exactly once over the merged
`TrackDefinition`. This reuses every line of `TrackBake.cpp` unchanged and
avoids inventing a second, compiled-level merge path.

**Editor gets a lightweight *reader*, not the full mpp engine.** The grilled
decision requires the editor to parse and render embedded `.mppmodel`
geometry — but `cpp/editor` links no part of the MassivePolyPusher SDK today
(`model-tool`'s `mpp::ModelSerializer` route pulls in `mpp`/`mpp-helper`/
`mpp-mesh` prebuilt libs and a GPU shader pipeline the editor has no other use
for). `cpp/editor/src/MppModelExport.cpp` already proves a real *writer* can
be built from scratch with zero mpp dependency; this plan adds the mirror
image — a from-scratch, read-only `.mppmodel` parser (vertex/index/mesh-name
data only, no materials/shaders) — rather than linking the mpp engine into
the editor. This keeps the editor's "no mpp SDK dependency" property intact
and sidesteps a much larger, riskier integration than this feature actually
needs. `model-tool` keeps using the real `mpp::ModelSerializer` for its own
authoritative save path; the editor's reader only ever needs to be
byte-compatible with what that writer/serializer already produces.

**A new shared library, `cpp/model-xml`,** holds the `<Model>` fragment's
schema types and TinyXML2 read/write functions (`ModelXmlDefinition`,
`MeshMetadataXmlDefinition`, `parseModelXml`/`writeModelXml`), linked by both
`cpp/editor` and `cpp/model-tool` so the standalone-vs-embedded fragment
format has exactly one implementation between the two tools that both read
and write it (unlike the editor/host XML split above, these two *do* already
share a build closure and a TinyXML2 dependency, so there's no reason to
duplicate this one).

Work one step at a time. After each step: build, run `ctest --test-dir
cpp/build -C Release --output-on-failure`, and commit before moving to the
next step.

---

## Milestone 0 — ADR

**0.1 — Write `docs/adr/0003-model-xml-layer.md`** — done (`27ca622`)
- Records the ADR 0001 D1 reversal: why model-tool now needs an XML/Resource
  layer, referencing this plan and the grilling-session decision. Marks ADR
  0001 D1 as superseded (don't edit 0001's own text — add a superseded-by
  note, matching how schema version bumps are handled elsewhere).
- Also records why the editor gets its own from-scratch, no-mpp-dependency
  `.mppmodel` reader (Milestone 4) instead of linking `mpp::ModelSerializer`,
  and that `cpp/model-xml` is a narrow fragment schema, not a full willpower
  Resource system — D1's "no declarative resource files" reasoning still
  applies at that larger scope.

---

## Milestone 1 — `cpp/core`: placement rename + multi-source union

**1.1 — Rename `DrivableMeshObjectPlacementDefinition` → `ModelPlacementDefinition`** — done (`46af74e`)
- Files: `cpp/core/include/TrackDefinition.hpp`, `cpp/editor/include/EditorTrackDefinition.hpp`,
  every call site (`TrackLoader.cpp`, `TrackBake.cpp`, `EditorTrackDefinition.cpp`,
  `EditorState.hpp`, `PropertiesPanel.cpp`, `TopDownCanvas.cpp`, and
  `tungsten-monoxide/{src,include}/TrackCollisionBuild.{cpp,h}`, found via
  `grep -rl DrivableMeshObjectPlacementDefinition cpp/`). `main.cpp`/`Map.cpp`
  turned out to have no direct references (only through the renamed type).
- Field/comment update only, as planned — `modelId`'s type/role stayed a
  plain authored string; both structs' doc comments now say `modelId` will
  name an embedded `<Model id>`, not a raw path. `EditorState`'s method
  names (`addMeshObjectPlacement`/etc.) were kept as-is, not renamed — they
  already read as placement-generic.
- Test: `ctest` (4/4 suites), plus a full workspace build (`track_editor`,
  `TungstenMonoxide`, `model_tool`) to confirm no other consumer broke.
  Commit.

**1.2 — Multi-source TrackData union** — done (`69dfbb4`)
- Files: `cpp/core/include/Track.hpp`, `cpp/core/src/TrackLoader.cpp` (added
  directly here, in the same translation unit as the anonymous-namespace
  `normalize()` it calls, rather than exposing `normalize()` through a new
  header).
- `Track::fromTrackDataFiles(const std::vector<std::filesystem::path>&,
  bool detectSelfIntersections = true)`: loads+normalizes each path
  independently, namespaces every id a source *owns* (paths/points/
  reservations/textureAssets/meshObjects/zones/triggers/disjointSeams/
  junctions/selfIntersectionOverrides) plus same-source references to them
  with an `"<index>:"` prefix, concatenates the authored lists into one
  `TrackDefinition`, then bakes exactly once. `ModelPlacementDefinition::modelId`
  is deliberately never namespaced (see 1.1) — it names an embedded
  `<Model id>` in the enclosing Track resource's `<Models>` list, an
  outer-XML concept this source doesn't own. No namespacing at all is
  applied for a single-path call, guaranteeing byte-identical output to
  `fromFile`. Singular fields (name/samples/handling/start/version) come
  from the first source only.
- Test: `track_tests.cpp` — single-source call matches `fromFile` exactly
  (path id, centerline size, zone/trigger counts); loading the SAME fixture
  twice as two sources (a strong collision test, since its own ids are
  identical across both "files") doubles every list with distinct
  `"0:"`/`"1:"`-prefixed ids and bakes cleanly. `ctest` (4/4). Commit.

---

## Milestone 2 — `cpp/model-xml`: shared `<Model>` fragment schema — done (`acfc972`)

**2.1 — New CMake target** — done
- Files: `cpp/model-xml/CMakeLists.txt` (new static lib, TinyXML2 via
  `Willpower::Common` — no vendored TinyXML2 copy of its own, no mpp/AssImp/
  SDL, mirroring `cpp/core`'s "no heavyweight deps" posture), `cpp/CMakeLists.txt`
  (added `add_subdirectory(model-xml)` ahead of `editor`/`model-tool`, plus a
  header-comment entry).
- `include/ModelXml.hpp` matches the planned shape (`MeshType`,
  `MeshMetadataXmlDefinition`, `ModelXmlDefinition`) — `tinyxml2::XMLElement`
  is only forward-declared in the header, so consumers of just the struct
  types don't need a TinyXML2 include.

**2.2 — Parse/write functions** — done, folded into 2.1's commit
- Files: `cpp/model-xml/src/ModelXml.cpp`, `tests/model_xml_tests.cpp` (new
  CTest target `model_xml_tests`, plus a post-build `Willpower.Common.dll`
  copy step — needed here since, unlike `editor_track_resource_tests`, this
  target has no sibling executable already copying it into the same output
  directory).
- `parseModelFragment`/`writeModelFragment` operate on an already-located
  `<Model>` element exactly as planned; `validateModelDefinition()` (the
  "`Type=Track` requires `TrackData`" rule, plus a non-empty `ModelFile`
  check) is called from both, and from `saveStandaloneModelXml` before any
  file is touched.
- Test: round-trip through `writeModelFragment`→`parseModelFragment`; the
  exact `Definition/Models/Model×2` nesting shape from
  `example_track_def.xml`; standalone save→load drops any `id` the caller
  set and writes no `id` attribute at all; the Track-mesh-needs-TrackData
  validation failure/success cases. `ctest` (5/5 suites — this raised the
  count from 4). Commit.

---

## Milestone 3 — `model-tool`: XML layer

**3.1 — Link `cpp/model-xml`**
- File: `cpp/model-tool/CMakeLists.txt`.
- Test: `ctest` (build-only change). Commit.

**3.2 — Retire `CollidableFlag.hpp`'s name-suffix encoding**
- Files: `cpp/model-tool/include/CollidableFlag.hpp`/`src/CollidableFlag.cpp`
  (deleted), `src/MppSave.cpp`, `src/MppModelImport.cpp`, `main.cpp`,
  `tests/model_tool_tests.cpp` (round-trip tests removed).
- Per-mesh state in `model-tool`'s in-memory model moves from a bare
  `collidable` bool to a `MeshType`+`visible` pair (from `cpp/model-xml`);
  `.mppmodel` mesh names are written/read completely unchanged now — the
  metadata that used to ride in the name lives only in the associated XML
  (standalone or embedded) from here on. A `.mppmodel` with no associated
  XML at all has no Type/Visible metadata (not "every mesh collidable" as
  before) — surfaced as "Physical/visible" defaults in the UI until XML
  metadata is loaded or authored.
- Test: `ctest` (existing collidable-flag round-trip tests removed, no
  replacement needed — the concept no longer exists at the `.mppmodel`
  layer). Commit.

**3.3 — Open-dialog auto-detection**
- Files: `main.cpp` (open flow), new `src/OpenTarget.cpp`/`.hpp` (peeks at
  file content/extension to classify: `.mppmodel` magic bytes → raw model;
  XML with root `<Model>` → standalone; XML with root `<Resources>...
  factory="Track"` → Track resource, prompting a picker over its `<Models>`
  list if more than one entry).
- Whichever path was opened is remembered so Save writes back to the same
  place: raw `.mppmodel` → `MppSave.cpp`'s existing path; standalone Model
  XML → `saveStandaloneModelXml` (2.2) plus re-saving the referenced
  `.mppmodel` via the existing `mpp::ModelSerializer` path; embedded-in-Track
  → rewrite just that one `<Model>` element in place inside the Track
  resource XML (parse whole document, locate by id, replace element,
  re-serialize) plus the `.mppmodel`.
- Test: `model_tool_tests` — classification of the three input shapes from
  in-memory XML strings/fixture files; save-back-to-origin round trip for
  each of the three cases against a temp file. `ctest`. Commit.

**3.4 — Per-mesh Type/Visible authoring UI**
- File: `main.cpp` (Meshes panel).
- Replaces the old `Collidable` checkbox with a `Type` combo
  (Track/Physical/Decorative) and a `Visible` checkbox per mesh, backed by
  `ModelXmlDefinition::meshes`. Selecting `Type=Track` without a `TrackData`
  set surfaces the 2.2 validation error inline rather than only at save
  time.
- Test: `ctest` (UI change, no new headless-testable logic beyond what 3.2/
  3.3/2.2 already cover). Commit.

---

## Milestone 4 — Editor: `.mppmodel` geometry reader

**4.1 — From-scratch read-only parser**
- Files: `cpp/editor/include/MppModelImport.hpp`/`src/MppModelImport.cpp`
  (new — distinct from `cpp/model-tool`'s own `MppModelImport.cpp`, which is
  a *reimport-for-editing* path built on the real `mpp::ModelSerializer`;
  this one is a minimal geometry-only reader with zero mpp dependency,
  mirroring `MppModelExport.cpp`'s existing from-scratch *writer*).
- Reads vertex positions/normals/indices and per-mesh names/counts only — no
  materials, no shaders, no `mpp::ModelSerializer` involvement. Must
  correctly parse output written by both `MppModelExport.cpp` (baked track
  geometry) and `model-tool`'s real `mpp::ModelSerializer` save path (props)
  — round-trip-test against real files produced by each.
- Test: `editor_track_resources`/new test target — parse a `.mppmodel`
  written by `model-tool` in this session (a small fixture, e.g. a cube) and
  one written by `MppModelExport.cpp`, confirm vertex/triangle counts and a
  handful of spot-checked positions match what the writer put in. `ctest`.
  Commit.

**4.2 — Viewport rendering of embedded models**
- Files: `cpp/editor/src/TopDownCanvas.cpp` or a new `ModelRenderer.cpp`.
- Feeds 4.1's parsed geometry into the editor's existing OpenGL pipeline as
  ordinary triangle data, transformed by each placement's 6-DOF transform
  (reusing `placementTransformPosition`/`placementTransformNormal`-style math
  already established in `cpp/tungsten-monoxide/src/Map.cpp` for the same
  purpose — reimplemented independently per the existing "core/editor/host
  don't share code" convention, not extracted into a shared helper).
  `Visible=false` meshes ARE drawn here (editor-visible, per the locked-in
  semantics) — the game-hidden behavior is host-side only (Milestone 8).
- Test: `ctest` (no new headless-testable logic — this is draw-call wiring).
  Manual visual verification flagged as needed once a GUI session is
  available, matching this repo's established "no display in this
  environment" caveat pattern. Commit.

---

## Milestone 5 — Editor: `<Models>` list parsing/saving

**5.1 — Link `cpp/model-xml`; parse `<Models>` list**
- Files: `cpp/editor/CMakeLists.txt`, `cpp/editor/src/TrackResourceDocument.cpp`/
  `include/TrackResourceDocument.hpp`.
- `TrackResourceCandidate` gains `std::vector<ModelXmlDefinition> models`
  (via `cpp/model-xml`'s `parseModelFragment` called once per `<Model>` child
  of `<Models>`), replacing the current single `trackDataPath`/`modelFile`.
  Old bare `<TrackData>`/`<ModelFile>` (no `<Models>` wrapper) fails to parse
  with a clear error — no migration, per the locked-in decision.
- Test: `editor_track_resources` — a fixture Track resource XML mirroring
  `example_track_def.xml` (two models, one Track-type + TrackData, one
  Physical) parses into the expected candidate; a legacy-shape fixture fails
  with a clear "old Track schema no longer supported" error. `ctest`.
  Commit.

**5.2 — Multi-model load via `Track::fromTrackDataFiles`**
- Files: `cpp/editor/main.cpp` (`loadTrackCandidate` and friends).
- Collects every Track-type model's `trackData` path from the parsed
  candidate and calls Milestone 1.2's new entry point instead of a single-
  file load. `EditorState`/`EditorTrackDefinition` need no change for this
  part — they still work off one merged `editor::TrackDefinition`, same as
  today, just sourced from N files instead of one.
- Test: `ctest` plus a smoke-launch check loading a fixture with two
  Track-type models, confirming paths/zones from both are present and
  editable. Commit.

**5.3 — Save: write the `<Models>` list back**
- Files: `cpp/editor/src/TrackResourceSave.cpp`, `include/MppModelExport.hpp`/
  `src/MppModelExport.cpp` (`buildTrackResourceXml` reworked to emit
  `<Models>` via `cpp/model-xml`'s `writeModelFragment` per entry instead of
  the old single `<TrackData>`/`<ModelFile>` pair).
- Each Track-type model's own TrackData JSON is written back to its own
  file (the merged in-memory `TrackDefinition` needs to be split back apart
  by its namespace prefix — the inverse of 1.2's merge — before writing;
  add this as a `Track`/`TrackDefinition`-level helper in `cpp/core` rather
  than duplicating split logic in the editor).
- Test: `editor_track_resources` — round-trip save→load of a two-Track-type-model
  fixture reproduces the original per-file TrackData content exactly.
  `ctest`. Commit.

---

## Milestone 6 — Editor: "Load Model" flow

**6.1 — Replace "Place Drivable Mesh Object..." menu item**
- File: `cpp/editor/main.cpp`.
- New "Load Model..." item opens a file picker over `.mppmodel` or
  standalone Model XML (auto-detected, same classification approach as
  model-tool's 3.3 — reuse `cpp/model-xml` directly here rather than
  reimplementing detection). If the resulting `ModelFile` already matches an
  entry in the current Track's embedded `<Models>` list, reuse that entry's
  id (dedup, per the locked-in decision); otherwise embed a new `<Model>`
  entry with a freshly generated id.
- Then creates a `ModelPlacementDefinition` (Milestone 1.1) referencing that
  id, placed at the current view center — same UX as today's mesh-object
  placement, just resolved through an id instead of a raw path.
- Test: `ctest` — headless check (mirroring the existing M3/M5/etc. built-in
  main.cpp smoke checks) that loading the same Model XML twice produces one
  embedded `<Model>` and two placements, and loading two different Model
  XMLs produces two embedded entries. Commit.

**6.2 — Properties panel: per-mesh metadata editing**
- File: `cpp/editor/src/PropertiesPanel.cpp`.
- Alongside a placement's existing transform fields, a Type/Visible editor
  for its referenced embedded Model's meshes (shared list, so editing it
  from one placement's panel affects every placement referencing the same
  embedded Model — consistent with "embedded Model is the shared metadata
  set, placements only add a transform").
- Test: `ctest` (UI only). Commit.

---

## Milestone 7 — Host (`tungsten-monoxide`): `<Models>` list + Type-driven resolution

**7.1 — Parse `<Models>` list (independent `wp::XmlNode` implementation)**
- File: `cpp/tungsten-monoxide/src/MapTungstenMonoxideDefinitionFactory.cpp`.
- Mirrors 5.1's parsing logic against the same documented `<Model>` fragment
  shape, but reimplemented on `wp::XmlNode` rather than linking
  `cpp/model-xml` (TinyXML2-based) — consistent with today's existing split
  and this repo's stated preference for independent reimplementation over a
  cross-engine shared dependency at this boundary.
- Test: existing `tungsten-monoxide` build/test path (per `cpp/README.md`);
  no CTest coverage lives in this module today per prior milestones'
  precedent — note this as an existing gap, not a new one introduced here.

**7.2 — Multi-model load via `Track::fromTrackDataFiles`**
- File: `cpp/tungsten-monoxide/src/Map.cpp`.
- Replaces the single `tox::Track::fromFile(dataPath)` call (`Map.cpp:190`)
  with `Track::fromTrackDataFiles(trackTypeModelPaths)`.

**7.3 — Placement resolution by embedded-Model id, Type-driven collision**
- File: `cpp/tungsten-monoxide/src/Map.cpp`.
- `modelId` on a placement now looks up the enclosing Track resource's
  parsed `<Models>` list by id (not a relative-path `safeRelativePath` call
  as today) to find the referenced `.mppmodel`'s path and its per-mesh
  Type/Visible metadata. Collision-mesh building keys off `Type=Physical`
  (was: name-suffix-decoded `collidable=true`); `Type=Track`/`Decorative`
  meshes on a placement are not expected in practice (Track-type meshes only
  ever appear on the special-cased primary model, never a placement) but
  `Decorative` explicitly excludes from collision the same way the old
  `~decorative` suffix did.
- Rendering: `Visible=false` meshes are skipped entirely (game-hidden, per
  the locked-in semantics — unlike the editor's always-render-everything
  behavior from 4.2).
- Test: extend `cpp/app/tools/mesh_physics_diag.cpp`'s headless harness (per
  this repo's "all physics testing is headless" convention) with a fixture
  using the new `<Models>`-list shape; confirm collision triangles appear
  for a `Physical` mesh and not for a `Decorative` one. Commit.

---

## Milestone 8 — Fixtures, docs, cleanup

**8.1 — Update/replace fixtures**
- `cpp/test-data/fixtures/mesh-object/basic-placement.json` and any Track
  resource XML fixtures under `cpp/test-data/` or `cpp/editor/`'s own test
  assets get rewritten to the new `<Models>`-list shape; add a new fixture
  mirroring `example_track_def.xml` exactly (two models, Track+Physical).

**8.2 — Docs**
- `docs/adr/0001-model-tool.md`: add a superseded-by note pointing at
  `0003-model-xml-layer.md` for D1 specifically (other decisions in 0001
  stand).
- `docs/adr/0002-track-resource-save-load.md`: update for the `<Models>`
  list shape (this ADR documents exactly the schema this plan changes).
- `cpp/core/CLAUDE.md`, `cpp/editor/CLAUDE.md`, root `CLAUDE.md`,
  `docs/UBIQUITOUS_LANGUAGE.md` (add **Model**, **Model placement**; retire
  "drivable mesh object" as the placement's name where it now means
  something broader), `DRIVABLE_MESH_OBJECTS_PLAN.md` (note the later
  rename of its own `DrivableMeshObjectPlacementDefinition`).

**8.3 — Full regression pass**
- `ctest --test-dir cpp/build -C Release --output-on-failure`, full
  `track_editor.exe`/`model_tool.exe` smoke launches, matching the existing
  "Milestone 8.2/8.3" precedent from `DRIVABLE_MESH_OBJECTS_PLAN.md`.
