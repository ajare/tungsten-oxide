# tox::GeometryBatch → mppmodel Export Specification

Status: **implemented** (`cpp/editor/include/MppModelExport.hpp`, `cpp/editor/src/MppModelExport.cpp`,
wired into `track_editor`'s toolbar as "Export MppModel..." next to "Export USD..."). This
documents how to convert the C++ core library's renderer-neutral `tox::GeometryBatch`
(`cpp/core/include/TrackGeometry.hpp`) into the `.mppmodel` binary format that
`ext/massivepolypusher`'s `mpp::ModelSerializer` reads and writes, by feeding it the same way
`model-convert`'s `ModelConvert.exe` does today for AssImp-loaded meshes. Written after reading
`ext/massivepolypusher/model-convert/` (`Main.cpp`, `AssImpModelLoader.h/.cpp`) and
`ext/massivepolypusher/mpp/` + `mpp-mesh/` (`ModelSerializer.h/.cpp`, `MeshDefinition.h`,
`VertexBufferDefinition.h`, `VertexBufferAttributeLayout.h`, `Vertex.h/.cpp`, `Primitive.h`).

**Implementation deviates from §6's literal proposal in one deliberate way, decided with the
user before writing code:** it does **not** link `mpp::ModelSerializer` itself.
`mpp/include/mpp/ModelSerializer.h` unconditionally includes `<glew/glew.h>`/`<gl/gl.h>` and
transitively drags in `mpp/ResourceManager.h` → `mpp/RenderSystem.h` (MassivePolyPusher's whole
OpenGL rendering/resource-management subsystem) just to compile the header, even though
`ModelSerializer::save()` itself never calls a GL function. Linking it in would mean adding GLEW
as a second GL function-pointer loader alongside `cpp/editor`'s existing `gl3w` (real
duplicate-symbol risk) and compiling/linking a large slice of `mpp`'s resource subsystem
(`ResourceStreamSerializer`, `ResourceManager`, program/texture streams, XML parsing) just to
satisfy the linker for code paths (`writeMaterial`) never exercised, since materials are
deliberately deferred (§5, option 1). `MppModelExport.cpp` instead emits the documented binary
layout directly, verified field-for-field against `ModelSerializer.cpp`'s `write*`/`read*` pairs
— no new build dependency, no GL-loader risk.

**A real bug in `mpp::ModelSerializer::save()` was found and deliberately not reproduced:** its
`updateDirectoryEntry()` seeks to `sizeof(Header) + type * sizeof(Directory::Entry)` to backpatch
each directory entry after writing that section, but `writeDirectoryEntry()` only ever writes 16
bytes (4× `uint32_t`) per entry on disk. Compiling a standalone probe against the identical
struct layout (same compiler, same platform) confirmed `sizeof(Header) == 12` (coincidentally
matching the real 12-byte on-disk header) but `sizeof(Directory::Entry) == 32` (`size_t`-sized
`start`/`end`/`count` fields plus alignment padding after the 4-byte `Type` enum) — not 16. That
backpatch therefore seeks to the wrong byte offset for every directory entry after the first
(`Unused`, type 0, where `0 * anything == 0` hides the bug), corrupting whatever real content sat
at the wrongly-computed offset. The *read* path (`readDirectory`/`readDirectoryEntry`) has no such
bug — it just reads six 16-byte entries sequentially, no seeking — so a file whose directory is
computed and written correctly up front, with no backpatching at all, loads back correctly despite
this latent upstream issue. `MppModelExport.cpp` builds every section into memory first
specifically so every offset/count is known before anything is written, sidestepping the bug
entirely rather than imitating it.

## 1. How `model-convert` does it today (reference pipeline)

`model-convert/src/Main.cpp`'s `convert()` is the whole pipeline in miniature:

