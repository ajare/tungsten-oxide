# TungstenMonoxide PBR pipeline and material migration plan

## Objective

Move TungstenMonoxide's playable 3D scene from MassivePolyPusher's legacy `Material`/`Program` path and implicit legacy render pipeline to the authored PBR package workflow used by PipelineEditor and DemoSuite.

The end state is:

- PipelineEditor is the source of truth for the game's PBR render graph, materials, textures, environment, preview scene, and lights.
- PipelineEditor exports one standard ZIP-based `.mpppackage` containing those assets and their transitive dependencies.
- TungstenMonoxide loads `manifest.xml`, `PbrPipelineDocument`, and `SceneDocument` from the package and creates `PbrPipelineRuntime` in the same order as DemoSuite's `PackageScene`.
- The game keeps its own dynamic `mpp::Scene`; the packaged scene is an authoring/validation preview, not the live race scene.
- Ship, track, and placed-model meshes use PBR materials through stable logical bindings and provide position, normal, UV0, colour, and `tangent4` vertex data.
- The PBR graph renders HDR and tone-maps to its package-owned presentation target. The game blits that target to the window before drawing its HUD and ImGui overlay.
- Legacy rendering remains available to loading/controller states during migration, then unused TungstenMonoxide legacy material resources are removed.

This is an application migration, not a rewrite of AppLib's loading screens or a change to track collision geometry.

## Reference implementation

Follow these MassivePolyPusher components rather than creating a private package format:

- Authoring and export: `ext/willpower/ext/massive-poly-pusher/pipeline-editor/src/Main.cpp`
- Runtime package loading: `ext/willpower/ext/massive-poly-pusher/demo-suite/src/PackageScene.cpp`
- Package extraction: `ext/willpower/ext/massive-poly-pusher/demo-suite/src/Main.cpp`
- Pipeline runtime: `mpp::resource_parsers::PbrPipelineRuntime`
- Pipeline parsing: `mpp::resource_parsers::PbrPipelineDocumentLoader`
- Scene parsing: `mpp::resource_parsers::SceneParser`
- Manifest and ZIP support: `mpp::app::PackageManifest` and `mpp::app::ZipArchive`
- Authoring guidance: `PIPELINE_EDITOR_AUTHORING_GUIDE.md`, `PBR_MATERIAL_AUTHORING.md`, and `PBR_MATERIAL_SETUP.md` in the MPP `doc/` directory

Do not replace the package's localized paths with source-tree paths at runtime. Export must continue to collect dependencies, localize references, write `manifest.xml`, validate the staged package, and ZIP the staged tree.

## Locked design decisions

### 1. Use a dedicated game pipeline

The authored pipeline is named `TungstenMonoxide.Pbr`. It does not replace MPP's generic legacy `Default` path and is not named after an AppLib state.

AppLib gets a virtual pipeline-selection hook. Its default implementation preserves today's `getOrCreateRenderPipeline(getName())`; `StatePlayTungstenMonoxide` overrides the hook and returns the package-created PBR pipeline. This avoids creating a legacy pipeline called `Play` and then attempting to recreate it with incompatible PBR options.

Controller, loading, unloading, and transition states remain legacy initially. Only the playable 3D scene moves to PBR.

### 2. Load the package before map loading

`Map::load()` can run on a worker and creates model streams containing concrete MPP material resource names. MapLoad uploads those models before the Play state is entered. Therefore package materials cannot first be declared in `StatePlayTungstenMonoxide::setup()`.

Add a `TungstenPbrPackage` service owned by `TungstenMonoxideModel`. Initialize it on the render thread from `StateControllerTungstenMonoxide::setup()`, before the controller transitions to `Load` and `MapLoad`. Keep it alive until controller teardown, after map and Play resources have been released.

The service owns:

- the extracted package directory and its cleanup guard;
- parsed `PbrPipelineDocument` and `SceneDocument`;
- `PbrPipelineRuntime`;
- the declared `RenderGraphStream` resource;
- the `TungstenMonoxide.Pbr` render pipeline;
- the presentation render target;
- the logical material-binding map;
- package diagnostics and current viewport size.

