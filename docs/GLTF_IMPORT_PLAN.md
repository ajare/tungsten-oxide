# glTF import — implementation plan

Branch: `gltf-import`. The matching MassivePolyPusher submodule change (the `RSE4`
stream format, see M1) is on `rse4-texture-colour-space` in that repository.

Overall status: **M1–M5 complete.**

## Goal

Import a glTF/GLB asset into a `.mppmodel` carrying an **embedded**
`PbrMaterial`, validated up-front against a PBR pipeline specification so that
the result is guaranteed to render through that pipeline.

Two consumers:

- `track_editor`, as an "Import glTF…" authoring action.
- `gltf_convert`, a new headless console tool analogous to MassivePolyPusher's
  own `ModelConvert`.

Both sit on a shared pair of libraries under `cpp/model-io/`.

This supersedes the requirements-analysis pass recorded in the grilling session
on 2026-08-13; all open decisions from that pass are resolved below and are not
re-litigated here.

## Scope

**In scope.** glTF → `.mppmodel` + embedded `PbrMaterial`; the two libraries;
the console tool; the editor's import UI; the consequential changes to
`cpp/editor`, `cpp/model-tool` and `cpp/tungsten-monoxide` that fall out of
those decisions.

**Explicitly out of scope.** The reverse operation. There is no
`mpp::Model` → glTF export, no `track_editor` export option, and no Blender
round-trip. The console tool is import-only. This was considered and dropped
during the grilling pass; do not reintroduce it as a "while we're here".

## Decisions locked in

- **`track_editor` links mpp.** Its long-standing no-mpp posture (recorded in
  `cpp/editor/include/MppModelExport.hpp` and `docs/adr/0003-model-xml-layer.md`)
  is reversed. See ADR 0004.
- **`track_editor` migrates from vendored gl3w to GLEW**, exactly as
  `cpp/model-tool` already does, rather than firewalling mpp behind GL-free
  headers. One GL loader per process.
- **Two library targets, split on dependencies.** `model_io_core` is free of
  AssImp and of GL; `model_io_gltf` adds AssImp. `cpp/tungsten-monoxide` links
  only the core, so AssImp never enters the shipped game DLL's dependency
  closure.
- **The pipeline supplies the contract; the caller names one material.**
  Helpers take a loaded `mpp::PbrPipelineDocument` plus one material name. That
  material's `MeshSpecification` is the target vertex layout and its program is
  the target program. A pipeline's materials do *not* share one layout — in
  `TungstenMonoxide.pipeline.xml`, `Ship.Surface` is indexed and the seven
  `Track.*` are not — so a single named material is the only unambiguous input.
- **Material validation is feature-derived, not slot-whitelisted.** mpp derives
  `PbrMaterialFeatures` per material and injects shader specialisation defines
  (`mpp/PbrMaterialFeatures.h`), so map slots are a per-material property, not a
  pipeline capability. Rejecting a glTF normal map because the named pipeline
  material happens not to declare one would be wrong.
- **One embedded material per glTF material.** Values (base colour, metallic,
  roughness, emissive, alpha mode, double-sidedness, maps) come from the glTF;
  program and `MeshSpecification` come from the pipeline material.
- **Textures are child `TextureStream`s with model-relative paths.**
  `MppModelStream` calls `setFileBasePaths(baseDirectory(mFilename))` on the
  *children* of an embedded material only, and `PbrMaterialStream` does not
  override `setFileBasePaths` at all — so an inline `TextureOptions::source`
  would resolve against the process CWD. A child `TextureStream` is the only
  form the engine already rebases portably.
- **`.glb`-embedded and `data:` URI images are a validation error.** External
  images are referenced in place. An image outside the output model's directory
  tree is an error, because no relative path can express it.
- **The resource-stream format is extended to `RSE4`** so a texture's colour
  space survives serialization. This modifies the MassivePolyPusher submodule —
  a deliberate exception to treating it as untouched upstream, because
  `RSE3` had no field for colour space and an embedded sRGB base-colour map
  therefore always reloaded as linear. Reads stay backwards compatible.
- **Manual node walk.** One output mesh per node×mesh instance, world transform
  baked into vertices, named from the node. Neither existing precedent is
  adequate: `model_tool` uses bare `aiProcess_PreTransformVertices` (bakes
  transforms but merges meshes by material, destroying the names `model_xml`'s
  per-mesh `Type`/`Visible` metadata is keyed on), and upstream `model-convert`
  omits it entirely (keeps names but silently discards node transforms).
