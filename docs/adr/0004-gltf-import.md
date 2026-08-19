# 0004 — glTF import: `track_editor` links mpp; a shared `model-io` layer owns the vertex contract

Status: Accepted (design agreed; implementation staged per `docs/GLTF_IMPORT_PLAN.md`)
Date: 2026-08-13

## Context

The repository needs to import glTF/GLB assets into `.mppmodel` files that are
guaranteed to render through a given PBR pipeline. The conversion must be usable
from two places: `track_editor` (as an authoring action) and a new headless
console tool analogous to MassivePolyPusher's `ModelConvert`.

Three existing arrangements stand in the way.

**`src/editor` deliberately does not link mpp.** Its `.mppmodel` reader and
writer (`MppModelImport.cpp`, `MppModelExport.cpp`) are from-scratch byte-level
implementations, written that way specifically to avoid a GLEW-vs-gl3w GL-loader
conflict — `src/model-tool` links real mpp and therefore uses GLEW, while the
editor vendors gl3w. `MppModelExport.hpp` and `0003-model-xml-layer.md` both
record this as intentional.

**Materials are referenced by name, never embedded.** Both writers in this
codebase leave the `.mppmodel` `MaterialNames`/`Materials` sections empty.
`src/model-tool/include/MppSave.hpp` documents that embedding was implemented
once and reverted after four distinct `ResourceStreamSerializer` defects.

**Two runtimes disagree about materials.** `mpp::MppModelStream` consumes
embedded material streams as child resources
(`mpp/src/MppModelStream.cpp:37-79`), whereas `src/tungsten-monoxide/src/Map.cpp`
drives `mpp::ModelSerializer` directly and resolves each mesh's material *by
name* through `getDependentResource` → `PbrMaterialBinding` → package binding.
An embedded material in a track model is ignored by the game today.

Additionally, only `mpp/ModelSerializer.h` pulls `<GL/glew.h>`; `MeshSpecification.h`,
`MeshSpecificationParser.h`, `PbrMaterialStream.h`, `ResourceStreamSerializer.h`
and `PbrPipelineDocument.h` are all GL-free, and upstream `ModelConvert` proves a
headless, context-free mpp + AssImp converter is viable.

## Decision

### D1 — `track_editor` links mpp, and migrates to GLEW

The editor's no-mpp posture is reversed. It links the mpp stack and AssImp, and
replaces its vendored gl3w loader with GLEW so that one GL loader serves the
process, exactly as `src/model-tool` already does.

A narrower option existed — keep gl3w and firewall mpp behind GL-free headers,
since `glewInit()` is called in exactly one place (`mpp/src/RenderSystem.cpp:331`)
and an editor that never constructs a `RenderSystem` would leave GLEW
uninitialised. It was rejected in favour of consistency with `model-tool` and of
leaving the door open for the editor to render through mpp later.

**Consequence.** `MppModelImport.cpp` and `MppModelExport.cpp`'s byte writer are
deleted in favour of `mpp::ModelSerializer`. This is not merely tidying: the
editor's reader only supports the fixed 36-byte layout and throws otherwise, so
it could not display the 52-byte PBR models this feature produces.
`buildTrackResourceXmlForName`/`buildModelsXml` are pure XML and survive.

### D2 — a two-target `src/model-io/` layer, split on dependencies

`model_io_core` is free of AssImp and of any GL loader; `model_io_gltf` adds
AssImp. `src/tungsten-monoxide` links only the core, so AssImp, Draco and zlib
never enter the shipped game DLL's dependency closure.

`model_io_core` becomes the single definition of the vertex-layout contract:
`mono::gameMeshSpecification` and `mono::addPbrTangents` move there from
`src/tungsten-monoxide`, where they are today unreachable by the editor, which
now needs them to bake 52-byte tracks.

### D3 — the caller names one pipeline material; validation is feature-derived

Helpers take a loaded `mpp::PbrPipelineDocument` and one material name. That
material supplies the target `MeshSpecification` and program. A pipeline's
materials do not share a layout — in `TungstenMonoxide.pipeline.yaml`
`Ship.Surface` is indexed and the seven `Track.*` are not — so naming one is the
only unambiguous input.

A glTF material is rejected only when it needs a feature outside mpp's
`PbrMaterialFeature` enum (transmission, clearcoat, sheen, iridescence, unlit, …)
or one the target `MeshSpecification` cannot feed (a normal map without
`tangent4`; a second UV set without a second `texcoord2`).

It is *not* rejected merely for using a map slot the named pipeline material
does not declare. `derivePbrMaterialFeatures` shows mpp derives features per
material and injects shader specialisation defines
(`makePbrSpecializationDefines`), so map slots are a per-material property, not a
pipeline capability. A literal slot whitelist would reject assets the renderer
can display.

### D4 — one embedded `PbrMaterial` per glTF material; `Map.cpp` prefers embedded