Package initialization is fail-fast. A missing package, invalid manifest, invalid document, unresolved required binding, or absent presentation target prevents startup and reports MPP diagnostic codes. Do not silently fall back to legacy rendering after maps have begun loading.

### 3. Use the package scene as a preview and lighting contract

The live race scene is dynamic and must not be replaced by `SceneRuntime`. The runtime loader still parses and validates the manifest's scene document, verifies its environment binding against the pipeline, and uses its authored lights and shadow-light direction.

PipelineEditor preview models should be cheap representative primitives or preview-only models covering every material binding. Their transforms and camera are not used by gameplay. The Play state copies the scene document's lights into `mScene->setPbrLights(...)`. It uses the directional light for the package shadow domain and updates the shadow focus around the race camera/lead ship as required.

### 4. Resolve stable logical material bindings

Do not bake generation-specific names such as `PbrPipelineRuntime.1/...` into tracks or game XML. The package exports these stable bindings:

| Binding | Initial use |
| --- | --- |
| `Ship.Surface` | Player, opponents, and physics ghost |
| `Track.Asphalt` | Main road surface |
| `Track.Rail` | Path and mesh rails |
| `Track.Mesh` | Generic placed/drivable mesh surface |
| `Track.Shell` | Shell/decorative track geometry |
| `Track.Zone` | Zone geometry |
| `Track.Trigger` | Debug trigger geometry |
| `Track.Fallback` | Model Tool's unassigned-material fallback |

Introduce a lightweight Willpower `PbrMaterialBinding` resource containing only a binding string. It does not create an MPP resource. Track `DependentResource` IDs remain byte-for-byte equal to material keys embedded in `.mppmodel` files, but point to `PbrMaterialBinding` resources. This preserves the existing map indirection while removing dependencies on legacy `MaterialResource`.

`TungstenPbrPackage::resolveMaterial(binding)` returns the loaded `mpp::PbrMaterial` and its current qualified resource name. `Map::resolveMaterialMppName()` accepts `PbrMaterialBinding` during coexistence. The ship game definition changes from a legacy material resource name to the logical `Ship.Surface` binding.

Keep support for `TrackMaterial` and legacy `Material` only until all bundled content has moved. Remove that compatibility code in the cleanup milestone.

### 5. Adopt one tangent-capable vertex contract

PBR model vertices must provide:

1. `position3`, float
2. `normal3`, float
3. `texcoord2`, float
4. `colour4`, normalized unsigned byte
5. `tangent4`, float (`xyz` tangent and `w` handedness)

This extends the current 36-byte game vertex to 52 bytes while retaining its existing colour representation. Author package PBR materials with exactly this attribute contract. Verify in the first implementation milestone whether indexed/non-indexed state participates in MPP material compatibility. If it does, author indexed ship and non-indexed track material variants; do not weaken validation.

Add one shared game-side tangent builder rather than separate ship and track implementations:

- indexed input: accumulate per-triangle tangent/bitangent contributions per vertex, orthonormalize against the final normal, and derive handedness;
- flat track input: calculate each triangle's tangent frame and write it to all three corners;
- degenerate UVs: choose a deterministic orthogonal tangent from the normal and use handedness `+1`;
- placed meshes: compute tangents after placement transforms and indexed-to-flat expansion;
- reject non-finite positions, normals, UVs, or generated tangents with mesh-qualified diagnostics.

Tangents affect rendering only. Collision validation continues to consume the original serialized position data and freshly baked TrackData normals as it does today.

### 6. Present before overlays

The XML PBR graph renders to the `screen`/`presentation` import owned by `PbrPipelineRuntime`; it is not the Launcher's backbuffer. `StatePlayTungstenMonoxide::renderImpl()` must:

1. render the live scene with `TungstenMonoxide.Pbr`;
2. blit the package presentation `RenderTexture` to `RenderSystem::getScreenRenderTarget()` using the same texture-diagnostic presentation operation as DemoSuite;
3. draw race HUD text;
4. allow the Launcher's existing ImGui pass to draw last.