- **Missing channels are synthesised where derivable, and reported.**
  `colour4` → opaque white, `texcoord2` → (0,0), `normal3` → generated smooth
  normals, `tangent4` → computed from UV and normal. Each substitution is a
  named warning against the mesh it applies to. Validation fails only for a
  channel that genuinely cannot be produced. `--strict` promotes every warning
  to an error.
- **The editor's track export moves to the 52-byte PBR layout with baked
  tangents**, and `Map.cpp` accepts both 36- and 52-byte strides.
- **`cpp/model-tool` is refactored onto the new libraries**, accepting that its
  scene flattening behaviour changes (meshes are no longer merged by material).
  Its `ImportedModel` view model is kept rather than replaced by `ModelData` —
  see M2 for why.

## Architecture

| Target | Path | Links | Consumed by |
| --- | --- | --- | --- |
| `model_io_core` | `cpp/model-io/core` | `MppMesh`, `MppResourceParsers`, `MassivePolyPusher`, `Utils` — no AssImp, no GL loader | `track_editor`, `model_tool`, `tungsten-monoxide`, `gltf_convert` |
| `model_io_gltf` | `cpp/model-io/gltf` | `model_io_core` + AssImp | `track_editor`, `model_tool`, `gltf_convert` |
| `gltf_convert` | `cpp/gltf-convert` | both | — |

`model_io_core` owns:

- the renderer-neutral in-memory model type shared by import, export-to-mppmodel
  and every consumer;
- the vertex-layout contract — the 52-byte PBR and 36-byte legacy layouts, and
  tangent generation. `mono::gameMeshSpecification` and `mono::addPbrTangents`
  move here from `cpp/tungsten-monoxide`;
- `MeshSpecification`-driven packing/unpacking and `.mppmodel` read/write via
  `mpp::ModelSerializer`;
- embedded `PbrMaterial` construction and the diagnostics report type.

`model_io_gltf` owns AssImp loading, the node walk, and the two validations.

## Milestones

Each milestone is independently revertible.

### M1 — libraries, console tool, tests

**Status: complete.** `model_io_core`, `model_io_gltf`, `gltf_convert` and
`model_io_tests` are in and green, alongside the existing suite —
`ctest --test-dir cpp/build -C Release` passes 12/12. Verified end-to-end against
the real `TungstenMonoxide.pipeline.xml`: material listing, conversion against
`Ship.Surface`, and `--strict` correctly refusing a model that needs synthesis.

Two defects were found by the fixtures and fixed: AssImp's glTF2 importer appends
a default material to every scene, which was being embedded even when no mesh
referenced it; and an image inlined as a `data:` URI is resolved by AssImp into a
container-embedded texture, so both rejection paths converge on
`texture.embedded-in-container`.

The `RSE4` stream-format change was added to this milestone after the
colour-space gap was found — see "Decisions locked in" and ADR D5a.

Not verified here: DemoSuite's GPU material tests (see Risks).

- `cpp/model-io/` with both targets and their public headers.
- `cpp/gltf-convert/` — `gltf_convert`, import-only:
  `--in model.gltf --pipeline X.pipeline.xml --material Ship.Surface
  --out model.mppmodel`, plus `--strict` and `--validate-only`.
- `model_io_tests`, a ctest target covering the validation matrix and a
  round-trip of the written `.mppmodel` back through `mpp::ModelSerializer`.
- Fixtures under `cpp/test-data/fixtures/gltf/`: a `.gltf` with an external
  texture; one lacking `COLOR_0` and `TANGENT`; one with no UVs at all; a
  multi-node hierarchy with non-identity transforms; one with an inlined image,
  asserted to be rejected; one using `KHR_materials_transmission`, likewise.
  Plus a small dedicated `.pipeline.xml` so the tests do not couple to
  `TungstenMonoxide.pipeline.xml`, and `legacy-rse3.mppmodel`, a frozen
  pre-`RSE4` model guarding the stream-format compatibility path.
- The `RSE4` stream-format change in `ext/massive-poly-pusher`
  (`mpp/src/ResourceStreamSerializer.cpp`), covered by a colour-space round-trip
  test and the legacy-read test above.

Apart from the submodule's serializer, nothing outside `cpp/model-io/`,
`cpp/gltf-convert/` and `cpp/test-data/` is touched by M1, and no existing
target changes behaviour.

### M2 — `model_tool` refactor