```
ModelspecStream (parses -s <spec.xml>)   -->  mpp::mesh::MeshSpecification
AssImpModelLoader(file, spec)::load()    -->  N x mpp::mesh::MeshDefinition
  for each MeshDefinition:
    fileSaver.setName/setMaterial/setPrimitiveType/setPrimitiveCount/setIndexBuffer
    for each VertexBufferDefinition on it:
      fileSaver.addVertexStream(meshIndex, vertexCount, vertexStride, rawBytes)
fileSaver.save(outFile)                  -->  <name>.mppmodel
```

The `MeshSpecification` (parsed from an XML "modelspec" file's `<Buffers>` element, see
`mpp-mesh-specification-parser/src/SpecificationParser.cpp::parseMeshSpecification` and
`demo-suite/resources/res/cube/cube.modelspec.xml`) is the **only** place the interleaved vertex
byte layout is described. It says nothing the binary format doesn't already need at write time,
but everything a *reader* needs to know how to interpret the raw vertex bytes back out — see §2.3.

`AssImpModelLoader::load()` (`model-convert/src/AssImpModelLoader.cpp:568-644`) does the actual
per-source-mesh conversion: for each `aiMesh`, it builds one interleaved `float` buffer per
component present (position/normal/texcoord/colour), each tagged with a `Vertex::Component` +
`Vertex::DataType::Float` + byte offset/stride into that source buffer
(`createMeshDataStreams`, lines 177-397). It then walks the target `MeshSpecification`'s buffer
layout(s) and, for each one, either:

- `copyVertexBufferData` — a straight `memcpy` when the source streams already happen to be
  tightly packed in exactly the target layout's order/types (`streamsAreTightlyPacked`,
  lines 443-489), or
- `deinterlaceVertexBufferData` — per-vertex, per-attribute repacking that also does the actual
  type conversion (`float` → `UnsignedByte` via `*255`, `float` → `HalfFloat` via the vendored
  `half` library, or a straight `memcpy` for `float` → `float`), plus zero-padding
  (`attrib.paddingBytes`) (lines 507-562).

Index data is packed into raw bytes at either 16 or 32 bits per index (`indexWidth`), chosen by
vertex count vs `numeric_limits<uint16_t>::max()` (lines 265-272), little-endian, during
`createMeshDataStreams` (lines 370-393).

## 2. The `.mppmodel` binary format (`mpp::ModelSerializer`)

Read from `mpp/src/ModelSerializer.cpp`'s `write*`/`read*` pairs — this is the *actual* on-disk
contract, more precise than the API surface alone.

### 2.1 Layout

```
Header                (magic 'MPPM', u16 versionMajor=1, u16 versionMinor=1, u32 flags)
Directory              6 fixed-size entries (Unused, MaterialNames, Materials, VertexData,
                        IndexData, MeshMetadata), each {u32 type, u32 startOffset, u32 endOffset,
                        u32 count} -- offsets backpatched via updateDirectoryEntry() after each
                        section is written, since sizes aren't known up front.
MaterialNames section   count x length-prefixed string
Materials section       count x ResourceStreamSerializer-serialized ResourceStreamPtr (a whole
                        separate XML-resource subsystem: mpp/ResourceStreamSerializer.h --
                        programs, textures, uniforms. Out of scope for geometry conversion; see §5.)
VertexData section      count x { u32 dataSizeBytes, u32 vertexCount, u32 vertexStride, raw bytes }
IndexData section       count x { u32 dataSizeBytes, u32 indexWidthBits, raw bytes }
MeshMetadata section    count x { str name, u32 primitiveType, u32 primitiveCount, str material,
                        u32 numVertexBuffers, u32 vertexBufferId[numVertexBuffers], u32 indexStreamId }
```

`FLAG_INDEXED_VERTICES` is unconditionally set by `writeHeader()` (`ModelSerializer.cpp:224-227`)
regardless of what's actually written — upstream, every `.mppmodel` file is nominally "indexed".
Nothing in `ModelSerializer::load()` ever reads the flag back, though: `readIndexBuffers()` is
driven purely by the `IndexData` directory entry's `count`. A writer can therefore emit zero index
streams and clear the flag, and the read path handles it without special-casing — which is exactly
what `MppModelExport.cpp` does (see §4.3).

