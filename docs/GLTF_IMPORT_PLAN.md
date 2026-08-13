# glTF import — implementation plan

Branch: `gltf-import`. The matching MassivePolyPusher submodule change (the `RSE4`
stream format, see M1) is on `rse4-texture-colour-space` in that repository.

Overall status: **M1 complete; M2–M5 not started.**

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
`model_io_tests` (15 groups) are in and green, alongside the existing suite —
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

**Status: not started.**

Reimplement `AssImpImport`/`MppSave` over `model_io_*`. This reaches further
than those two files: `NormalSmoothing`, `ObjSmoothingGroups`, `MppModelImport`,
`ModelResources`, `MaterialLibrary` and the UI all consume `ImportedModel`/
`ImportedVertex`. Scene flattening changes as noted above — a deliberate
behaviour change that fixes today's lost per-mesh names.

### M3 — `track_editor` GLEW migration

**Status: not started.**

Drop `include/gl3w`/`src/gl3w`, point `imconfig.h` at `<GL/glew.h>`, call
`glewInit()` at startup, link `glew32`, and deploy the mpp/AssImp/Draco runtime
DLL set via a script mirroring `model-tool`'s `CopyModelToolRuntime.cmake`.

This milestone is high-risk and has nothing to do with glTF. It exists only
because the editor now links mpp. Land and verify it on its own.

### M4 — editor `.mppmodel` I/O, 52-byte track export, `Map.cpp`

**Status: not started.**

- Replace `MppModelImport.cpp` and `MppModelExport.cpp`'s byte writer with
  `model_io_core`. `buildTrackResourceXmlForName`/`buildModelsXml` are pure XML
  and survive unchanged.
- Rework `mpp_model_import_tests` and `editor_track_resource_tests`; the
  former's explicit "no Willpower/SDL/mpp dependency" premise is retired.
- Editor track export writes `gameMeshSpecification(false, true)` — 52 bytes,
  tangents baked at save time.
- `Map.cpp` accepts stride 36 (synthesise tangents, as today) *or* 52 (pass
  through) and rejects anything else, replacing the hard `stride != 36`
  rejections at `Map.cpp:120` and `Map.cpp:297`. Committed 36-byte models under
  `cpp/tungsten-monoxide/resources/` must keep loading — that is what the dual
  acceptance is for.
- `Map.cpp` prefers an embedded material when a mesh has one, falling back to
  the existing `PbrMaterialBinding` by-name lookup otherwise.

### M5 — editor import UI

**Status: not started.**

An "Import glTF…" modal presenting the full validation report — per-mesh
channel synthesis, per-material findings, the chosen pipeline material — before
converting. On accept it writes the `.mppmodel` and feeds it through the
existing `EditorState::loadModel` path, so dedup, placement and per-mesh
metadata work unchanged.

The pipeline XML path lives in `editor.ini`, defaulting to
`cpp/tungsten-monoxide/pbr/TungstenMonoxide.pipeline.xml` resolved through the
existing repo-root discovery convention. The target material is picked per
import from that pipeline's material list, with the last choice remembered.

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
