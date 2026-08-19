# 0003 — `model-tool` and the editor gain a `<Model>` XML layer; a Track resource embeds a `<Models>` list

Status: Accepted (design only — not yet implemented)
Date: 2026-08-04

## Context

`0001-model-tool.md`'s D1 decided `model-tool` would talk to `mpp::RenderSystem`/
`mpp::ResourceManager` directly with no willpower Resource/XML layer, because it
had "no declarative resource files to author" — it only ever loaded whatever
file the user picked at runtime and saved a single `.mppmodel` back out.

A Track resource's schema is changing (`docs/TRACK_MODEL_LIST_PLAN.md`): instead
of one `<ModelFile>`/`<TrackData>` pair, a Track's `<Definition>` now embeds a
`<Models>` list, each `<Model>` carrying a `.mppmodel` reference, optional
`TrackData`, and per-mesh `<Meshes><Mesh><Type>/<Visible></Mesh></Meshes>`
metadata (`D:\Code\Projects\example_track_def.xml`). Authoring that per-mesh
metadata is `model-tool`'s job — it is the one place a `.mppmodel`'s meshes are
already listed and edited. That means `model-tool` now DOES have a declarative
resource file to author: a `<Model>` fragment, standalone or embedded in a
Track resource's own XML. D1's premise no longer holds.

## Decision

**D1 of `0001-model-tool.md` is superseded.** `model-tool` gains a real XML
layer for the `<Model>` fragment (not a full willpower Resource/`Definition`
system — see Scope below): it can open a `<Model>` XML file directly (standalone
root `<Model>`, or embedded inside a Track resource's `<Definition><Models>`
list), edit each mesh's `Type`/`Visible` metadata, and save back to whichever
shape it was opened from. Every other decision in `0001-model-tool.md` (D2–D11)
is unaffected and stands as written.

The editor (`src/editor`) gains the same `<Model>` fragment read/write
capability, plus the ability to parse/write a whole Track resource's `<Models>`
list (already partially true via `TrackResourceDocument.cpp`/
`TrackResourceSave.cpp`, extended here) and to load and render the geometry
those entries reference — reversing `DRIVABLE_MESH_OBJECTS_PLAN.md`'s
"`.mppmodel` loading is host-only, never in `core`/the editor" architecture
note for the editor specifically (`core` itself is untouched — see
`docs/TRACK_MODEL_LIST_PLAN.md`'s architecture notes).

### Scope: a fragment schema, not a willpower Resource

The new `src/model-xml` library (`docs/TRACK_MODEL_LIST_PLAN.md` Milestone 2)
is a small TinyXML2-based reader/writer for exactly the `<Model>` fragment
shape — it does not wrap a Model as a willpower `Resource`/`Definition` the
way `src/tungsten-monoxide`'s `Map` does for a Track. D1's original reasoning
("the willpower Resource/XML layer exists to let a *game* declare its asset
graph ahead of time; a load-preview-save utility has no such graph to
declare") still holds at that larger scope — `model-tool` remains a
load-preview-save utility with no willpower `Resource`/`ResourceManager`
involvement (D1's second sentence, about `mpp::ResourceManager`, is untouched).
What's added is authoring one specific, narrow XML shape it now needs to read
and write, not a general resource-declaration system.

### Why the editor gets its own from-scratch reader, not `mpp::ModelSerializer`

`model-tool` already links the full MassivePolyPusher SDK (`0001-model-tool.md`
D2/D5/D9) and uses the real `mpp::ModelSerializer` for its authoritative save
path — that doesn't change. `src/editor` links no part of that SDK today, and
pulling it in solely to render placement geometry would be a far larger,
riskier dependency addition than this feature needs (a GL loader conflict
`MppModelExport.hpp`'s own header comment already documents avoiding once).
Instead, the editor gets a minimal, from-scratch, read-only `.mppmodel`
geometry parser (vertex/index/mesh-name data only, no materials/shaders) —
the mirror image of `MppModelExport.cpp`'s existing from-scratch *writer*,
which already proves a real `.mppmodel` file can be produced/consumed without
linking `mpp::ModelSerializer`. See `docs/TRACK_MODEL_LIST_PLAN.md` Milestone 4.

## Consequences

- `model-tool`'s "Open" dialog gains format auto-detection across three shapes
  (`.mppmodel`, standalone `<Model>` XML, Track resource XML) instead of the
  single `.mppmodel`-only flow D1 assumed.
- The per-sub-mesh collidable/decorative name-suffix convention
  (`CollidableFlag.hpp`, `DRIVABLE_MESH_OBJECTS_PLAN.md` Milestone 4.3) is
  retired in favor of the XML `Type`/`Visible` metadata this ADR introduces —
  a `.mppmodel`'s own mesh names are no longer a metadata carrier.
- Two independent `<Models>`-list XML parsers now exist (editor: TinyXML2 in
  `TrackResourceDocument.cpp`; host: `wp::XmlNode` in
  `MapTungstenMonoxideDefinitionFactory.cpp`), matching the pre-existing split
  for `<TrackData>`/`<ModelFile>` — `src/model-xml` is shared only between
  `model-tool` and the editor, which already share a TinyXML2 dependency and
  build closure; it is not linked into `src/tungsten-monoxide`.
- The editor's "no mpp SDK dependency" property is preserved even though it
  now renders real model geometry — worth extra scrutiny in review, since a
  from-scratch binary-format reader is new, unproven code (mirroring the
  existing scrutiny note on `MppModelExport.cpp`'s writer side).
