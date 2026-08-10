# cpp/model-tool — 3D model import/preview/save utility (`model_tool`)

`cpp/model-tool` is a standalone native app (Windows/MSVC) that imports a 3D model file via AssImp, previews it in a live `mpp::RenderSystem` viewport, and saves it as `.mppmodel` (MassivePolyPusher's binary model format, documented in `MPPMODEL_EXPORT_SPEC.md`).

**Status note:** the design record, `docs/adr/0001-model-tool.md`, still carries `Status: Accepted (design only — not yet implemented)`. That is stale — the tool is fully built and shipping (wired into the combined `cpp/` build via `add_subdirectory(model-tool)` in `cpp/CMakeLists.txt`), with several of the ADR's design decisions since superseded by what actually shipped (noted below). Treat this document, not the ADR header, as current.

## Intended use

A standalone offline utility for getting a third-party 3D model into `.mppmodel` form, and (since `TRACK_MODEL_LIST_PLAN.md`, `docs/adr/0003-model-xml-layer.md`) for authoring the `<Model>` XML fragment that carries each mesh's Type/Visible metadata alongside it. It shares `cpp/model-xml` (a small TinyXML2-based library, see that plan's Milestone 2) with `cpp/editor` — the one place the two apps share code — but nothing else at runtime beyond the `.mppmodel` file format itself; NormalSmoothing.cpp/ObjSmoothingGroups.cpp are still compiled only here. It is not wired into the editor's own UI or the game runtime beyond that shared fragment-schema library; a `.mppmodel` this tool saves is consumed the same way any other `.mppmodel` is, by `cpp/tungsten-monoxide/src/Map.cpp` (see `DRIVABLE_MESH_OBJECTS_PLAN.md` Milestone 3's architecture note and Milestone 4 below) or by `cpp/editor`'s own placement-rendering path (`TRACK_MODEL_LIST_PLAN.md` Milestone 4).

## Import pipeline

Entry point: `modeltool::importModel()` (`include/AssImpImport.hpp`/`src/AssImpImport.cpp`).

- **Formats**: whatever AssImp + the file dialog filter enumerate — OBJ, FBX, USD (`.usd`/`.usda`/`.usdc`/`.usdz`), glTF (`.gltf`/`.glb`), plus round-tripping an existing `.mppmodel`.
- **Post-process flags**: `aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_PreTransformVertices`. The last one bakes every node's transform into its meshes' vertex data, collapsing the scene's node hierarchy into root/world space — there is no node/scene-graph concept anywhere downstream. No `aiProcess_GenSmoothNormals` — see "Normal recomputation" below.
- **Fixed vertex layout**: `struct ImportedVertex` — position (float×3), normal (float×3), uv (float×2), color (unorm8×4) = **36 bytes**, matching the stride `cpp/editor`'s own `MppModelExport.cpp` uses for track geometry (independent implementations, same shape by convention). Missing color defaults to opaque white, missing UV to (0,0).
- **Materials**: `aiTextureType_DIFFUSE` with `aiTextureType_BASE_COLOR` fallback — diffuse/base-color texture path only, no specular/roughness/metalness/emissive/normal maps. Material names are qualified to `"<file-stem>/<material-name>"` and deduplicated; embedded textures (common in `.glb`) are detected and skipped rather than decoded, falling back to default-white with a warning.
- **Round-trip import**: `include/MppModelImport.hpp`/`src/MppModelImport.cpp` reads an existing `.mppmodel` directly via `mpp::ModelSerializer` (deliberately not `mpp::MppModelStream`), producing the same `ImportedModel` — strict about it: only exactly-36-byte vertex streams and `Triangles` meshes are accepted.

### Normal recomputation (`DRIVABLE_MESH_OBJECTS_PLAN.md` Milestone 4.2)