Surface values and maps come from the glTF; program and `MeshSpecification` come
from the pipeline material. `Map.cpp` is extended to use a mesh's embedded
material when present, falling back to the existing by-name `PbrMaterialBinding`
lookup otherwise — without which imported props would be self-contained for
`MppModelStream` and invisible to the game.

This knowingly re-enters the machinery `MppSave.hpp` warns about. The
`PbrMaterial` write/read paths at stream version 3 are a different code path from
the ones that failed, but the mitigation is round-trip test coverage, not
optimism.

### D5 — textures are child `TextureStream`s with model-relative paths

`MppModelStream` calls `setFileBasePaths(baseDirectory(mFilename))` on the
*children* of an embedded material, and `TextureStream` is the only class that
implements `setFileBasePaths`. `PbrMaterialStream` does not override it, so an
inline `TextureOptions::source` would resolve against the process CWD. A child
`TextureStream` with a model-relative path is therefore the only form the engine
already resolves portably; `Map.cpp` must replicate the rebasing.

`.glb`-embedded and `data:` URI images are a validation error, and so is an
external image outside the output model's directory tree, since no relative path
can express it.

### D5a — the resource-stream format is extended to `RSE4` to carry texture colour space

`ResourceStreamSerializer` wrote a texture's filters, wrap, mipmap and LOD
settings but **not** `TextureParams::colourSpace`, and `TextureParams` defaults
it to `Linear`. Colour space could therefore only ever be recovered from XML, so
an embedded sRGB base-colour or emissive map reloaded as linear and shaded
incorrectly — with no fix available on this side of the boundary.

The stream format is bumped `RSE3` → `RSE4`, adding the field to a `Texture`
stream's own definition and to each `BasicMaterial`/`PbrMaterial` texture-options
record. Reads are version-gated: `RSE3`, `RSE2` and `RSER` still load, and a
pre-`RSE4` texture keeps the `Linear` default. Only `RSE4` is written.

This modifies the MassivePolyPusher submodule, which this work otherwise treats
as upstream and untouched; it is a deliberate, explicitly requested exception.
The alternative was shipping an importer that silently produces wrongly-shaded
models. `src/test-data/fixtures/gltf/legacy-rse3.mppmodel` freezes a pre-change
model so the compatibility path stays covered independently of the submodule's
own assets, which may be re-exported at any time.

### D6 — manual node walk

One output mesh per node×mesh instance, world transform baked into vertices
(inverse-transpose for normals and tangents), named from the node with collision
disambiguation.

Both existing precedents are inadequate. `model_tool` uses bare
`aiProcess_PreTransformVertices`, which bakes transforms but merges meshes by
material and so destroys the per-mesh names `model_xml`'s `Type`/`Visible`
metadata is keyed on. Upstream `model-convert` omits it entirely, keeping names
but silently discarding node transforms — which collapses every object in a
typical Blender scene onto the origin.

### D7 — missing channels are synthesised where derivable, and reported

`colour4` → opaque white, `texcoord2` → (0,0), `normal3` → generated smooth
normals, `tangent4` → computed from UV and normal. Each substitution is a named
warning against its mesh; validation fails only where a channel cannot be
produced at all. `--strict` promotes warnings to errors.

Strict rejection was considered and rejected: against the 52-byte
`TungstenMonoxide` contract it would refuse essentially every ordinary Blender
export, which typically carries neither `COLOR_0` nor `TANGENT`.

### D8 — the editor's track export moves to the 52-byte PBR layout

Tracks are written as `gameMeshSpecification(false, true)` with tangents baked at
save time, and `Map.cpp` accepts stride 36 (synthesising tangents, as today) or
52 (passing through), rejecting anything else. Committed 36-byte models under
`src/tungsten-monoxide/resources/` must keep loading.

## Scope

glTF **export** is explicitly out of scope. There is no `mpp::Model` → glTF
path, no editor export option, and the console tool is import-only. `mpp::Mesh`
holds its vertex data in GPU buffers — only index data stays CPU-side — so
exporting from a live `mpp::Model` would require a GL context and readback that
the editor has no reason to stand up.

## Consequences

- The editor gains a substantial runtime DLL set (MassivePolyPusher, MppMesh,
  MppResourceParsers, Utils, AssImp, GLEW, Draco, zlib) and a deployment script
  mirroring `model-tool`'s.
- `mpp_model_import_tests`' explicit "no Willpower/SDL/mpp dependency" premise is
  retired, and both editor test targets gain runtime DLLs.
- `model_tool`'s scene flattening changes: meshes are no longer merged by
  material. This fixes a latent name-loss bug but is a visible behaviour change.
- The vertex-layout contract has one home instead of being duplicated between
  `src/tungsten-monoxide` and the editor.
- Every resource stream written from now on is `RSE4`, including those produced
  by MassivePolyPusher's own tools (PipelineEditor package export, ModelConvert,
  DemoSuite). Older readers cannot load them. Committed `RSE3` assets are
  unaffected and need no re-export.
