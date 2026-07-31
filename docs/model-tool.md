# cpp/model-tool — 3D model import/preview/save utility (`model_tool`)

`cpp/model-tool` is a standalone native app (Windows/MSVC) that imports a 3D model file via AssImp, previews it in a live `mpp::RenderSystem` viewport, and saves it as `.mppmodel` (MassivePolyPusher's binary model format, documented in `MPPMODEL_EXPORT_SPEC.md`).

**Status note:** the design record, `docs/adr/0001-model-tool.md`, still carries `Status: Accepted (design only — not yet implemented)`. That is stale — the tool is fully built and shipping (wired into the combined `cpp/` build via `add_subdirectory(model-tool)` in `cpp/CMakeLists.txt`), with several of the ADR's design decisions since superseded by what actually shipped (noted below). Treat this document, not the ADR header, as current.

## Intended use

A standalone offline utility for getting a third-party 3D model into `.mppmodel` form. It shares nothing at runtime with `cpp/editor` or `cpp/tungsten-monoxide` beyond the `.mppmodel` file format itself — nothing outside `cpp/model-tool/` references it. It is not wired into the editor's mesh-region authoring (mesh regions are unrelated 2D geometry) or the game runtime.

## Import pipeline

Entry point: `modeltool::importModel()` (`include/AssImpImport.hpp`/`src/AssImpImport.cpp`).

- **Formats**: whatever AssImp + the file dialog filter enumerate — OBJ, FBX, USD (`.usd`/`.usda`/`.usdc`/`.usdz`), glTF (`.gltf`/`.glb`), plus round-tripping an existing `.mppmodel`.
- **Post-process flags**: `aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenSmoothNormals | aiProcess_PreTransformVertices`. The last one bakes every node's transform into its meshes' vertex data, collapsing the scene's node hierarchy into root/world space — there is no node/scene-graph concept anywhere downstream.
- **Fixed vertex layout**: `struct ImportedVertex` — position (float×3), normal (float×3), uv (float×2), color (unorm8×4) = **36 bytes**, matching the stride `cpp/editor`'s own `MppModelExport.cpp` uses for track geometry (independent implementations, same shape by convention). Missing color defaults to opaque white, missing UV to (0,0).
- **Materials**: `aiTextureType_DIFFUSE` with `aiTextureType_BASE_COLOR` fallback — diffuse/base-color texture path only, no specular/roughness/metalness/emissive/normal maps. Material names are qualified to `"<file-stem>/<material-name>"` and deduplicated; embedded textures (common in `.glb`) are detected and skipped rather than decoded, falling back to default-white with a warning.
- **Round-trip import**: `include/MppModelImport.hpp`/`src/MppModelImport.cpp` reads an existing `.mppmodel` directly via `mpp::ModelSerializer` (deliberately not `mpp::MppModelStream`), producing the same `ImportedModel` — strict about it: only exactly-36-byte vertex streams and `Triangles` meshes are accepted.

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
- **Left panel**: a flat Meshes list (name, triangle count, material — flagging unresolved materials), a flat unified Materials list (texture path, source, an unload action disabled while in use), and a Scale section (axis, target size, preview-apply, bake).
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