`include/NormalSmoothing.hpp`/`src/NormalSmoothing.cpp` recomputes every vertex normal from triangle winding order right after import (`importModel()`'s last step), never trusting the source file's own normals (real or AssImp-synthesized) — this is why `aiProcess_GenSmoothNormals` is no longer in the post-process flags above, it would just be redundant work immediately overwritten. There is no smoothing-angle threshold — every triangle touching a shared position contributes, matching AssImp's own default (no configured crease angle).

**Smoothing groups, and why they needed a second, format-specific piece (`ObjSmoothingGroups.hpp`/`.cpp`).** `aiMesh` (verified against this repo's vendored AssImp headers, and empirically: a hand-crafted `.obj` with two triangles in different `s` groups sharing an edge, run through `aiProcess_GenSmoothNormals`, still smooths straight across the group boundary) exposes **no authored smoothing-group data for any format** — `GenSmoothNormals` is a fixed crease-angle heuristic (`AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE`), not group-aware. So `recomputeNormals()` takes an optional `std::vector<MeshTriangleGroups>*` (one entry per `ImportedModel::meshes`, one group id per triangle) and behaves differently depending on whether real group data was recoverable:

- **Smoothing-group-aware** (groups supplied): two triangle corners merge into one smoothed normal only when they share both a position *and* a group id — global across every mesh in the model, so an authored group spanning a `usemtl`/mesh split still smooths seamlessly. Two faces in different groups at a shared position get a genuine hard edge: since a single vertex can only ever carry one normal, this requires *splitting* that vertex into one copy per group it's used by (`mesh.vertices`/`mesh.indices` can grow as a result — never shrink).
- **Per-mesh** (groups is null — every format except `.obj`, and a `.mppmodel` reimport, since neither AssImp's public API nor the `.mppmodel` format itself carries this data): every triangle within one `ImportedMesh` shares one synthetic group (smooth throughout that one mesh, hard boundary between different meshes). This *replaces* Milestone 4.2's original implementation, which smoothed across every mesh in the model unconditionally — narrowed once it became clear that behavior was papering over sub-mesh boundaries the source file never actually asked to be seamless, for every format that lacks real group data anyway.

`include/ObjSmoothingGroups.hpp`/`src/ObjSmoothingGroups.cpp` is the only source of real (non-null) group data: it re-parses the raw `.obj` text file directly, tracking `s <n>`/`s off` state against `f` lines, then matches AssImp's post-`Triangulate`/`JoinIdenticalVertices`/`PreTransformVertices` output back to the original faces **by vertex position** (not declaration order, which isn't guaranteed stable across those steps) — a triangulated n-gon's sub-triangles only ever use vertices the original face already had, so the correct source face is whichever one's own position set contains all three of a given triangle's corners. `s off`/`s 0` gets a fresh, always-unique synthetic group per face, matching OBJ's own "no smoothing" semantics (it never merges with anything, including another `s off` face). Only `.obj` gets this treatment — FBX/glTF/USD have no equivalent public-API-free path without a much larger investment (a binary/format-specific parser), and `.mppmodel` has no smoothing-group concept at all.

**Deviates from the plan's literal wording** ("recompute... at export"): this happens at *import* time instead, so the live viewport preview always matches what gets saved — there's no dedicated "export" step in this app's pipeline distinct from `importModel()` that a save-time recompute could hook into without doing it twice, and doing it once at import avoids ever showing normals in the preview that the saved file won't have.

Both `NormalSmoothing.cpp` and `ObjSmoothingGroups.cpp` are deliberately dependency-free (no AssImp, no mpp) — see "Intended use" above — so both are cheaply unit-testable headlessly (`tests/model_tool_tests.cpp`, wired into `ctest` as `model_tool_tests` — no GPU, no window, no AssImp DLL needed to run it, including for the `.obj`-parsing coverage).

### Per-mesh Type/Visible metadata (`TRACK_MODEL_LIST_PLAN.md` Milestone 3, superseding the old collidable/decorative name-suffix flag)

Each `ImportedMesh` (a sub-mesh) carries `modelxml::MeshType type` (Track/Physical/Decorative, defaulting to `Physical`) and `bool visible` (defaulting to `true`) — the common case for a *drivable* mesh object is that most of its sub-meshes should be driven on, so authoring only needs to mark the exceptions. Editable per sub-mesh in the Panels window's Meshes list (a Type combo + Visible checkbox beside each entry); selecting `Type=Track` shows an inline hint that the Model needs a `<TrackData>` file.

**Representation choice, revised**: originally (`DRIVABLE_MESH_OBJECTS_PLAN.md` Milestone 4.3) this metadata rode along in the exported `.mppmodel` mesh name itself, as a fixed `~decorative` suffix marker (`CollidableFlag.hpp`, since deleted) — chosen because `mpp::ModelSerializer`'s `MeshMetadata` section has no free-form per-mesh field, and this app had no batch/headless export mode for CLI arguments to attach to. `TRACK_MODEL_LIST_PLAN.md` Milestone 3.2 retired that convention entirely: mesh names are now always written/read completely unchanged, and the metadata lives only in the associated `<Model>` XML fragment (`cpp/model-xml`) — either a standalone file or one embedded in a Track resource's `<Models>` list. `main.cpp`'s Open dialog auto-detects which of the three shapes (`.mppmodel` / standalone Model XML / Track resource XML) was picked (`OpenTarget.hpp`/`.cpp`, TRACK_MODEL_LIST_PLAN.md Milestone 3.3) and Save writes back to wherever it came from; a `.mppmodel` with no associated XML at all has no Type/Visible metadata to read, so it just gets `ImportedMesh`'s in-memory Physical/visible defaults, not "every mesh collidable" as the old convention implied. Unit-tested headlessly in `model_xml_tests` (the fragment schema itself) and `model_tool_tests` (`OpenTarget.cpp` classification/scan/rewrite).

## Preview

`include/Viewport.hpp`/`src/Viewport.cpp` — a docked ImGui panel rendering via `ImGui::Image()`. The ADR's plan (create a dedicated `mpp::RenderTexture` sized to the panel) was **not** what shipped: `mpp::RenderPipeline`'s default render pass owns its own internal target that overrides any pushed one, so `Viewport` instead reads back the pipeline's own output target and recovers its raw GL texture id directly (`mpp::Texture` has no public id getter). Practical consequence: the preview always renders at main-window resolution and is stretched to fit the panel, not pixel-perfect.

Shading also diverged from the ADR's planned bundled GLSL program — a `ProgrammaticMaterialStream` that never calls `setProgram()` resolves to mpp's existing core program (`"__mpp_p3d_tris_p3n3t2c4__"`), which already matches the fixed vertex layout. Lighting is a flat ambient plus one light at the camera position, recomputed every frame.

Camera: `include/OrbitCamera.hpp`, an explicit spherical-coordinate orbit camera (target/distance/azimuth/elevation) with exponential zoom and frame-on-bounds. Extras beyond the ADR: an XZ reference grid (toggle `G`), a preview-scale tool with a bake-to-geometry action, and geometry undo/redo (capped at 20 steps, currently covering only the scale-bake action).

## Save

`include/MppSave.hpp`/`src/MppSave.cpp` — `saveModelAsMppModel()` uses the real `mpp::ModelSerializer` (unlike `cpp/editor`'s hand-written exporter — see `docs/editor.md`), preserving shared-vertex indexing rather than an unshared triangle soup.

**Materials diverged from the ADR.** The original plan (D5) was to embed real materials via `ModelSerializer::addMaterial()`; that was implemented, then reverted after surfacing several independent bugs in `mpp::ResourceStreamSerializer`'s round-trip (a directory-offset miscalculation, a corrupted string-length prefix, a missing re-attached texture-load function, a uniform count/size mismatch that crashed natively). Saved `.mppmodel` files now carry **name-only** material references, same as the editor's exporter — converging on the simpler approach after all. A companion materials-declaration `.xml` used to be written beside the `.mppmodel` (`include/ModelResourceExport.hpp`, one `<Image>`+`<Material>` pair per referenced material) but that export was retired once Save/Save As started writing the `<Model>` XML fragment instead (see "Open/Import/Save" below) — reopening a saved `.mppmodel` on its own still shows every material as unresolved default-white until matching materials are loaded some other way (e.g. "Import Materials XML..." against a hand-authored `Resources.xml`).

## Open / Import / Save

The File menu splits opening/creating a document into two distinct actions, and correspondingly splits saving:

- **Open...** (`Ctrl+O`) — a `<Model>` XML fragment only: a standalone file (bare `<Model>` root) or one embedded in a Track resource XML's `<Models>` list (prompting a "Choose Model" picker if it embeds more than one, mirroring `cpp/editor`'s own). Reads the referenced `.mppmodel` and applies that fragment's per-mesh Type/Visible metadata onto the freshly loaded meshes.
- **Import Model...** — AssImp formats or a raw `.mppmodel`, always as a brand-new document with no `<Model>` XML origin, for setting mesh properties from scratch. This is what "Open..." used to also handle before the `<Model>` XML fragment existed.
- **Save** (`Ctrl+S`) — writes back to wherever the document came from. A document with no origin (fresh Import) falls back to Save As. Otherwise it always updates the `<Model>` XML's per-mesh metadata, but only rewrites the `.mppmodel` when a dirty flag says geometry has actually changed since it was last written (set by Bake Scale and its own undo/redo, and by a fresh Import that's never had a `.mppmodel` written yet) — a pure metadata edit leaves the `.mppmodel` untouched.
- **Save As...** — always prompts for a new standalone `<Model>` XML destination (not an `.mppmodel` destination directly); the `.mppmodel` filename is derived from it (same stem) and always written, since this is establishing a brand-new save location. Adopts the new location as the document's origin, so a subsequent `Ctrl+S` goes there.

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
