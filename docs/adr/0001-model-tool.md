# 0001 — `model-tool`: a standalone AssImp import/preview/save app

Status: Accepted (design only — not yet implemented)
Date: 2026-07-28

## Context

We want a new native app, `model-tool`, living alongside `cpp/editor`, `cpp/app`,
`cpp/launcher`, `cpp/applib`, etc. It should let a user import a 3D model file
(OBJ, FBX, USD, or glTF), preview it in a live 3D viewport, and save it out as
MassivePolyPusher's `.mppmodel` format — the same binary format
`MPPMODEL_EXPORT_SPEC.md` documents and `cpp/editor`'s `MppModelExport.cpp`
already writes (for baked track geometry, a different source domain).

Two existing things in the repo look superficially similar but solve different
problems, and neither one fits whole:

- `ext/willpower/ext/massive-poly-pusher/model-convert` (`ModelConvert.exe`) already does
  AssImp → `.mppmodel` conversion, but is driven by an external `modelspec.xml`
  (`ModelspecStream`/`MeshSpecification`) so it can target an arbitrary,
  user-specified interleaved vertex layout. It also never bakes AssImp node
  transforms into mesh vertices (no `aiProcess_PreTransformVertices`, no manual
  matrix walk) — it only works correctly for scenes whose meshes already sit at
  the scene root.
- `cpp/editor`'s `MppModelExport.cpp` writes `.mppmodel` bytes from scratch
  specifically to avoid linking `mpp::ModelSerializer` (and therefore GLEW,
  `mpp::RenderSystem`'s GL loader), because `cpp/editor` never needed to render
  anything through mpp — it only ever needed to *emit* the file format. It also
  deliberately never embeds real materials (name-only references, `Resources.yaml`
  supplies the rest by hand) and always writes non-indexed triangle soups,
  because that's what baked track geometry always is.

`model-tool` needs a live embedded mpp viewport (so it must link real
`mpp::RenderSystem` anyway, unlike `cpp/editor`) and needs to preserve a source
mesh's real shared-vertex indexing and embed real extracted materials (unlike
the track exporter, which has neither of those things to preserve). So neither
existing precedent's constraints apply, and copying either wholesale would
import unnecessary machinery or unnecessary limitations.

## Decisions

### D1 — Bare mpp, no willpower.application Resource system

**Superseded in part by `0003-model-xml-layer.md`**: `model-tool` gained a
`<Model>` XML fragment read/write layer once the Track resource schema grew a
`<Models>` list with per-mesh metadata that's `model-tool`'s job to author.
This D1 entry (and its "no declarative resource files to author" premise) is
kept here for historical context; see 0003 for what changed and why the
narrower Resource-scope reasoning below still applies to the willpower
Resource/`ResourceManager` system specifically.

`model-tool` talks to `mpp::RenderSystem`/`mpp::ResourceManager` directly and
builds `mpp::Model`/`mpp::Material`/`mpp::Texture` objects programmatically at
runtime (mirroring `ext/willpower/ext/massive-poly-pusher/demo-suite`'s `ModelScene` and
`StatePlayTungstenMonoxide::createTorusModel()`), rather than wrapping the
loaded model as a willpower `Resource`/`Definition` the way
`cpp/tungsten-monoxide`'s `Map` does.

Why: `model-tool` has no declarative resource files to author (no
`Resources.yaml`-equivalent) — it loads whatever file the user picks at runtime.
The willpower resource-definition layer exists to let a *game* declare its asset
graph ahead of time; a load-preview-save utility has no such graph to declare.

### D2 — SDL2 + Dear ImGui docking branch, GLEW as the GL loader

Window/UI shell copies `cpp/editor`'s exact bootstrap (SDL2,
`imgui_impl_sdl2` + `imgui_impl_opengl3`, docking branch, menubar/toolbar/
statusbar/dockspace layout) — but the OpenGL3 ImGui backend is initialized
against **GLEW**, not the `gl3w` loader `cpp/editor` vendors.

