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

## Milestone 3 — `model-tool`: XML layer — done (`4447a1d`, `19a68a9`)

**3.1 — Link `cpp/model-xml`** — done
- File: `cpp/model-tool/CMakeLists.txt` (`model_tool` and `model_tool_tests`
  both link `model_xml`). It turned out `model-tool` already linked
  `Willpower::Common` (for `MaterialXmlImport.hpp`'s XmlReader), so this
  added no new third-party dependency, just the new fragment-schema lib.

**3.2 — Retire `CollidableFlag.hpp`'s name-suffix encoding** — done
- Files: `include/CollidableFlag.hpp`/`src/CollidableFlag.cpp` (deleted),
  `src/MppSave.cpp`, `src/MppModelImport.cpp`, `include/AssImpImport.hpp`
  (`ImportedMesh::collidable` → `modelxml::MeshType type` + `bool visible`),
  `tests/model_tool_tests.cpp` (round-trip tests removed; see
  `model_xml_tests.cpp`, already landed in Milestone 2, for the coverage
  that replaced it).
- Landed exactly as planned: mesh names are now always written/read
  unchanged; a `.mppmodel` with no associated XML gets in-memory
  Physical/visible defaults, not "every mesh collidable."

**3.3 — Open-dialog auto-detection** — done
- Files: new `include/OpenTarget.hpp`/`src/OpenTarget.cpp` (classification
  by extension for `.mppmodel`, by parsed root element for `.xml` —
  `<Model>` vs `<Resources>` containing a `Definition[factory=Track]/Models`
  list, found via an independent re-walk rather than depending on
  `cpp/editor`'s `TrackResourceDocument.cpp`), `main.cpp` (`doOpen`
  dispatches via `classifyOpenTarget`; a multi-entry Track resource queues a
  new "Choose Model" picker modal, mirroring the existing Material Name
  Conflicts modal's `OpenPopup`/`BeginPopupModal` pattern).
- Save-back landed as `ModelXmlOrigin` (`None`/`StandaloneXml`/
  `EmbeddedInTrackResource`) plus a `doSave()`/`doSaveAs()` split: `doSave()`
  (now what Ctrl+S/the "Save" menu item call) writes back to the remembered
  origin silently when one is known — re-saving the `.mppmodel` and
  rewriting the associated `<Model>` fragment's mesh metadata via
  `rewriteEmbeddedModel`/`saveStandaloneModelXml` — and falls back to the
  always-prompting `doSaveAs()` (the old "Save As .mppmodel..." behavior,
  renamed but otherwise unchanged) when there's no origin yet.
- Test: `model_tool_tests` — classification of all three input shapes;
  `scanTrackResourceModels`/`readEmbeddedModel`/`rewriteEmbeddedModel`
  against an in-memory Track resource XML (confirms an unrelated XML
  comment elsewhere in the document survives a rewrite untouched, and a
  missing-id lookup throws). `ctest` (5/5 suites). Manual GUI verification
  (Open dialog, Choose Model modal, Save round trip) wasn't possible in this
  environment (no display) — flagged as the thing to actually click through
  once a GUI session is available. Commit.

**3.4 — Per-mesh Type/Visible authoring UI** — done, folded into 3.2's commit
- File: `main.cpp` (Meshes panel).
- The old `Collidable` checkbox is replaced by a `Type` combo
  (Track/Physical/Decorative) and a `Visible` checkbox per mesh. Selecting
  `Type=Track` shows an inline "requires a TrackData file on this Model"
  hint; the hard validation itself (2.2's `validateModelDefinition`) fires
  at Save time via `doSave`/`doSaveAs`'s existing try/catch, surfaced as a
  status-bar "Save failed" message — no separate inline validation pass was
  added beyond that hint, since the save-time throw already covers
  correctness and the hint covers discoverability.

---

## Milestone 4 — Editor: `.mppmodel` geometry reader — done (`7c11718`, `7ba6f49`)

**4.1 — From-scratch read-only parser** — done
- Files: `cpp/editor/include/MppModelImport.hpp`/`src/MppModelImport.cpp`
  (new — distinct from `cpp/model-tool`'s own `MppModelImport.cpp`, a
  *reimport-for-editing* path built on the real `mpp::ModelSerializer`; this
  one is a minimal geometry-only reader with zero mpp dependency, mirroring
  `MppModelExport.cpp`'s existing from-scratch *writer*), new
  `tests/mpp_model_import_tests.cpp` target.
- The on-disk format was verified field-for-field against the real
  `ext/massivepolypusher/mpp/src/ModelSerializer.cpp` write*()/read*()
  functions directly (`MPPMODEL_EXPORT_SPEC.md` isn't present in-repo, so
  that vendored source is the actual ground truth) rather than inferred from
  `MppModelExport.cpp`'s writer alone. Reads vertex positions/normals/UVs
  and per-mesh names/materials/indices only; Materials/MaterialNames
  sections are directory-skipped, never parsed.
- Test: round-trips `MppModelExport.cpp`'s own non-indexed output; since a
  live `model-tool` session wasn't available to generate a real file in this
  environment (no display), a minimal real-format INDEXED `.mppmodel` was
  instead hand-built byte-for-byte per `ModelSerializer.cpp`'s actual write
  functions (16-bit index stream) and confirmed to unpack to the expected
  triangle list — exercising the one code path `MppModelExport.cpp`'s own
  output never does. `ctest` (6/6 suites). Commit.

**4.2 — Viewport rendering of embedded models** — done, scoped down (no
  OpenGL/mpp work needed at all)
- File: `cpp/editor/src/TopDownCanvas.cpp` (plus `TopDownCanvas.hpp`/
  `main.cpp` for a new `modelBaseDir` parameter).
- Turned out the editor's canvas has no real 3D OpenGL rendering pipeline in
  the first place — every shape on it (`drawBakedPath`, `drawZones`, the
  placement marker itself, etc.) is a flat 2D shape filled via ImGui's
  `ImDrawList` (`AddTriangleFilled`/`AddConvexPolyFilled`), projected through
  the active `ProjectionMode` plane via the existing `worldToScreenPlane`.
  `drawMeshObjectPlacements` now does the same for every triangle of a
  placement's real geometry (loaded via 4.1's reader, cached per resolved
  path for the process's lifetime — no on-disk-change invalidation, an
  accepted limitation), transformed by a new `placementTransformPosition`
  mirroring `TrackBake.cpp`/`Map.cpp`'s own scale-then-yaw/pitch/roll-then-
  translate convention (reimplemented independently, as planned). The
  existing diamond marker + facing line still draws on top as a selection
  handle. `Visible=false` meshes are NOT special-cased yet (Milestone 6/7's
  concern once per-mesh metadata is actually wired up) — every mesh in the
  file renders unconditionally for now.
- `modelId` is resolved exactly as "Place Drivable Mesh Object" already
  does today (relative to the current save location's directory, via a new
  `modelBaseDir` parameter threaded through `DrawTopDownCanvas`) — not
  wasted work even once Milestone 6 gives placements a real embedded-Model-
  id lookup instead of a raw path, since only the caller producing the
  resolved path changes, not this renderer/cache.
- Test: `ctest` (6/6 suites, no new headless-testable logic — this is
  draw-call wiring); a `track_editor.exe` smoke launch confirmed every
  built-in self-check still reports `OK` with zero `MISMATCH`. Manual visual
  verification of the rendered geometry itself wasn't possible in this
  environment (no display) — flagged as the thing to actually look at once
  a GUI session is available. Commit.

---

## Milestone 5 — Editor: `<Models>` list parsing/saving — done (`9ff771a`), scoped down

**Scope revision, decided while starting this milestone:** the editor
continues to author exactly ONE Track-type Model per session —
`EditorState` stays single-`TrackDefinition`, as today. True multi-document
editing of several independently-baked TrackData files (5.2 as originally
scoped) would mean rearchitecting `EditorState`'s undo/redo and every panel
around N simultaneously-open documents, out of proportion to what this
branch's actual user stories (embedding Physical/Decorative props) need.
`Track::fromTrackDataFiles` (Milestone 1.2) is unused by the editor under
this revision — it remains for the **host** (Milestone 7) to union multiple
Track-type Models' TrackData, a scenario the editor itself never needs to
construct or edit. What Milestone 5 actually delivers: the outer Track
resource XML shape changes to `<Models>`, and non-primary `<Model>` entries
(Physical/Decorative props, Milestone 6's "Load Model") round-trip through
the editor as opaque pass-through data it preserves but never edits.

**5.1 — Link `cpp/model-xml`; parse `<Models>` list** — done
- Files: `cpp/editor/CMakeLists.txt` (`track_editor` and both test targets
  link `model_xml`), `cpp/editor/src/TrackResourceDocument.cpp`/
  `include/TrackResourceDocument.hpp`.
- `TrackResourceCandidate` gained `std::vector<modelxml::ModelXmlDefinition>
  models` plus `primaryModelIndex` — the existing `trackDataReference`/
  `modelFileReference`/`trackDataPath`/`modelFilePath` fields are kept,
  mirroring `models[primaryModelIndex]` (the first entry with any Type=Track
  mesh), so every pre-existing call site elsewhere in the editor needed no
  change. A `<Models>`-less `Definition[factory=Track]` (the old bare
  `<TrackData>`/`<ModelFile>` shape) fails to parse with an explicit error —
  no migration, per the locked-in decision.
- Test: `editor_track_resources` (extended, see 5.3 below — the fixture
  work and the save-side work landed together in one pass, not as separable
  commits, since a save→load round trip is what actually exercises the
  parse side meaningfully). `ctest`. Commit.

**5.2 — Multi-model load via `Track::fromTrackDataFiles`** — superseded by
  the scope revision above; not implemented. The editor's own bake path
  (`main.cpp`'s existing `editor::TrackDefinition` → `tox::Track::fromJson`
  flow) is completely unchanged.

**5.3 — Save: write the `<Models>` list back** — done, landed together with
  5.1 in one commit
- Files: `cpp/editor/src/TrackResourceSave.cpp`, `include/MppModelExport.hpp`/
  `src/MppModelExport.cpp`.
- `buildTrackResourceXmlForName` gained `primaryModelId`/`otherModels`
  parameters: it regenerates the primary Model fresh every save (a
  `<Meshes><Mesh>Type=Track</Mesh>` entry for every baked collidable batch
  id, built via a new `buildModelsXml` helper that constructs a small
  TinyXML2 subtree and calls `modelxml::writeModelFragment` — printed
  without trying to match the surrounding hand-built string XML's
  indentation, since `upsertTrackResource` reparses/reprints the whole
  document before anything reaches disk anyway) — **superseding the old
  flat `<TrackMeshes>` list entirely** (Milestone 7 migrates the host to
  read it this way; until then this is a real, expected, and already-planned
  breaking change to host-side loading, uncaught by any current CTest
  target). `otherModels` (everything except the primary) are written back
  via `writeModelFragment` completely unedited. The dead, unused
  `buildTrackResourceXml` legacy overload (no call sites anywhere) was
  deleted rather than threading the new parameters through code nothing
  called. `prepareTrackSave` sources the primary's stable id and the
  `otherModels` list from `matching` (the freshly rescanned candidate at the
  target Resource identity, not the passed-in `binding`) — mirrors ADR 0002
  D4's "Save replaces one complete Track Resource and preserves the rest,"
  generalized to per-Model granularity within `<Models>`.
- No `TrackDefinition`-splitting-by-namespace-prefix helper was needed (the
  scope revision above eliminated the multi-TrackData case this was meant
  to invert).
- Test: `editor_track_resources` — a freshly saved resource embeds exactly
  one Model with a stable non-empty id and Type=Track meshes matching
  `trackDataReference`; a hand-injected Physical Model (simulating
  Milestone 6, not yet implemented) survives a bound Save byte-for-byte
  (id/modelFile unchanged), with the primary's own id stable across saves;
  hand-editing the file first requires binding fresh off a rescan, not
  reusing a stale binding — the fingerprint guard correctly flags that as
  an external change, exactly as designed; the old bare-shape XML fails to
  load with an error naming `<Models>`. `ctest` (6/6 suites); a
  `track_editor.exe` smoke launch confirms every built-in self-check still
  reports `OK`. Commit.

---

## Milestone 6 — Editor: "Load Model" flow — done (`3896a98`)

**6.1 — Replace "Place Drivable Mesh Object..." menu item** — done
- Files: `cpp/editor/main.cpp`, `include/EditorTrackDefinition.hpp`
  (`TrackDefinition` gains `models`, outer-XML-only, excluded from
  fromJson/toJson — see this milestone's own header note above),
  `include/EditorState.hpp` (`loadModel`/`findModel`/`editEmbeddedModel`/
  `newModelId`).
- "Load Model..." opens a file picker over `.mppmodel` or standalone Model
  XML, classified by extension/parsed root element directly in `main.cpp`
  (reimplemented independently of `model-tool`'s own `OpenTarget.cpp`
  classification — the two apps don't share code at that layer, only the
  `cpp/model-xml` fragment schema both link). `EditorState::loadModel`
  reuses an existing `track_.models` entry whose `ModelFile` already matches
  (dedup, per the locked-in decision) rather than duplicating it, or embeds
  a fresh entry with a freshly generated `"model"`-prefixed id otherwise —
  one undo step for the whole embed-plus-placement operation, not two.
- Test: a new `LoadModelSmokeCheckResult`/`runLoadModelSmokeCheck()` in
  `main.cpp` (mirroring the existing Gap-N/M-N built-in checks) confirms
  loading the same Model twice dedups to one embedded entry with two
  placements, loading a different Model embeds a second entry, and editing
  shared mesh metadata is visible through every placement referencing it.
  `ctest` (6/6 suites); a `track_editor.exe` smoke launch confirms every
  built-in self-check reports `OK`, including this new one. Commit.

**6.2 — Properties panel: per-mesh metadata editing** — done, landed
  together with 6.1 in one commit
- File: `cpp/editor/src/PropertiesPanel.cpp`.
- Alongside a placement's existing transform fields, a Type/Visible editor
  for its referenced embedded Model's meshes, backed by
  `EditorState::editEmbeddedModel` — editing it from one placement's panel
  affects every placement referencing the same embedded Model, consistent
  with "embedded Model is the shared metadata set, placements only add a
  transform." A `Type=Track` selection shows an inline "requires a
  TrackData file on this Model" hint (the hard validation itself is
  `cpp/model-xml`'s `validateModelDefinition`, enforced at Save time).
- **Follow-on change to Milestone 5's save logic, made while implementing
  this**: `TrackResourceSave.cpp`'s `otherModels` (the non-primary `<Model>`
  entries written back on Save) now come from the in-session
  `track.models` — the editor is the authoritative live source once it can
  add to that list — rather than being re-derived from whatever's
  currently on disk (`matching`), which was only ever a stand-in for not
  having this milestone's storage yet. `editor_track_resources`' Milestone
  5 test was updated to exercise the real parse-seeds-`track.models` →
  save-writes-it-back round trip through `scanTrackResources`, not just an
  in-memory field poke.
- Test: `ctest` (no additional headless-testable logic beyond 6.1's own
  check and the updated `editor_track_resources` coverage — this is UI
  wiring). Commit.

---

## Milestone 7 — Host (`tungsten-monoxide`): `<Models>` list + Type-driven resolution — done (`5e43256`)

**7.1 — Parse `<Models>` list (independent `wp::XmlNode` implementation)** — done
- File: `cpp/tungsten-monoxide/src/MapTungstenMonoxideDefinitionFactory.cpp`.
- Landed as planned: independently reimplemented on `wp::XmlNode`
  (`getChild`/`getOptionalChild`/`getAttribute`/`getOptionalAttribute`/`next`/
  `getValue`), not linking `cpp/model-xml`. Exactly one `<Model>` may carry a
  Type=Track mesh (mirroring the editor's own single-primary scope); every
  `<Model>`, including that primary, is kept on the new
  `Map::mEmbeddedModels` (`TrackCollisionBuild.h`'s new `EmbeddedModelRef`/
  `ModelMeshMeta`/`ModelMeshType`) so placement resolution (7.3) can look
  any of them up by id. The old `<TrackMeshes>` parsing loop is deleted
  outright — see 7.3's note on why.
- Test: `TungstenMonoxide.dll` builds clean; no CTest coverage lives in this
  module (pre-existing gap, not new). `cpp/tungsten-monoxide/resources/Resources.xml`
  (real bundled game content, not a test fixture) was updated to the new
  `<Models>` shape in the same commit — it would otherwise fail to load at
  all under this milestone's parser change. Commit.

**7.2 — Multi-model load via `Track::fromTrackDataFiles`** — done
- File: `cpp/tungsten-monoxide/src/Map.cpp`.
- `tox::Track::fromFile(dataPath)` replaced with
  `tox::Track::fromTrackDataFiles({dataPath})` — a single-element,
  byte-identical call today (only one Type=Track Model is supported, per
  7.1's guard), but the correct entry point once that cap is ever lifted.

**7.3 — Placement resolution by embedded-Model id, Type-driven collision** — done
- Files: `cpp/tungsten-monoxide/src/Map.cpp`, `src/TrackCollisionBuild.cpp`/
  `include/TrackCollisionBuild.h`.
- **Scope finding that simplified this step**: the old `<TrackMeshes>`
  list's selection was always *required* to exactly equal
  `mono::collidableGeometryBatchIds(track)` (a set derived purely from the
  baked Track's own `GeometryKind`s) — `buildCollisionTriangles` already
  threw if they ever diverged. So rather than parsing the primary Model's
  own `<Meshes>` list for collision selection, `Map::load()` now just calls
  `collidableGeometryBatchIds(*mTrack)` directly (matching
  `mesh_physics_diag.cpp`'s own pre-existing pattern) — `<TrackMeshes>` is
  retired with no replacement parsing needed, losing no real validation
  (the actual safety net, cross-checking each mesh's physical vertex data
  against TrackData, is unaffected by where the name list came from).
- `modelId` resolution and Type/Visible metadata lookup landed as
  `resolveModelFileReference`/`findMeshMeta` (`TrackCollisionBuild.h`), used
  by both `buildMeshObjectCollisionTriangles` (collision: `Type=Physical`
  only) and `appendMeshObjectRenderMeshes` (rendering: `Visible=false`
  skipped — game-hidden, unlike the editor's always-renders-everything
  Milestone 4.2 behavior). Unknown metadata (mesh name not found) defaults
  to Physical/visible, matching `model-tool`'s own default.
- **`embeddedModels` defaults to empty and falls back to treating `modelId`
  as a literal relative path** when a placement's id isn't found in it (or
  the list is empty) — this is what let `cpp/app/tools/mesh_physics_diag.cpp`
  (which loads a CLI-supplied TrackData JSON directly, no Resources XML/
  `<Models>` list at all) keep working with **zero changes**, rather than
  the originally-planned "extend its headless harness with a `<Models>`-list
  fixture." That test extension was not implemented — collision-vs-
  decorative Type-driven behavior is exercised only implicitly (by
  inspection/build, not a dedicated headless assertion); flagged as a real
  gap, worth closing in Milestone 8 or a follow-up if `mesh_physics_diag.cpp`
  ever needs to validate a real `<Models>`-list Resources XML end to end.
- Test: `ctest` (6/6 suites — none of which touch this module);
  `TungstenMonoxide.dll`, `mesh_physics_diag`, `track_runner` all build
  clean, the latter two confirming `buildMeshObjectCollisionTriangles`'s new
  defaulted parameter doesn't break their existing 5-argument call site. A
  real game launch loading the updated `Resources.xml` wasn't possible in
  this environment (no display/GPU) — flagged as the thing to actually run
  once available. Commit.

---

## Milestone 8 — Fixtures, docs, cleanup — done

**8.1 — Update/replace fixtures** — done, turned out to be a no-op
- Checked `cpp/test-data/fixtures/mesh-object/basic-placement.json` and
  every other fixture under `cpp/test-data/`/`cpp/editor/`'s own test
  assets: none is a Track resource *XML* file (the `<Models>`-list shape
  this plan changed) — `basic-placement.json` and its siblings are all
  TrackData JSON, an entirely different, unaffected schema layer (the
  `<Models>` list is outer-XML-only, per the plan's own architecture
  notes). `grep -rl "TrackMeshes\|<ModelFile>" --include=*.xml .` across
  the whole repo found exactly one hit outside `ext/`/`build/`:
  `cpp/tungsten-monoxide/resources/Resources.xml` — real bundled game
  content, not a fixture, already migrated to `<Models>` as part of
  Milestone 7's own commit (`5e43256`). No new fixture mirroring
  `example_track_def.xml` was added as a committed file — the shape is
  instead exercised directly in `editor_track_resource_tests`/
  `model_xml_tests`/`model_tool_tests`' own in-memory XML strings, which
  cover the same ground without adding a new permanent fixture this plan
  never otherwise needed.

**8.2 — Docs** — done (`732e2b9`)
- `docs/adr/0001-model-tool.md`: superseded-by note added in Milestone 0.1
  already (`27ca622`), ahead of this milestone.
- `docs/adr/0002-track-resource-save-load.md`: D2/D4 both got a "Schema
  note"/"Extended by" callout pointing at this plan, without rewriting
  their still-accurate underlying principles (JSON-is-authoritative,
  per-Resource-element replace-and-preserve) — historical-record style,
  matching `DRIVABLE_MESH_OBJECTS_PLAN.md`'s own precedent elsewhere.
- `cpp/core/CLAUDE.md` (new `Track::fromTrackDataFiles` bullet),
  `cpp/editor/CLAUDE.md` (new bullet on the `<Models>` list/`loadModel`/
  placement rendering), `docs/UBIQUITOUS_LANGUAGE.md` (added **Model**,
  **Model placement** to the Track-structure glossary table plus a
  Relationships line), `docs/core.md` and `docs/tungsten-monoxide.md`
  (both had *load-bearing*, now-stale `<TrackMeshes>`/collision-selection
  descriptions rewritten — found while doing this pass, not originally
  scoped by name in this milestone but squarely "docs" work),
  `docs/model-tool.md` (its whole "Collidable/decorative flag" section
  rewritten for the Type/Visible XML metadata that replaced it — likewise
  found live-and-stale during this pass), `docs/DRIVABLE_MESH_OBJECTS_PLAN.md`
  (light note on `DrivableMeshObjectPlacementDefinition`'s later rename,
  historical text otherwise left untouched). Root `CLAUDE.md` needed no
  change — its subproject list already just points at
  `cpp/CMakeLists.txt`'s own header comment, which Milestone 2 already
  updated with a `cpp/model-xml` entry.

**8.3 — Full regression pass** — done
- `cmake --build cpp/build --config Release` (the whole workspace, no
  target filter) — zero errors, only pre-existing benign warnings
  (`std::float_denorm_style` deprecation, unused-parameter). `ctest
  --test-dir cpp/build -C Release --output-on-failure` — 6/6 suites green
  (`parity`, `track_tests`, `model_xml_tests`, `editor_track_resources`,
  `mpp_model_import_tests`, `model_tool_tests`). A `track_editor.exe`
  smoke launch reported 22 `OK` self-checks and zero `MISMATCH`/`FAIL`.
  A real interactive session (Open/Save/Load Model/a live game launch
  loading the updated `Resources.xml`) was not possible in this
  environment (no display/GPU) at any point across this whole plan —
  every milestone's own entry above already flags exactly where that
  matters most.

---

## Post-plan follow-up — model-tool: export `<Model>` XML instead of a materials fragment — done (`08252b4`)

A second grilling session, after all 8 milestones above landed, revised
`model-tool`'s Open/Save flow further:

- **Open** (`Ctrl+O`) is now `<Model>` XML only (standalone or embedded in a
  Track resource); the AssImp-formats-plus-raw-`.mppmodel` case Open used to
  also handle moved to a new **Import Model...** item, always starting a
  document with no XML origin.
- **Save As** now prompts for the `<Model>` XML destination (not an
  `.mppmodel` destination as before), deriving the `.mppmodel` filename from
  it and adopting the new location as the origin.
- **Save** gained `mppModelDirty` tracking (set by Bake Scale and its
  undo/redo, and by a fresh Import) so a metadata-only edit updates just the
  XML, leaving the `.mppmodel` untouched.
- The old companion materials-declaration XML export
  (`ModelResourceExport.hpp`/`.cpp`, `buildModelMaterialsXml`) is deleted
  outright — the `<Model>` XML fragment fully replaces it, per this
  session's own locked-in decision, rather than the two coexisting.

See the commit above for the full breakdown; `docs/model-tool.md`'s "Open /
Import / Save" section documents the resulting UX.