Remove legacy `setAmbientColour`, `setLightCount`, and `setLight1Colour` calls from Play. PBR lights/environment come from the package contract.

## Authored asset layout

Keep editable source and the runtime export separate:

```text
cpp/tungsten-monoxide/pbr/
  TungstenMonoxide.pipeline.yaml
  TungstenMonoxidePreview.scene.yaml
  TungstenMonoxideMaterials.xml
  textures/...
  environment/...

cpp/tungsten-monoxide/resources/
  TungstenMonoxide.mpppackage
```

Start from PipelineEditor's `Shadows.pipeline.yaml` template (or `Full.pipeline.yaml` only if bloom is required for the first release), retain an RGBA16F scene target, and retain an explicit tone-map presentation pass to RGBA8. Use an HDR equirectangular environment when a production environment is available; neutral documented IBL fallback is acceptable for the first functional milestone but must produce a visible warning.

Initial material conversion:

- Existing colour textures become sRGB base-colour maps.
- Metallic-roughness, normal, occlusion, and emissive maps are linear.
- Until authored maps exist, use metallic `0`, roughness approximately `0.6-0.8`, normal scale `1`, and no emissive contribution.
- Trigger/zone debug materials may use emissive factors, but keep them opaque initially so existing visibility and wireframe controls remain deterministic.
- Preserve sampler wrapping needed by tiled track textures.

The committed `.mpppackage` is generated only through PipelineEditor's Export Package action. Reviewers must be able to reproduce it from the committed workspace. Do not hand-edit ZIP contents.

## Implementation milestones

### Milestone 0 — Capture the legacy baseline

**Status: complete.** See `TUNGSTEN_MONOXIDE_PBR_LEGACY_BASELINE.md`; its mapping guard was advanced in Milestone 3 to CTest `tungsten_monoxide_pbr_material_bindings`.

Files: tests and documentation only.

- Capture screenshots of the start grid, rails, road, ship, trigger debug view, wireframe view, and physics ghost.
- Record bundled model mesh layouts, material keys, and maximum vertices per mesh.
- Add a small test that confirms all material keys in bundled `NewTrack.mppmodel` have a declared mapping.
- Confirm current Release build and CTest suite pass.

Exit criteria: the visual and data baseline is checked in or linked from this plan, and no runtime behavior changed.

### Milestone 1 — PBR package authoring spike

**Status: complete.** The authored workspace is under `cpp/tungsten-monoxide/pbr/`, the standard PipelineEditor export is `cpp/tungsten-monoxide/resources/TungstenMonoxide.mpppackage`, and `pbr/validate_package.py` guards its self-contained binding and geometry contract. PipelineEditor validation, package export, and DemoSuite `--package-smoke-test` pass.

Files: `cpp/tungsten-monoxide/pbr/**`, temporary package-validation test.

- Create the pipeline workspace in PipelineEditor from the Shadows template.
- Author all eight bindings and representative preview objects.
- Configure one directional key light, optional fill/point lights within MPP's light limit, environment binding, shadows, HDR target, and tone mapping.
- Export `TungstenMonoxide.mpppackage`.
- Validate that package extraction, manifest paths, pipeline, scene, transitive textures/shaders, and every required binding are self-contained.
- Run a focused MPP compatibility spike proving the selected 52-byte PBR material specification renders both the indexed ship and non-indexed track model form. If indexed state is strict, split material specifications as described above.

Exit criteria: DemoSuite can load the package with `--package`, preview all bindings, and complete `--package-smoke-test` without source-tree dependencies.

### Milestone 2 — Package host and build integration

**Status: complete.** `TungstenPbrPackage` now follows DemoSuite's manifest/extraction/document/runtime/graph/pipeline workflow, validates all eight PBR bindings, and is initialized by Controller before Load and MapLoad. Debug and Release launcher configurations provide the package path; legacy Play rendering remains active.

New files (names may be adjusted to project conventions):