### 2.2 What is and isn't self-describing

**Not written per vertex-stream:** the attribute layout (which bytes are position vs normal vs
uv vs colour). `writeVertexBuffer` (`ModelSerializer.cpp:490-505`) only ever writes
`{size, count, stride, rawBytes}` — the *meaning* of those bytes is entirely external. In the
`model-convert` pipeline that meaning is the `MeshSpecification` read from the `-s` spec file,
which is separately embedded per-material inside the `.material` XML (see `MaterialInformation.h`
and the commented-out `<MeshSpecification>` override block in `cube.modelspec.xml:48-74`) — a
consumer resolves a mesh's material, then that material's mesh spec, to know how to decode the
mesh's vertex streams. **A converter that skips writing materials must still ensure whatever does
end up reading the file has an out-of-band way to know the vertex layout** (see §4.1 and §5).

**Written per mesh:** primitive type, primitive count, and index width — enough to know the
*triangle* structure without a spec, just not the vertex *attribute* structure.

### 2.3 The `ModelSerializer` write API (used directly, no `MeshDefinition`/`AssImpModelLoader` needed)

```cpp
mpp::ModelSerializer fileSaver;                      // resourceMgr may be nullptr for writing
fileSaver.addMaterial(name, resourceStreamPtr);       // 0+ times, before or after setMeshCount
fileSaver.setMeshCount(n);
for (i in 0..n) {
  fileSaver.setName(i, name);
  fileSaver.setMaterial(i, materialName);              // string; need not resolve to an addMaterial() call
  fileSaver.setPrimitiveType(i, Primitive::Type::Triangles);
  fileSaver.setPrimitiveCount(i, triangleCount);
  fileSaver.setIndexBuffer(i, packedIndexBytes, indexWidthBits);   // 16 or 32
  fileSaver.addVertexStream(i, vertexCount, vertexStrideBytes, packedVertexBytes);  // 1+ times
}
fileSaver.save(outFile);
```

This is the whole surface a converter needs — `MeshDefinition`/`VertexBufferDefinition`/
`AssImpModelLoader` are `model-convert`'s own intermediate representation for going from AssImp's
data model to this API; a `tox::GeometryBatch`-based converter doesn't need them and can target
`ModelSerializer` directly, since `GeometryBatch` is already a flat, mesh-shaped record.

## 3. `tox::GeometryBatch` (source format)

```cpp
struct RenderVertex { Vec3 position, normal; Vec2d uv; Color4 rgba; };  // all double-precision
struct GeometryBatch {
  std::string id, materialKey;
  GeometryKind kind;                    // PathSurface | PathShell | PathRail | MeshSurface | MeshRail | ZoneSurface
  std::vector<RenderVertex> vertices;
  std::vector<std::uint32_t> indices;
  bool hasUv{false};
  std::optional<TextureBinding> texture;  // { assetId, tile }
};
```

Facts that matter for the converter, established by reading `cpp/core/src/TrackBake.cpp`'s
`Builder::tri()` (lines 378-386) and `cpp/core/src/TrackMesh.cpp`'s `addTriangle()`
(lines 46-53) — the only two places that ever populate a batch:

- **Every triangle gets three brand-new vertices.** Nothing is ever deduplicated or shared; for
  every batch, `indices.size() == vertices.size() == 3 * triangleCount`, and
  `indices[k] == k` — the index buffer is the identity permutation, i.e. the batch is already a
  triangle soup and the indices carry no information. This is why the export is non-indexed
  (see §4.3).
- **Normals are flat, per-triangle** (`triNormal`/`normalOf`, both a plain cross-product of two
  edges, normalized) — not vertex-averaged smooth normals. All 3 vertices of one triangle share
  the identical normal.