Why: `mpp::RenderSystem`'s headers pull in GLEW as *its* GL loader
unconditionally (see `MppModelExport.hpp`'s header comment on why `cpp/editor`
avoids linking `mpp::ModelSerializer` at all, specifically to dodge a
GLEW-vs-gl3w duplicate-loader conflict). `model-tool` must link real
`mpp::RenderSystem` for its viewport (D5), so it inherits GLEW regardless —
and since `model-tool` is its own fresh process, there is no cross-process
conflict with `cpp/editor`'s independent gl3w choice. One GL loader per
process; GLEW is the one already forced on us.

### D3 — Bespoke fixed-layout AssImp converter, not `AssImpModelLoader` reuse

New conversion code walks `aiScene`/`aiMesh`/`aiMaterial` directly and always
emits one fixed 36-byte interleaved vertex layout:

| Channel  | Type          | Bytes |
|----------|---------------|-------|
| position | float × 3     | 12    |
| normal   | float × 3     | 12    |
| uv       | float × 2     | 8     |
| colour   | unorm8 × 4    | 4     |

This does **not** reuse `ext/willpower/ext/massive-poly-pusher/model-convert`'s
`AssImpModelLoader`, which is built around an externally-specified,
arbitrary vertex layout (`ModelspecStream`/`MeshSpecification`) for a use case
that only ever has one target layout here. `aiProcess_PreTransformVertices` is
passed on import specifically to bake node-hierarchy transforms into
world-space vertex data — a gap in `AssImpModelLoader`'s own precedent (see
Context) that would otherwise import hierarchical scenes (common in FBX)
visibly wrong.

Vertex color defaults to opaque white when a mesh has no `mColors[0]`; UV
defaults to `(0,0)` when a mesh has no texture-coordinate channel — both
matching the existing `RenderVertex`/`GeometryBatch` conventions in
`cpp/core`.

### D4 — Materials: diffuse/base-color texture only, ignore all shading constants

Per the original request, only texture *references* are extracted from
`aiMaterial` — specifically `aiTextureType_DIFFUSE`, falling back to
`aiTextureType_BASE_COLOR` for glTF/PBR-style materials — and nothing else
(no flat base-color factors, no shininess/specular/roughness/metalness
constants). This is the only slot the preview shader (D6) can sample, so
extracting other slots (normal/specular/emissive maps) would extract data
nothing uses.

Embedded textures (AssImp's in-memory `aiTexture`, common in `.glb`) are
**skipped for v1** rather than decoded — an imported material whose texture
turns out to be embedded falls back to the default-white material (D7) and
should be reported to the user (e.g. via the status bar), rather than being
silently identical to a "real" texture load succeeding. Only externally
file-referenced textures are loaded, through the existing `loadImage`/
`mpp::TextureStream::setFile` path.

### D5 — One canonical in-memory model, shared by viewport and save

A single `mpp::ProgrammaticModelStream`-built model (mesh data, real AssImp
indices, materials) is both:

- the model added to the live preview scene (`mScene->add3dModel(...)`), and
- the exact model handed to real `mpp::ModelSerializer::save()`/
  `addMaterial()` for "Save as .mppmodel" — no separate export-time
  reconversion, no from-scratch byte writer.

This differs from `cpp/editor`'s `MppModelExport.cpp`, which writes bytes
directly without linking `mpp::ModelSerializer` at all. That constraint doesn't
apply here: `model-tool` links real mpp fully anyway (D2), so using the real
serializer is strictly simpler than reimplementing it, and it can embed real
materials (unlike the track exporter's name-only references, which rely on a
hand-authored companion `Resources.yaml`).

Meshes keep AssImp's real shared-vertex indexing (16-bit or 32-bit, chosen by
vertex count — the same rule `ModelConvert` already uses), rather than being
flattened to a non-indexed triangle soup the way baked track geometry is
(track geometry has no shared vertices to begin with; imported meshes do).

### D6 — Preview shading: lit (ambient + 1 directional light)