- `cpp/tungsten-monoxide/include/TungstenPbrPackage.h`
- `cpp/tungsten-monoxide/src/TungstenPbrPackage.cpp`

Modified files:

- `cpp/tungsten-monoxide/CMakeLists.txt`
- `cpp/tungsten-monoxide/include/TungstenMonoxideModel.h`
- `cpp/tungsten-monoxide/src/DLL.cpp`
- `cpp/tungsten-monoxide/include/StateControllerTungstenMonoxide.h`
- `cpp/tungsten-monoxide/src/StateControllerTungstenMonoxide.cpp`
- Launcher Debug/Release configuration or a documented default package path

Work:

- Build/link MPP's `MppResourceParsers` and `MppAppSupport` libraries and include their headers.
- Deploy the `MppResourceParsers` runtime DLL beside Launcher; `MppAppSupport` supplies manifest/ZIP support.
- Add a configurable `PbrPackage` DLL argument, defaulting to `cpp/tungsten-monoxide/resources/TungstenMonoxide.mpppackage` through the active resource configuration.
- Extract into `mpp::app::createUniqueTemporaryDirectory`, read and validate `manifest.xml`, and keep the directory alive for the service lifetime.
- Reproduce DemoSuite's ordering: parse documents; validate; declare/load/create `RenderGraphStream`; rebuild `PbrPipelineRuntime`; configure imports, outputs, environment, bloom, and shadows; create `XmlGraphPbrForward`; prepare external outputs; then accept the runtime generation.
- Validate the complete required-binding set and resource type (`mpp::PbrMaterial`).
- Clean up in dependency order: live models/scenes, pipeline, runtime, graph resource, extracted directory.

Exit criteria: Launcher reaches MapLoad with the package pipeline and all material resources already declared, while existing legacy Play rendering remains operational.

### Milestone 3 — Binding resources and staged coexistence

**Status: complete.** AppLib now owns the lightweight `PbrMaterialBinding` resource and factories. The ship and all seven embedded track material IDs resolve through the package's stable logical bindings. Package-backed track and ship streams are prepared with concrete runtime PBR material names during threaded loading. Parallel legacy streams remain the presented Play models until Milestone 5 because MPP intentionally rejects `PbrMaterial` in the legacy render pass; this is explicit staged coexistence, not an error fallback.

New files:

- `cpp/applib/include/applib/PbrMaterialBinding.h`
- `cpp/applib/src/PbrMaterialBinding.cpp`
- corresponding resource/definition factory files and tests

Modified files:

- `cpp/applib/CMakeLists.txt`
- `cpp/tungsten-monoxide/src/DLL.cpp`
- `cpp/tungsten-monoxide/resources/Resources.yaml`
- `cpp/tungsten-monoxide/src/Map.cpp`
- `cpp/tungsten-monoxide/include/Game.h`
- `cpp/tungsten-monoxide/src/GameDefinitionFactory.cpp`

Work:

- Register `PbrMaterialBinding` factory support.
- Add binding resources and redirect map dependents to them while preserving dependent IDs embedded in model files.
- Resolve binding resources through `TungstenPbrPackage` in `Map.cpp`.
- Change ship configuration to a logical PBR binding.
- Retain legacy resolution branches temporarily and add explicit diagnostics identifying the model, mesh, embedded key, and missing binding.
- Ensure worker-thread map construction only reads the already-initialized binding table; package creation and GPU work remain on the render thread.

Exit criteria: generated track and ship model streams reference package-owned `PbrMaterial` names, with threaded loading enabled and no legacy material required by those models.

### Milestone 4 — Tangent-capable geometry

**Status: complete.** `gameMeshSpecification()` now defines the shared indexed/non-indexed game contract and asserts its 36-byte compatibility or exact 52-byte PBR stride. Indexed ship, flat track, and placed-mesh streams generate stable tangent frames; the package-backed streams are loaded to validate them against their concrete PBR materials while legacy Play remains the presentation path until Milestone 5. CTest covers conventional and mirrored UVs, shared indexed vertices, degenerate UV fallback, transformed placement data, preserved source channels, out-of-range indices, and non-finite position/normal/UV rejection.