- **`uv` is only meaningful when `hasUv` is true.** `TrackMesh.cpp`'s `addTriangle` always leaves
  `uv` at `Vec2d{}` (`{0,0}`) and never sets `hasUv` (mesh-region/rail batches carry no UVs), and
  neither does `TrackBake.cpp`'s `"shell"`/`"rail"` batches. `TrackBake.cpp`'s road-surface
  (`"road"`, `PathSurface`) and both zone-surface batches (`"zone-<effect>"`, path-hosted and
  mesh-hosted, `ZoneSurface`) set `hasUv = true` and compute real `(u, v)` per vertex.
- **`rgba` is always the struct default** `Color4{1,1,1,1}` — nothing in the codebase ever writes
  a non-white vertex colour today (confirmed by `track_tests.cpp`'s "render RGBA defaults white"
  assertion). Safe to treat as an always-white/unused channel for now.
- **Units/axes:** double-precision metres, Y-up, right-handed (see `Vec3.hpp`'s header comment) —
  the same convention `cpp/editor/src/USDExport.cpp` assumes (`upAxis = "Y"`, `metersPerUnit = 1`)
  when walking these same batches. No evidence `mpp`'s `RenderSystem.cpp` enables
  `GL_CULL_FACE` anywhere (grepped for it, found nothing), so winding order is very likely
  inconsequential there too — but this is inferred from absence, not confirmed positively; worth
  a quick visual check against a real MassivePolyPusher render before trusting it blind.
- **`materialKey` values in use today** (from `TrackBake.cpp`/`TrackMesh.cpp`, and enumerated
  explicitly in `USDExport.cpp`'s `styleFor()`): `"road"`, `"shell"`, `"rail"`, `"mesh-region"`,
  and `"zone-velocityChange"` / `"zone-startGrid"` (`"zone-" + zone.effect`). Treat this as the
  known set, not a closed one — `styleFor()` already has a gray fallback for anything else.
- **`Track::geometry`** (`cpp/core/include/Track.hpp:72`) is `std::vector<GeometryBatch>` — one
  track bakes to N independent batches, exactly analogous to one AssImp scene baking to N
  `aiMesh`es. This maps 1:1 onto `ModelSerializer`'s per-file mesh array; no batch-merging or
  splitting is structurally required (see §4.4 for when you'd want to anyway).

## 4. The conversion

### 4.1 Pick one canonical vertex layout for the whole export

Unlike `model-convert`, which lets the `-s` spec define arbitrary per-project layouts and
`AssImpModelLoader` adapt to them, a `tox`-specific converter should **fix one interleaved
layout** and always emit it, rather than varying it per batch — because (per §2.2) the layout
isn't recorded in the binary; whatever your `.mppmodel` reader is must be told this layout
out-of-band, and a single fixed layout is far easier to hand off/document than "ask the loader to
inspect `hasUv` per mesh." Recommended layout, chosen to cover every field `RenderVertex` carries
and match `cube.modelspec.xml`'s existing precedent:

| Channel | Component | DataType | Bytes | Normalised |
|---|---|---|---|---|
| position | `Position3` | `Float` | 12 | no |
| normal | `Normal3` | `Float` | 12 | no |
| texcoord | `TexCoord2` | `Float` | 8 | no |
| colour | `Colour4` | `UnsignedByte` | 4 | yes |

Total stride: **36 bytes/vertex**. When `hasUv` is false, write `(0, 0)` for texcoord rather than
omitting the channel — keeps every mesh in the file self-consistent under one
`MeshSpecification`, at the cost of 8 wasted bytes/vertex on non-UV batches (mesh-region/rail
geometry, which is a minority of total track vertices). `TexCoord2/Float` (not `HalfFloat`, unlike
`cube.modelspec.xml`) avoids pulling in the vendored `half` conversion for a first pass; revisit
if file size matters. `Colour4/UnsignedByte/normalised` matches `RenderVertex.rgba`'s always-white
default cheaply (`{255,255,255,255}`) and leaves room for real per-vertex colour later without a
format change.

This is exactly the shape `SpecificationParser::parseMeshSpecification` would build from an XML
spec like:

```xml
<Buffers indexed="false" primitive="triangles" storage="static">
  <Buffer>
    <Channel data="position3" type="float32" />
    <Channel data="normal3" type="float32" />
    <Channel data="texcoord2" type="float32" />
    <Channel data="colour4" type="uint8" normalised="true" />
  </Buffer>
</Buffers>
```

— write this file alongside the converter (or embed the equivalent `MeshSpecification` in
whatever `.material` resources you author, per §5) so a human/consuming loader has it in writing.

**This layout is load-bearing on both sides and there is no runtime negotiation of it.** The
binary records a per-stream `vertexStride` but never the attribute meaning (§2.2), and
`ProgrammaticModelStream::createMeshDataStreams()` derives both the vertex count
(`dataSize / spec.getVertexStrideInBytes()`) and every attribute's byte offset from the consumer's
own `MeshSpecification`. A consumer that declares, say, `Colour4/Float` (16 bytes) instead of
`Colour4/UnsignedByte` (4) computes a 48-byte stride against 36-byte-packed data and silently
decodes garbage past the first vertex — no exception, just a mesh that renders as nothing. The two
places that must agree with the table above are
`cpp/tungsten-monoxide/src/Map.cpp`'s `trackMeshSpecification()` and the `TrackProgram`
`<MeshSpecification>` in `cpp/tungsten-monoxide/resources/Resources.xml`. `Map.cpp` additionally
cross-checks the file's own recorded stride against its spec and throws on mismatch, so drift
surfaces as a load error rather than an invisible one.

### 4.2 Vertex buffer: pack directly, no `deinterlaceVertexBufferData` needed

Because `GeometryBatch::vertices` is already an AoS of exactly the fields being emitted (unlike
AssImp's per-component float arrays), packing is a single pass, no attribute-stream bookkeeping
required:

```cpp
std::vector<std::byte> packVertices(const tox::GeometryBatch& batch) {
  std::vector<std::byte> out(batch.vertices.size() * 36);
  std::byte* p = out.data();
  for (const auto& v : batch.vertices) {
    auto putF3 = [&](double x, double y, double z) {
      float f[3] = {(float)x, (float)y, (float)z};
      std::memcpy(p, f, 12); p += 12;
    };
    putF3(v.position.x, v.position.y, v.position.z);
    putF3(v.normal.x, v.normal.y, v.normal.z);
    float uv[2] = {(float)v.uv.x, (float)v.uv.y};             // (0,0) when !batch.hasUv, already true
    std::memcpy(p, uv, 8); p += 8;
    auto normU8 = [](double c) { return (std::uint8_t)std::clamp(std::lround(c * 255.0), 0L, 255L); };
    std::uint8_t rgba[4] = {normU8(v.rgba.r), normU8(v.rgba.g), normU8(v.rgba.b), normU8(v.rgba.a)};
    std::memcpy(p, rgba, 4); p += 4;
  }
  return out;
}
```

Hand the result to `addVertexStream(meshIndex, batch.vertices.size(), 36, sharedPtrOverOut)` —
note `addVertexStream` takes `std::shared_ptr<const int8_t>`, so wrap `out` in a shared buffer
(`std::shared_ptr<int8_t[]>` with an array deleter, mirroring the `[](int8_t* p){ delete[] p; }`
pattern used throughout `AssImpModelLoader.cpp`) rather than handing over a `vector`'s storage
directly.

### 4.3 Index buffer: none — the export is non-indexed

**No index streams are written at all**, and `FLAG_INDEXED_VERTICES` is left clear.

Every `GeometryBatch`'s index array is the identity permutation `indices[k] == k` (§3): the batch
is already a triangle soup, with `vertices.size() == 3 * triangleCount` and no vertex shared
between triangles. An index buffer holding `0,1,2,3,...` is therefore pure redundancy — drawing
the vertex buffer straight through yields byte-identical geometry, at 2 bytes/vertex less on disk
(~5% of a typical track file).

Concretely, in the emitted binary:

- `IndexData` directory entry: `count = 0`, zero-length section. `ModelSerializer::readIndexBuffers()`
  is driven by that count, so it reads nothing and the section's `start == end` check passes.
- Each mesh's `indexStreamId` is `0xFFFFFFFF`, matching `readMesh()`'s own documented
  "index buffer id (or -1 for none)" convention. Nothing may call
  `ModelSerializer::getIndexData()` for such a mesh — it would index an empty stream vector.
- `primitiveCount` is `vertices.size() / 3` rather than `indices.size() / 3` (identical values,
  just no longer routed through indices).

Consumers must declare `setIndexedVertices(false)`; `ProgrammaticModelStream::createMeshDataStreams()`
then derives `primitiveCount` as `vertexCount / 3` instead of from index data.

Older `.mppmodel` files written before this change still load correctly against a non-indexed
consumer: their index buffers are simply ignored, and because those indices were the identity
permutation, the drawn geometry is unchanged.

### 4.4 Per-batch mesh entry

```cpp
void exportTrackToMppModel(const tox::Track& track, const std::string& outFile) {
  mpp::ModelSerializer out;                       // nullptr resourceMgr: writing only
  out.setMeshCount(track.geometry.size());

  for (std::size_t i = 0; i < track.geometry.size(); ++i) {
    const tox::GeometryBatch& batch = track.geometry[i];

    out.setName(i, batch.id);
    out.setMaterial(i, batch.materialKey);         // see §5 -- not resolved to addMaterial() here
    out.setPrimitiveType(i, mpp::mesh::Primitive::Type::Triangles);
    out.setPrimitiveCount(i, batch.vertices.size() / 3);   // non-indexed, see §4.3

    // No setIndexBuffer() call: the export is non-indexed (§4.3).
    auto vertexBytes = packVertices(batch);
    out.addVertexStream(i, batch.vertices.size(), 36, toSharedI8(vertexBytes));
  }

  out.save(outFile);
}
```

One `GeometryBatch` → one mesh entry, one vertex stream, no index stream. No splitting is needed
at any batch size: with no index buffer there is no 16-bit vertex-addressing ceiling to stay under
in the first place (`model-convert`'s `splitSize`/`maxVerticesPerMesh` mesh splitting via
`aiProcess_SplitLargeMeshes` is AssImp-side and has no equivalent needed here).

## 5. Materials — out of scope for the geometry conversion itself, needs a decision

`setMaterial(meshIndex, name)` just stores a string; it does **not** require a corresponding
`addMaterial()` call to have been made (`ModelSerializer.cpp:757-760` — plain field assignment,
no validation). Three options, increasing in effort:

1. **(Recommended for a first pass.)** Write `.mppmodel` files with materials referenced by name
   only (`"road"`, `"shell"`, `"rail"`, `"mesh-region"`, `"zone-<effect>"`) and author matching
   `.material` XML resources by hand once, alongside the modelspec from §4.1, in whatever
   MassivePolyPusher project consumes the export. This mirrors `model-convert`'s own division of
   labour — meshes and materials are already independently-specified concerns there (`-s` vs
   `-m`), and materials are a whole separate resource subsystem
   (`mpp/ResourceStreamSerializer.h`, `MaterialInformation.h`, shaders/textures/uniforms) that has
   nothing to do with `tox::GeometryBatch`'s shape.
2. Synthesize placeholder `ResourceStreamPtr` materials programmatically at export time, one per
   distinct `materialKey` seen, using the same fixed colour palette `USDExport.cpp::styleFor()`
   already defines (`road` teal, `shell` navy, `rail` orange, `mesh-region` purple, `zone-*`
   yellow) — gives an out-of-the-box previewable file without hand-authoring, at the cost of
   needing to build a `ResourceStreamPtr` construction path outside `FileMaterialStream`'s
   normal from-XML-file flow (not yet investigated here).
3. Map `GeometryBatch.texture` (`TextureBinding{assetId, tile}`) through to an actual textured
   material — meaningful only for `"road"` batches with `texture` set (tile atlases authored in
   the editor, see `CLAUDE.md`'s texture-asset conventions); needs a texture-tile → UV-transform
   or texture-array convention on the mpp side that doesn't exist yet. Out of scope until (1)/(2)
   are in place and there's a concrete reason to need it.

## 6. Implementation location

Follows this repo's existing split: `cpp/core` holds baking/runtime, `cpp/editor` holds
tooling/export that walks the baked result (see `cpp/editor/src/USDExport.cpp`, which already
does exactly this kind of `track.geometry` walk for a different target format).
`cpp/editor/include/MppModelExport.hpp` + `cpp/editor/src/MppModelExport.cpp` mirror
`USDExport.hpp`'s shape (`MppModelExportResult { std::string bytes; std::size_t meshCount; }` /
`exportTrackToMppModel(const tox::Track&)`, matching `USDExportResult`/`exportTrackToUSDA`'s
"return the bytes, let the caller do file I/O" pattern), wired into `track_editor`'s toolbar as
"Export MppModel..." right next to the existing "Export USD..." button in `main.cpp`, sharing the
same `showSaveFileDialog`/`std::ofstream` write pattern (binary mode, no text encoding involved).
Added to `EDITOR_SOURCES` in `cpp/editor/CMakeLists.txt`. **No new link dependency** — see the
top-of-file note on why `mpp`/`mpp-mesh` are deliberately not linked.

## 7. Verification

No real `mpp::ModelSerializer::load()` is available to round-trip through (by design — see above),
so verification instead uses a small structural reader (`readMppModelStructurally` in
`cpp/editor/main.cpp`'s "MppModel smoke check", written independently against this spec's
documented layout rather than reusing `MppModelExport.cpp`'s own writer logic) that parses the
emitted bytes back out and cross-checks every field against the source `tox::Track`: header
magic/version/flags, all six directory entries' offsets/counts, and every mesh's
name/material/primitive-type/primitive-count/vertex-count/stride/index-width/data-sizes — plus a
dedicated case forcing a >65535-vertex batch to confirm the 32-bit index-width branch actually
gets taken, not just the 16-bit path every real track batch exercises. Runs at `track_editor`
startup alongside the other milestone/parity smoke checks; all green as of this writing.

## 8. Open questions

- Confirm winding-order/culling assumption in §3 against an actual MassivePolyPusher render
  before shipping, rather than trusting the RenderSystem.cpp grep-for-absence inference. Not
  verifiable without a live MassivePolyPusher build, so still open.
- Decide materials strategy (§5) before authoring a companion MassivePolyPusher project around
  real exported files — it gates whether output files are usable standalone (option 2/3) or need
  hand-authored `.material` resources matching `road`/`shell`/`rail`/`mesh-region`/`zone-<effect>`
  (option 1, what's implemented).
- Vertex welding (deduplicating `GeometryBatch`'s always-unshared triangle vertices) is a valid
  file-size optimization but changes nothing about correctness (§3) — treat as a later pass, not
  a blocker. Not implemented.
- Report the `updateDirectoryEntry` offset bug upstream to `ext/massivepolypusher` -- it corrupts
  every real `.mppmodel` file `mpp::ModelSerializer::save()` produces once more than one directory
  entry is populated (i.e. always, in practice). Not done as part of this change (out of scope --
  `ext/` is vendored).
