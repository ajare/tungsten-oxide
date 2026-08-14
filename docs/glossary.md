# Glossary

Repo-wide terminology. Add to this as new ADRs/designs introduce terms a
future reader wouldn't already know from the code alone — this file doesn't
try to re-document things that are self-evident from file/class names.

## model-tool (see `docs/adr/0001-model-tool.md`)

- **model-tool** — new native app (`cpp/model-tool`, CMake target
  `model_tool`) for importing a 3D model file (OBJ/FBX/USD/glTF) via AssImp,
  previewing it in a live mpp viewport, and saving it as `.mppmodel`.
- **mppmodel** — MassivePolyPusher's binary model file format (magic `MPPM`),
  documented in `MPPMODEL_EXPORT_SPEC.md`: a fixed six-entry directory
  (MaterialNames, Materials, VertexData, IndexData, MeshMetadata) over a flat
  array of meshes. Has no node/scene-graph concept — every mesh is independent.
- **ModelConvert** (`ext/massive-poly-pusher/model-convert`) — the existing
  AssImp → `.mppmodel` CLI converter, driven by an external `modelspec.xml`
  for an arbitrary vertex layout. Precedent for model-tool's import, but not
  reused directly (see ADR 0001, D3).
- **AssImpModelLoader** (`ext/massive-poly-pusher/model-convert/include/
  AssImpModelLoader.h`) — ModelConvert's own AssImp traversal class, built
  around `ModelspecStream`/`MeshSpecification`'s spec-driven arbitrary
  layout. Not reused by model-tool (ADR 0001, D3).
- **Fixed vertex layout** — model-tool's one hardcoded interleaved vertex
  format: position (float×3), normal (float×3), uv (float×2), colour
  (unorm8×4) — 36 bytes/vertex. The same stride/shape `cpp/editor`'s
  `MppModelExport.cpp` uses for track geometry, though the two are otherwise
  independent implementations for different source domains.
- **`__mpp_tex_none__`** — mpp's built-in sentinel texture name for "no real
  texture", used wherever a default/blank texture is needed (e.g.
  `StatePlayTungstenMonoxide::createTorusMaterial()`, and model-tool's
  default-white material fallback).
- **`RenderTexture`** (`mpp::RenderTexture`, `mpp/RenderTexture.h`) — an
  off-screen render target that is also a sampleable `mpp::Texture`, created
  via `RenderSystem::createRenderTexture()`. model-tool renders its preview
  scene into one and displays it with `ImGui::Image()` — the first place in
  this repo an mpp scene renders into a docked ImGui panel rather than to the
  default framebuffer full-screen.
- **`aiProcess_PreTransformVertices`** — an AssImp import post-process flag
  that bakes every node's transform into its meshes' vertex data, collapsing
  a scene's node hierarchy into root/world space. model-tool always passes
  this, since `.mppmodel` has no node concept to preserve it in.

## Native track resource documents (see `docs/adr/0002-track-resource-save-load.md`)

- **Track metadata name** — editable `TrackDefinition::name`, serialized as the top-level schema-10
  JSON `name`. It is display metadata and does not rename an already-bound resource.
- **Track resource identity** — stable `(namespace="Tracks", Resource@name)` key captured when a
  Track is loaded or first saved. Save As copies this identity; changing track metadata does not.
- **Track save binding** — the editor session's association with one Resources XML path, Track
  resource identity, safe relative JSON/model references, and external-change fingerprints.
- **Track sidecars** — the schema-10 `<TrackData>` JSON (authoritative editable source) and generated
  `<ModelFile>` `.mppmodel` associated with a Track Resource.
- **Logical Tracks namespace** — all root-level `<Namespace name="Tracks">` blocks in one Resources
  XML considered together. Multiple blocks are accepted, but duplicate resource identities are
  ambiguous.
- **Unavailable TrackMaterial** — a non-empty path material assignment preserved from track JSON
  but absent from the catalog currently loaded through `editor.ini`. It is not silently replaced;
  Save remains blocked until refresh or reassignment resolves it.

## Existing terms referenced by ADR 0001

- **willpower.application Resource system** — the declarative,
  `Resources.yaml`-driven asset-graph layer (`Resource`/`ResourceManager`/
  `DependentResources`/`Definition` factories) `cpp/tungsten-monoxide` builds
  on. Distinct from bare `mpp::ResourceManager`, which just tracks/loads
  individual GPU-side resources with no declarative file format involved.
- **`ProgrammaticModelStream` / `ProgrammaticMaterialStream`** — mpp's
  in-memory, code-constructed (as opposed to file/XML-parsed)
  `ResourceStream` implementations for building a `Model`/`Material` at
  runtime without an on-disk resource definition. See
  `StatePlayTungstenMonoxide::createTorusModel()`/`createTorusMaterial()` for
  the established usage pattern.
- **`GeometryBatch` / `RenderVertex`** (`cpp/core/include/TrackGeometry.hpp`)
  — the track engine's own renderer-neutral geometry record (double-precision
  position/normal/uv/rgba, always-unshared triangle-soup indices). Not the
  same thing as model-tool's fixed vertex layout, though the two share the
  same "position, normal, uv, colour" shape by convention.