New shared files:

- `cpp/tungsten-monoxide/include/PbrMeshSpecification.h`
- `cpp/tungsten-monoxide/src/PbrMeshSpecification.cpp`
- `cpp/tungsten-monoxide/include/PbrVertexConversion.h`
- `cpp/tungsten-monoxide/src/PbrVertexConversion.cpp`
- focused unit tests

Modified files:

- `cpp/tungsten-monoxide/src/Map.cpp`
- `cpp/tungsten-monoxide/src/StatePlayTungstenMonoxide.cpp`

Work:

- Define the 52-byte mesh specification once and remove duplicate ship/track specifications where possible.
- Convert serialized ship vertices and indices, generate smooth tangents, and feed `ProgrammaticModelStream` the expanded layout.
- Expand ordinary track triangles and placed mesh-object triangles with tangent4.
- Preserve mesh names, material keys, debug visibility, collision positions, and index bounds checks.
- Test a conventional UV triangle, mirrored UV handedness, shared indexed vertices, degenerate UV fallback, transformed placement, and non-finite rejection.

Exit criteria: every rendered game mesh satisfies its PBR material specification and all collision/start-grid regression tests remain unchanged.

### Milestone 5 — Switch Play to the package pipeline

**Status: complete.** AppLib now exposes a legacy-default pipeline-selection hook, while Play selects the already-created package-owned `TungstenMonoxide.Pbr` pipeline without constructing `Play`. The live dynamic scene uses package-authored PBR lights, a ship-focused directional shadow domain, package-bound track and ship models, and the authored `XmlGraphPbrForward` pass/output plan. Each frame renders the graph, blits its tone-mapped presentation target, then draws the HUD and Launcher-owned ImGui. Viewport changes resize the package output before updating the live scene viewport; zero-sized minimized windows are skipped and failed resizes retain the previous target with actionable diagnostics. TAA remains authored off and is guarded by package validation.

Modified files:

- `cpp/applib/include/applib/State.h`
- `cpp/applib/src/State.cpp`
- `cpp/launcher/include/ImGuiDataProvider.h`
- `cpp/tungsten-monoxide/include/StatePlayTungstenMonoxide.h`
- `cpp/tungsten-monoxide/include/TungstenPbrPackage.h`
- `cpp/tungsten-monoxide/pbr/validate_package.py`
- `cpp/tungsten-monoxide/src/StatePlayTungstenMonoxide.cpp`
- `cpp/tungsten-monoxide/src/TungstenPbrPackage.cpp`

Work:

- Add the virtual pipeline-selection hook with a legacy default.
- Select `TungstenMonoxide.Pbr` for Play without creating a legacy `Play` pipeline first.
- Apply package-authored PBR lights to the live scene and configure/update the shadow domain.
- Render to the package graph, present to the screen, then render HUD.
- Update the Launcher ImGui backend for MPP's vector-backed texture bindings so debug UI remains usable after presentation.
- Detect viewport-size changes and call the package runtime's resize path before rendering; retain old resources and report a clear error if resize fails.
- Ensure camera cuts/respawns notify temporal AA if TAA is enabled. Keep TAA disabled until this is wired and tested.

Exit criteria: the bundled race renders entirely through `XmlGraphPbrForward`; HUD, ImGui, visibility toggles, wireframe, ghost, resizing, and shadows still work.

### Milestone 6 — Remove TungstenMonoxide legacy materials

**Status: complete.** TungstenMonoxide now declares only package-backed `PbrMaterialBinding` resources for 3D rendering. Stable embedded model keys remain unchanged, but resolve directly to those bindings; the parallel legacy track model, ship fallback, programs, shaders, render materials, and represented loose images are gone. AppLib's now-unused runtime `TrackMaterial` wrapper and factories were removed. The editor preserves its authored `Tracks/AsphaltTrack`/`Tracks/DefaultTrack` choices through editor-only metadata on PBR bindings and exports the same stable model keys without loading legacy render resources.