One small bundled GLSL program (Position3/Normal3/TexCoord2/Colour4 in, one
diffuse sampler, ambient + single directional light) — matching the one
lighting convention already established in this codebase
(`StatePlayTungstenMonoxide`'s `renderImpl`). A fully unlit/flat-textured
shader was considered and rejected: a model with no diffuse texture would
render as a flat, shapeless silhouette with no cues from its normals.

### D7 — Default texture: mpp's `"__mpp_tex_none__"` sentinel

When a mesh has no usable material/texture, its material uses mpp's existing
built-in sentinel texture name `"__mpp_tex_none__"` (already used by
`StatePlayTungstenMonoxide::createTorusMaterial()` for the same "no texture"
case) rather than bundling and shipping an actual 1×1 white PNG asset.

### D8 — New orbit/arcball viewport camera

`mpp-helper` only vendors `FpsCamera`/`FreeCamera`/`OrthoCamera` — no orbit
camera exists anywhere in this codebase. `model-tool` gets a new, small orbit
camera (drag to orbit around the loaded model's bounding-sphere center,
scroll to zoom, auto-framed on import) rather than reusing `FreeCamera`'s
WASD fly-through, which has no natural "inspect this one object" framing.

### D9 — Viewport rendering target: `mpp::RenderTexture` + `ImGui::Image`

The right-hand view panel is backed by an `mpp::RenderTexture`
(`RenderSystem::createRenderTexture`, confirmed to exist and to be
`Texture`-derived, i.e. it exposes a sampleable GL texture id), rendered into
each frame and displayed via `ImGui::Image()`, resized to the panel's
available content region. This is the first place in the repo an mpp scene is
rendered *into* a docked ImGui panel rather than to the default framebuffer
full-screen (contrast `demo-suite`'s ImGui-as-overlay-on-fullscreen-scene
approach).

### D10 — Left panel: flat mesh list + flat material list, no node tree

The left panel shows two flat lists — meshes (name, triangle count) and
materials (name, extracted diffuse texture path or "default white") — not a
walked scene-graph/node tree. `.mppmodel` itself has no node-hierarchy concept
(a flat mesh array, per `MPPMODEL_EXPORT_SPEC.md` §2.1), and node transforms
are already baked into vertex data at import time (D3), so there is no
structure beyond "list of meshes, list of materials" that survives to what
gets saved.

### D11 — Import UX: one "Import..." menu item, combined format filter

A single File → "Import..." menu item opens a file dialog with a combined
filter across OBJ/FBX/USD/glTF (plus an "All supported" option), rather than
one menu item per format. AssImp dispatches by file content/extension
internally regardless of how the dialog got invoked, so separate menu items
would only duplicate UI for no behavioral difference — matching `cpp/editor`'s
existing single "Import JSON..."/"Export USD..." pattern.

## Consequences

- `model-tool` is the first app in this repo to combine an ImGui-docked shell
  with a live embedded mpp viewport; `RenderTexture`-into-`ImGui::Image` (D9)
  and the orbit camera (D8) are new, unproven code paths worth extra scrutiny
  in review/testing.
- Import quality is capped by v1's scope choices: embedded textures (D4) and
  flat base-color-only materials with no image (D4, by the original request)
  are not represented — a material that "looks fine" in the source DCC tool
  may import as the default-white fallback. This should be visible to the user
  (status bar / material list, not silent).
- Because `model-tool` links `mpp::ModelSerializer` for real (D5), unlike
  `cpp/editor`, it does **not** inherit the upstream `updateDirectoryEntry()`
  offset bug `MPPMODEL_EXPORT_SPEC.md` documents working around — that bug
  only matters to a from-scratch writer imitating the on-disk layout by hand.
  Using the real serializer sidesteps it by construction, the same as it would
  for any other real `mpp::ModelSerializer::save()` caller.
- No willpower.application Resource system involvement (D1) means
  `model-tool` cannot reuse `cpp/tungsten-monoxide`'s existing texture-cache/
  material-cache resource lifecycle; it manages its own `mpp::ResourceManager`
  lifetime directly, same as `demo-suite` does.

## Open follow-ups (not blocking, noted for implementation time)

- UV V-axis convention (top-left vs bottom-left origin) can differ across
  OBJ/FBX/glTF exporters and wasn't resolved in this design pass; watch for
  upside-down/mirrored textures during implementation testing.
- `aiProcess_PreTransformVertices` collapses the whole scene into
  root-space meshes; this is intentional (D3) but means any AssImp
  post-process step that depends on the original node graph (e.g.
  skeletal/animation data) is out of scope — `model-tool` targets static
  meshes only, consistent with `.mppmodel` having no bone/animation concept.