**Status: complete.** `AssImpImport.cpp` and `MppSave.cpp` are adapters over
`model_io` (roughly 200 and 90 lines of duplicated AssImp walking and
`ModelSerializer` driving replaced), and `ModelResources.cpp`'s
`fixedMeshSpecification()` — byte-for-byte `gameMeshSpecification(true, false)` —
now defers to it. `model_tool` no longer references AssImp anywhere: its direct
include and link entries are gone, arriving transitively through
`model_io_gltf`. `ctest` passes 12/12.

**Scoping decision.** `ImportedModel`/`ImportedVertex` are *kept*, not replaced
by `modelio::ModelData`. They are model-tool's view model — carrying
`MaterialOrigin`, the single diffuse texture path, and per-mesh `model_xml`
`Type`/`Visible` metadata — consumed by `MaterialLibrary` bookkeeping, the
viewport and 1213 lines of UI. Migrating the type through those 17 files would
push model-tool's resource-management concerns into a shared conversion library
for no gain; replacing the *implementations* removes the actual duplication.
So `NormalSmoothing`, `ObjSmoothingGroups`, `MppModelImport`, `ModelResources`,
`MaterialLibrary` and the UI are untouched apart from the one-line spec change.

**Behaviour change, as designed.** Scene flattening moves from
`aiProcess_PreTransformVertices` to the manual node walk, so a multi-object
source keeps one mesh per node, named after it, instead of collapsing into one
mesh per material. That fixes the lost per-mesh names at the cost of more,
smaller meshes.

Two `model_io` capabilities were added to serve this consumer:
`EmbeddedTexturePolicy::Skip` (model-tool drops an unreferenceable image and
flags the material, rather than refusing the import, per ADR 0001 D4) and
`pruneUnreferencedMaterials`, both covered by new `model_io_tests` groups.
The importer was renamed `importGltf` →
`importAsset` (`GltfImport.hpp` → `AssetImport.hpp`), since it now serves
model-tool's OBJ/FBX/USD as well; and `writeMppModelWithNamedMaterials` was
split out for callers with no PBR pipeline to embed against. The importer also
gained AssImp's `GetEmbeddedTexture` check, which model-tool had and `model_io`
lacked.

`model_tool_tests` gains `importModel()` coverage over the shared glTF fixtures,
pinning the per-node flattening, the Skip policy and external-texture
resolution. That target is consequently no longer AssImp-free — accepted,
because the changed behaviour is otherwise reachable only through the GUI.

### M3 — `track_editor` GLEW migration

**Status: complete.** Split into three independently verifiable chunks, because
this milestone is the highest-risk one and has nothing to do with glTF — it
exists only because the editor now links mpp.

The GL surface turned out to be far smaller than feared: only two non-vendored
translation units touch GL at all (`main.cpp` and `TextureCache.cpp`), between
them using eight distinct GL entry points.

**M3.1 — GL loader swap.** `imconfig.h` and `TextureCache.cpp` point at
`<GL/glew.h>`; `main.cpp` calls `glewInit()`; `include/gl3w`/`src/gl3w` are
deleted; `glew32` is linked and its DLL deployed. The editor still links no mpp
at this point, so the loader change is isolated from everything else.

`glewExperimental = GL_TRUE` is set before `glewInit()`, and the error queue is
drained afterwards. Both are required, not incidental: the editor requests a
**core** 3.0 profile, and GLEW's normal path enumerates through
`glGetString(GL_EXTENSIONS)`, which a core context returns NULL for — leaving it
to fail or build a broken function table. Draining the queue clears the
`GL_INVALID_ENUM` that probing under a core profile leaves behind, so the first
real `glGetError()` of a frame is not this one.
`mpp::RenderSystem::initialise()` sets the same flag for the same reason, so the
two agree wherever both end up in one process.

**M3.2 — `track_editor` links `model_io_core`** (not `model_io_gltf`: the editor
reads and writes `.mppmodel` files and shares the vertex-layout contract, but
imports no assets, so it has no reason to pull in AssImp). The mpp runtime DLL
set is deployed via `model_io_deploy_runtime`, shared with `cpp/model-tool` and
`cpp/gltf-convert`.

**M3.3 — `mpp_model_import_tests` links `model_io_core`** for a bridge
assertion: the editor's from-scratch writer and reader each hard-code a 36-byte
stride and a channel order, entirely independently of `model_io`'s definition of
that layout. M4 replaces both, so the test pins that they already agree —
stride, per-channel offsets and the normalised colour flag. A silent
disagreement would otherwise surface as corrupt geometry at the moment of the
swap rather than as a failing test now. They do agree.