Modified files:

- `cpp/applib/CMakeLists.txt`
- `cpp/applib/include/applib/TrackMaterial*.h` (removed)
- `cpp/applib/src/TrackMaterial*.cpp` (removed)
- `cpp/editor/include/MaterialCatalog.hpp`
- `cpp/editor/include/MppModelExport.hpp`
- `cpp/editor/main.cpp`
- `cpp/editor/src/MaterialCatalog.cpp`
- `cpp/editor/src/MppModelExport.cpp`
- `cpp/tungsten-monoxide/include/Map.h`
- `cpp/tungsten-monoxide/resources/Resources.yaml`
- `cpp/tungsten-monoxide/resources/images/**` (represented legacy images removed)
- `cpp/tungsten-monoxide/resources/model.xml` (removed)
- `cpp/tungsten-monoxide/resources/shaders/**` (removed)
- `cpp/tungsten-monoxide/src/DLL.cpp`
- `cpp/tungsten-monoxide/src/Map.cpp`
- `cpp/tungsten-monoxide/src/StatePlayTungstenMonoxide.cpp`
- `cpp/tungsten-monoxide/tests/pbr_material_binding_mapping_tests.cpp`
- `docs/tungsten-monoxide.md`

Work:

- Remove `TrackVertexShader`, `TrackFragmentShader`, `TrackProgram`, legacy images represented in the package, and all legacy `Material` definitions from TungstenMonoxide resources.
- Remove duplicate `DefaultRailMaterial` definition while deleting the legacy block.
- Remove `TrackMaterial` wrappers/factories if repository-wide search confirms no remaining users.
- Remove legacy fallback branches from map and ship material resolution.
- Keep legacy AppLib state pipeline support for non-Play states; it is outside this migration's scope.

Exit criteria: repository search finds no TungstenMonoxide 3D model reference to a legacy `Material`/`Program`, and deleting the old shader files does not affect startup.

## Validation matrix

Run all of the following before merging:

1. `cmake -S cpp -B cpp/build`
2. Build MPP dependencies including `MppResourceParsers` and `MppAppSupport` in Release.
3. `cmake --build cpp/build --config Release`
4. `ctest --test-dir cpp/build -C Release --output-on-failure`
5. DemoSuite package load and package smoke test against the committed `.mpppackage`.
6. Launcher startup with threaded loading both enabled and disabled.
7. Launcher resize/minimize/restore and clean shutdown (temporary extraction directory removed).
8. Drive a full lap and verify road, rails, placed meshes, ship, shadows, and environment.
9. Toggle triggers, rails, reservation walls, wireframe, and physics ghost.
10. Verify HUD and ImGui are not tone-mapped, blurred, hidden behind, or captured into the PBR presentation texture.
11. Rename/remove the package and one required binding in separate negative tests; each must fail before MapLoad with actionable diagnostics.
12. Run from a copied Launcher output directory with the source checkout unavailable to prove package self-containment.

## Rollback strategy

Keep the migration in independently revertible milestones. Until Milestone 6, legacy resources and resolution paths remain present, so reverting the Play pipeline selection restores the old renderer without reverting collision or gameplay changes. Do not add an automatic runtime fallback switch: it would hide broken package exports and permit mixed PBR/legacy material state. Rollback is a build/release choice, not an error-recovery path.

## Completion criteria

The migration is complete when:

- PipelineEditor can open the committed workspace and reproduce the shipped package.
- DemoSuite and TungstenMonoxide load the same standard package contract.
- Play uses `XmlGraphPbrForward`, HDR intermediate rendering, tone-mapped presentation, PBR lights, and PBR environment resources.
- All dynamic game meshes include valid tangent4 data and use package-resolved PBR materials.
- No package path escapes to the authoring/source tree.
- No TungstenMonoxide 3D asset depends on the old track shaders, programs, or legacy material resources.
- Release build, CTest, package smoke test, and the manual validation matrix pass.
