# cpp/model-tool — 3D model import/preview/save utility (`model_tool`)

`cpp/model-tool` is a standalone native app (Windows/MSVC) that imports a 3D model file via AssImp, previews it in a live `mpp::RenderSystem` viewport, and saves it as `.mppmodel` (MassivePolyPusher's binary model format, documented in `MPPMODEL_EXPORT_SPEC.md`).

**Status note:** the design record, `docs/adr/0001-model-tool.md`, still carries `Status: Accepted (design only — not yet implemented)`. That is stale — the tool is fully built and shipping (wired into the combined `cpp/` build via `add_subdirectory(model-tool)` in `cpp/CMakeLists.txt`), with several of the ADR's design decisions since superseded by what actually shipped (noted below). Treat this document, not the ADR header, as current.

## Intended use

A standalone offline utility for getting a third-party 3D model into `.mppmodel` form. It shares nothing at runtime with `cpp/editor` or `cpp/tungsten-monoxide` beyond the `.mppmodel` file format itself — nothing outside `cpp/model-tool/` references it (including at build time: this is the only place `NormalSmoothing.cpp`/`CollidableFlag.cpp` are compiled). It is not wired into the editor's own UI or the game runtime; a `.mppmodel` this tool saves is consumed the same way any other `.mppmodel` is, by `cpp/tungsten-monoxide/src/Map.cpp` (see `DRIVABLE_MESH_OBJECTS_PLAN.md` Milestone 3's architecture note and Milestone 4 below).

## Import pipeline

Entry point: `modeltool::importModel()` (`include/AssImpImport.hpp`/`src/AssImpImport.cpp`).

- **Formats**: whatever AssImp + the file dialog filter enumerate — OBJ, FBX, USD (`.usd`/`.usda`/`.usdc`/`.usdz`), glTF (`.gltf`/`.glb`), plus round-tripping an existing `.mppmodel`.
- **Post-process flags**: `aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_PreTransformVertices`. The last one bakes every node's transform into its meshes' vertex data, collapsing the scene's node hierarchy into root/world space — there is no node/scene-graph concept anywhere downstream. No `aiProcess_GenSmoothNormals` — see "Normal recomputation" below.
- **Fixed vertex layout**: `struct ImportedVertex` — position (float×3), normal (float×3), uv (float×2), color (unorm8×4) = **36 bytes**, matching the stride `cpp/editor`'s own `MppModelExport.cpp` uses for track geometry (independent implementations, same shape by convention). Missing color defaults to opaque white, missing UV to (0,0).
- **Materials**: `aiTextureType_DIFFUSE` with `aiTextureType_BASE_COLOR` fallback — diffuse/base-color texture path only, no specular/roughness/metalness/emissive/normal maps. Material names are qualified to `"<file-stem>/<material-name>"` and deduplicated; embedded textures (common in `.glb`) are detected and skipped rather than decoded, falling back to default-white with a warning.
- **Round-trip import**: `include/MppModelImport.hpp`/`src/MppModelImport.cpp` reads an existing `.mppmodel` directly via `mpp::ModelSerializer` (deliberately not `mpp::MppModelStream`), producing the same `ImportedModel` — strict about it: only exactly-36-byte vertex streams and `Triangles` meshes are accepted.

### Normal recomputation (`DRIVABLE_MESH_OBJECTS_PLAN.md` Milestone 4.2)