`editor_track_resource_tests` was deliberately left alone: it exercises XML only
and has no use for the layer.

Verified at runtime, not just at build time: `track_editor.exe` launches, passes
every one of its startup smoke checks, and reaches its main loop with empty
stderr — so `glewInit()` genuinely succeeds against a real context.

Consequences recorded rather than glossed: `mpp_model_import_tests` loses the
"no Willpower/SDL/mpp dependency" property its comment advertised, and the
runtime deployment copies the whole mpp `bin/` DLL set — including
`assimp-vc145-mt.dll`, which the editor does not use — because the shared deploy
script globs the directory rather than resolving per-target dependencies.

### M4 — editor `.mppmodel` I/O, 52-byte track export, `Map.cpp`

**Status: complete.** `MppModelImport.cpp`/`MppModelExport.cpp` are now thin
wrappers over `model_io_core`'s `mpp::ModelSerializer`-backed reader/writer;
`buildTrackResourceXmlForName`/`buildModelsXml` were untouched, as planned.
`ctest --test-dir cpp/build -C Release` passes 12/12, and `track_editor.exe`
was launched directly to confirm its startup smoke checks (including the
`.mppmodel` structural self-check) still pass against the new writer's real
output.

- The editor's track export builds a `modelio::ModelData` from
  `tox::GeometryBatch` (identity-indexed triangle soup, matching the batches'
  own unshared-vertex convention), calls `modelio::generateTangents` per mesh,
  and writes it with `modelio::writeMppModelWithNamedMaterials` against
  `gameMeshSpecification(false, true)` — 52 bytes, non-indexed, tangents baked
  at save time. Because `model_io`'s writer only writes to a filesystem path
  and `TrackResourceSave.cpp`'s Save/Save As flow builds every output file in
  memory before installing all three atomically, `exportTrackToMppModel`
  writes to a scratch temp file, reads the bytes back, and discards it —
  preserving that transactional contract without threading model_io's file
  I/O through it.
  `MppModelImport.cpp`'s reader is a thin wrapper over `modelio::readMppModel`
  that first peeks the file's own on-disk IndexData directory entry to decide
  indexed-vs-non-indexed, because neither `mpp::ModelSerializer` nor
  `modelio::readMppModel` can answer that on their own — a non-indexed
  `model_io`-written file still leaves every mesh's internal index-stream id
  at 0, not a sentinel (`modelio/MppModelIo.hpp`'s "known limitation"), and
  `ModelSerializer::getIndexData()`/`getIndexWidth()` are unchecked array
  indexing, unsafe to call speculatively. It decodes position3/normal3/
  texcoord2, which sit at the same offsets in both the 36-byte legacy and
  52-byte PBR layouts, so no separate code path is needed for either.
- `mpp_model_import_tests` gained a 52-byte hand-built indexed fixture
  alongside the existing 36-byte one (both now pass through the same reader
  path) and a PBR-layout companion to the existing legacy-layout
  model_io-contract assertion. `editor_track_resource_tests` now also links
  `model_io_core`, since `MppModelExport.cpp` — one of its own sources — is no
  longer mpp-free. `main.cpp`'s own `.mppmodel` structural self-check
  (`runMppModelSmokeCheck`) was updated to match the real `ModelSerializer`'s
  actual on-disk conventions rather than the old from-scratch writer's, which
  it had been silently diverging from until this milestone (unconditional
  `FLAG_INDEXED_VERTICES` in the header regardless of indexed-ness, and the
  index-stream-id-0-not-a-sentinel convention noted above).
- `Map.cpp` accepts stride 36 (synthesise tangents, as before) *or* 52 (pass
  the already-baked tangent through, rotating it by a placement's transform
  the same way position/normal are) for both the primary track mesh loop and
  drivable mesh object placements, replacing the hard `stride != 36`
  rejections. Committed 36-byte models under
  `cpp/tungsten-monoxide/resources/` keep loading unchanged.
- `Map.cpp` prefers an embedded material when a mesh's material name matches
  one of the model's own embedded materials (`ModelSerializer::getMaterialNames()`),
  declaring it into the `ResourceManager` once per model file (and rebasing
  its child `TextureStream`s to the model's own directory, since
  `ModelSerializer::readMaterial()` — unlike `MppModelStream` — never does
  that itself), falling back to the existing by-name `PbrMaterialBinding`
  lookup otherwise. `TrackCollisionBuild.h`'s `MeshObjectModel` gained a
  `path` field so this resolution has the model's directory to rebase against
  even on a placement cache hit.

### M5 — editor import UI

**Status: complete.** `track_editor` now links `model_io_gltf` (adding AssImp
to its dependency closure for the first time — `mpp_model_import_tests`/
`editor_track_resource_tests` stay on `model_io_core` alone, since neither
imports glTF). `ctest` passes 12/12 and `track_editor.exe` was launched
directly to confirm startup still succeeds with the new pipeline-loading code
present but untriggered (it's lazy — see below).

- **"Import glTF…"** sits in the same File-menu group as "Load Model…". It
  picks a source `.gltf`/`.glb`, lazily loads the configured PBR pipeline
  document (`ensureGltfPipelineLoaded`, sticking with whatever error it first
  hit rather than re-reading a bad `editor.ini` entry on every click), and
  opens the **"Import glTF"** modal.
- The modal shows the source path, a combo box of the pipeline's material
  names (`modelio::pipelineMaterialNames`), and the full diagnostics report
  from a `modelio::convertGltf(..., validateOnly=true)` pass — re-run every
  time the material selection changes — coloured by severity, matching what
  `gltf_convert`'s own stderr output shows for the same call. The **Import**
  button is disabled until validation against the current material selection
  has passed.
- On accept, `convertGltf` runs again with `validateOnly=false` against an
  output path derived from the source (`<source-stem>.mppmodel`, beside the
  source file), and the result is embedded through the *exact* helper
  ("Load Model..."'s `resolveModelFileReference`, hoisted out to be shared
  between the two entry points) and `EditorState::embedModel` call the raw-
  `.mppmodel` branch of "Load Model…" already uses — so dedup, placement and
  per-mesh metadata all work unchanged, and a glTF import is indistinguishable
  from a hand-picked `.mppmodel` from that point on.
- The pipeline XML path lives in `editor.ini`'s new `[GltfImport]
  PipelinePath` key (resolved relative to the executable's directory, like
  `[Resources] Path`), defaulting to
  `cpp/tungsten-monoxide/pbr/TungstenMonoxide.pipeline.xml` via a repo-root
  walk (`defaultGltfPipelinePath`, the same technique
  `findEditorResourceFile` already used) when the key is absent. The target
  material is remembered across imports for the running session
  (`lastGltfImportMaterial`, a local -- not persisted back to `editor.ini`,
  since `EditorIni` is read-only).
- Verified end-to-end against the real `TungstenMonoxide.pipeline.xml` and a
  fixture from `cpp/test-data/fixtures/gltf/` via `gltf_convert` (the
  headless twin calling the identical `modelio::convertGltf`/
  `PbrPipelineDocumentLoader` API the modal now calls) — `--validate-only`
  and a real write both succeed, confirming the plumbing the modal drives.

## Conventions

- `aiProcess_Triangulate`. Non-triangle primitives are dropped with a warning; a
  mesh left with no triangles is an error.
- 16-bit indices when the vertex count fits, otherwise 32-bit. No mesh
  splitting — a `PbrPipeline` `MeshSpecification` carries no `splitSize`.
- Embedded material names are qualified `<model-stem>/<glTF material name>`,
  deduplicated within one import, matching `model_tool`'s existing convention.
- No axis or unit conversion. Blender's glTF exporter already emits Y-up
  right-handed, matching mpp and `tox`.
- One diagnostics report type, shared by the console tool's stderr output and
  the editor's dialog.

## Risks

- **M3 is unrelated risk.** The GLEW migration touches the editor's GL bootstrap
  for reasons that have nothing to do with this feature.
- **Embedded materials revisit known-buggy machinery.**
  `cpp/model-tool/include/MppSave.hpp` documents four distinct
  `ResourceStreamSerializer` bugs that caused material embedding to be abandoned
  once before. The `PbrMaterial` write/read paths are a different code path, but
  this must be proven by round-trip tests, not assumed — and the missing
  colour-space field found during M1 shows the warning was well founded.
- **`RSE4` output cannot be read by an older MassivePolyPusher.** Every stream
  written from now on — including by PipelineEditor's package export,
  ModelConvert and DemoSuite — is `RSE4`. Anything consuming those artifacts must
  be built from this submodule revision or later. Existing committed `RSE3`
  assets keep loading and need no re-export.
- **DemoSuite's GPU material tests were not run.** They exercise binary
  round-trips and need a real GL context, which this environment has not got.
  They should be run once on a machine that can, before M2.
- **M4 changes the game's geometry contract.** Dual-stride acceptance in
  `Map.cpp` is the mitigation and must be tested against a committed 36-byte
  model.