`include/NormalSmoothing.hpp`/`src/NormalSmoothing.cpp` recomputes every vertex normal from triangle winding order right after import (`importModel()`'s last step), never trusting the source file's own normals (real or AssImp-synthesized) — this is why `aiProcess_GenSmoothNormals` is no longer in the post-process flags above, it would just be redundant work immediately overwritten. Unlike AssImp's own per-mesh smoothing, this is smooth *across* sub-mesh boundaries too: vertices from different `ImportedMesh` entries that share a position (matched by quantized position, not object identity — real sub-meshes never literally share a vertex buffer) accumulate the same face-normal contributions, so a model split into several sub-meshes for organizational reasons (not because of an intended hard edge) reads as one seamless surface once placed. There is no smoothing-angle threshold — every triangle touching a shared position contributes, matching AssImp's own default (no configured crease angle).

**Deviates from the plan's literal wording** ("recompute... at export"): this happens at *import* time instead, so the live viewport preview always matches what gets saved — there's no dedicated "export" step in this app's pipeline distinct from `importModel()` that a save-time recompute could hook into without doing it twice, and doing it once at import avoids ever showing normals in the preview that the saved file won't have.

Deliberately dependency-free (no AssImp, no mpp) — see "Intended use" above — both so `MppModelImport.cpp`'s round-trip path could reuse it too if the round-tripped normals ever needed the same treatment, and so it's cheaply unit-testable headlessly (`tests/model_tool_tests.cpp`, wired into `ctest` as `model_tool_tests` — no GPU, no window, no AssImp DLL needed to run it).

### Collidable/decorative flag (Milestone 4.1/4.3)

Each `ImportedMesh` (a sub-mesh) carries a `collidable` bool, defaulting to `true` — the common case for a *drivable* mesh object is that most of its sub-meshes should be driven on, so authoring only needs to mark the exceptions (decorative flourishes) `false`. Editable per sub-mesh in the Panels window's Meshes list (a checkbox beside each entry).

**Representation choice** (the plan's "Flag authoring surface" step names two options — a naming convention or explicit CLI arguments — and asks that the choice be documented here): a **naming convention**, not CLI arguments. `mpp::ModelSerializer`'s `MeshMetadata` section has no free-form per-mesh field to add a flag to without changing the binary format every other `.mppmodel` writer/reader in this codebase depends on, and this app has no batch/headless export mode for CLI arguments to attach to anyway (`main()` takes one optional model path to *open*, nothing else — see "CLI / UI surface" below). Instead, `include/CollidableFlag.hpp`/`src/CollidableFlag.cpp` appends a fixed `~decorative` marker to a mesh's exported name when it's *not* collidable (a collidable mesh's name is written completely unchanged); `MppSave.cpp` encodes it on save, `MppModelImport.cpp` decodes and strips it back off on reimport. A `.mppmodel` from before this feature existed, or from any other tool, reads back as "every mesh collidable" — the least-surprising default. Also unit-tested headlessly in `model_tool_tests`.

## Preview

`include/Viewport.hpp`/`src/Viewport.cpp` — a docked ImGui panel rendering via `ImGui::Image()`. The ADR's plan (create a dedicated `mpp::RenderTexture` sized to the panel) was **not** what shipped: `mpp::RenderPipeline`'s default render pass owns its own internal target that overrides any pushed one, so `Viewport` instead reads back the pipeline's own output target and recovers its raw GL texture id directly (`mpp::Texture` has no public id getter). Practical consequence: the preview always renders at main-window resolution and is stretched to fit the panel, not pixel-perfect.

Shading also diverged from the ADR's planned bundled GLSL program — a `ProgrammaticMaterialStream` that never calls `setProgram()` resolves to mpp's existing core program (`"__mpp_p3d_tris_p3n3t2c4__"`), which already matches the fixed vertex layout. Lighting is a flat ambient plus one light at the camera position, recomputed every frame.

Camera: `include/OrbitCamera.hpp`, an explicit spherical-coordinate orbit camera (target/distance/azimuth/elevation) with exponential zoom and frame-on-bounds. Extras beyond the ADR: an XZ reference grid (toggle `G`), a preview-scale tool with a bake-to-geometry action, and geometry undo/redo (capped at 20 steps, currently covering only the scale-bake action).

## Save

`include/MppSave.hpp`/`src/MppSave.cpp` — `saveModelAsMppModel()` uses the real `mpp::ModelSerializer` (unlike `cpp/editor`'s hand-written exporter — see `docs/editor.md`), preserving shared-vertex indexing rather than an unshared triangle soup.

**Materials diverged from the ADR.** The original plan (D5) was to embed real materials via `ModelSerializer::addMaterial()`; that was implemented, then reverted after surfacing several independent bugs in `mpp::ResourceStreamSerializer`'s round-trip (a directory-offset miscalculation, a corrupted string-length prefix, a missing re-attached texture-load function, a uniform count/size mismatch that crashed natively). Saved `.mppmodel` files now carry **name-only** material references, same as the editor's exporter — converging on the simpler approach after all. A companion `.xml` (`include/ModelResourceExport.hpp`) is written beside the `.mppmodel`, one `<Image>`+`<Material>` pair per referenced material, and must be imported alongside it or every material shows as unresolved default-white on reopen.

## CLI / UI surface

Single `main()` (`cpp/model-tool/main.cpp`). SDL2 window, fixed DockBuilder layout (Panels left, Viewport right, no saved layout — identical every launch), a main menu bar, and a timed status bar.

- **CLI**: one optional positional argument (a model path to open immediately) — no flags, no headless/batch conversion mode.
- **Menus**: File (Open… / Save As `.mppmodel`… / Import Materials XML… / Exit), Edit (Undo/Redo), View (grid toggle/size).
- **Left panel**: a flat Meshes list (name, triangle count, material — flagging unresolved materials — plus a Collidable checkbox, see "Collidable/decorative flag" above), a flat unified Materials list (texture path, source, an unload action disabled while in use), and a Scale section (axis, target size, preview-apply, bake).
- A "Material Name Conflicts" modal (Replace/Ignore per row) handles reimporting a model whose material names collide with already-loaded ones.

## Limitations

- **No node/scene graph** — `aiProcess_PreTransformVertices` collapses hierarchy permanently; there's no node tree anywhere in the UI.
- **No animations or skeletons** — static meshes only; `.mppmodel` has no bone concept.
- **Materials are diffuse/base-color-texture-path only** — no other PBR channels, no flat color factors.
- **Embedded textures are skipped**, never decoded.
- **Saved files carry no material data** — name references only; the companion XML must travel with the `.mppmodel`.
- **`.mppmodel` import is strict** — a non-36-byte vertex stream or non-triangle mesh fails the whole import; a multi-stream mesh silently uses only the first stream.
- **Preview resolution is the main window's**, not the panel's.
- **Undo/redo covers only geometry edits** (currently just scale-baking), not model loading or material-library state.
- **Windows/MSVC only.**
- **UV V-axis convention across exporters was never settled** per the ADR, and no V-flip handling is present in the importer — likely still an open issue for content from tools with the opposite convention.
- **Single model, single pending import at a time.**
