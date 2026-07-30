#include "MppModelExport.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

namespace editor {
namespace {

// Raw native-endian appends, matching ModelSerializer.cpp's writeValue()s exactly (a plain
// `fp.write((char const*)&value, sizeof(value))` -- both projects target the same little-endian
// x64 Windows platform, so no explicit byte-swapping is needed or attempted here).
void appendU16(std::string& out, std::uint16_t value) { out.append(reinterpret_cast<const char*>(&value), sizeof(value)); }
void appendU32(std::string& out, std::uint32_t value) { out.append(reinterpret_cast<const char*>(&value), sizeof(value)); }
void appendF32(std::string& out, float value) { out.append(reinterpret_cast<const char*>(&value), sizeof(value)); }
void appendBytes(std::string& out, const void* data, std::size_t count) {
  if (count > 0) out.append(reinterpret_cast<const char*>(data), count);
}
// Mirrors writeValue(string const&, ofstream&): u32 length prefix, no null terminator.
void appendString(std::string& out, const std::string& value) {
  appendU32(out, static_cast<std::uint32_t>(value.size()));
  appendBytes(out, value.data(), value.size());
}

enum class PrimitiveType : std::uint32_t { Points = 0,
                                           Lines = 1,
                                           Triangles = 2 };

enum class DirectoryEntryType : std::uint32_t { Unused = 0,
                                                MaterialNames = 1,
                                                Materials = 2,
                                                VertexData = 3,
                                                IndexData = 4,
                                                MeshMetadata = 5,
                                                Count = 6 };

struct DirectoryEntry {
  DirectoryEntryType type{DirectoryEntryType::Unused};
  std::uint32_t startOffset{0}, endOffset{0}, count{0};
};

// Mirrors writeDirectoryEntry(): always exactly 16 bytes (4x uint32_t), regardless of the
// upstream C++ struct's in-memory size (see MppModelExport.hpp's header comment).
void appendDirectoryEntry(std::string& out, const DirectoryEntry& entry) {
  appendU32(out, static_cast<std::uint32_t>(entry.type));
  appendU32(out, entry.startOffset);
  appendU32(out, entry.endOffset);
  appendU32(out, entry.count);
}

// The recommended fixed vertex layout from MPPMODEL_EXPORT_SPEC.md 4.1: Position3(float32) +
// Normal3(float32) + TexCoord2(float32) + Colour4(unorm8), 36 bytes/vertex. Chosen so every mesh
// in the file shares one interleaved layout regardless of batch.hasUv, since the .mppmodel binary
// format itself carries no per-mesh attribute-layout metadata (spec 2.2) -- whatever reads this
// file needs to be told this layout out-of-band (a MeshSpecification/modelspec.xml matching it).
constexpr std::size_t kVertexStrideBytes = 36;

std::uint8_t normalizedByte(double c) { return static_cast<std::uint8_t>(std::clamp(std::lround(c * 255.0), 0L, 255L)); }

std::string packVertices(const tox::GeometryBatch& batch) {
  std::string out;
  out.reserve(batch.vertices.size() * kVertexStrideBytes);
  for (const auto& v : batch.vertices) {
    appendF32(out, static_cast<float>(v.position.x));
    appendF32(out, static_cast<float>(v.position.y));
    appendF32(out, static_cast<float>(v.position.z));
    appendF32(out, static_cast<float>(v.normal.x));
    appendF32(out, static_cast<float>(v.normal.y));
    appendF32(out, static_cast<float>(v.normal.z));
    // uv is {0,0} whenever !batch.hasUv (TrackBake.cpp/TrackMesh.cpp never set it otherwise),
    // which is exactly the value we want written for a layout that always reserves the channel.
    appendF32(out, static_cast<float>(v.uv.x));
    appendF32(out, static_cast<float>(v.uv.y));
    const std::uint8_t rgba[4] = {normalizedByte(v.rgba.r), normalizedByte(v.rgba.g), normalizedByte(v.rgba.b), normalizedByte(v.rgba.a)};
    appendBytes(out, rgba, 4);
  }
  return out;
}

// Sentinel written as each mesh's indexStreamId, matching ModelSerializer::readMesh()'s own
// documented "4 bytes: index buffer id (or -1 for none)" convention -- this export writes no index
// streams at all (see exportTrackToMppModel), so there is no valid id to point at.
constexpr std::uint32_t kNoIndexStream = 0xFFFFFFFFu;

// See MppModelExport.hpp's comment on `trackMaterialToMaterial`: a materialKey with no entry
// (the fixed rail/shell/zone/trigger materials, or an empty/legacy "road" literal) passes
// through unchanged -- those already name real Materials directly.
std::string resolveMaterialKey(const std::string& materialKey, const std::map<std::string, std::string>& trackMaterialToMaterial) {
  const auto it = trackMaterialToMaterial.find(materialKey);
  return it == trackMaterialToMaterial.end() ? materialKey : it->second;
}

}  // namespace

MppModelExportResult exportTrackToMppModel(const tox::Track& track, const std::map<std::string, std::string>& trackMaterialToMaterial) {
  const std::size_t meshCount = track.geometry.size();

  // Build every section's content in memory first, so every directory offset/count is known
  // before the header/directory are written -- no seek-and-backpatch needed at all (see
  // MppModelExport.hpp's header comment on why the upstream backpatch approach is unsafe to
  // imitate).
  std::vector<std::string> packedVertices(meshCount);
  for (std::size_t i = 0; i < meshCount; ++i) packedVertices[i] = packVertices(track.geometry[i]);

  // MaterialNames / Materials sections: deliberately empty (MPPMODEL_EXPORT_SPEC.md 5, option 1
  // -- materials are referenced by name only, authored separately in the target project).
  const std::string materialNamesSection;
  const std::string materialsSection;

  std::string vertexDataSection;
  for (std::size_t i = 0; i < meshCount; ++i) {
    // Mirrors writeVertexBuffer(): u32 dataSizeBytes, u32 vertexCount, u32 vertexStride, raw bytes.
    appendU32(vertexDataSection, static_cast<std::uint32_t>(packedVertices[i].size()));
    appendU32(vertexDataSection, static_cast<std::uint32_t>(track.geometry[i].vertices.size()));
    appendU32(vertexDataSection, static_cast<std::uint32_t>(kVertexStrideBytes));
    appendBytes(vertexDataSection, packedVertices[i].data(), packedVertices[i].size());
  }

  // IndexData section: deliberately empty. Every tox geometry batch is already a triangle soup --
  // TrackBake.cpp's Builder::tri() and TrackMesh.cpp's addTriangle() give every triangle three
  // brand-new vertices, never sharing any, so `indices[k] == k` for every batch (the identity
  // permutation) and vertices.size() == 3 * triangleCount. An index buffer holding 0,1,2,3,... is
  // pure redundancy: drawing the vertex buffer straight through produces byte-identical geometry.
  // cpp/tungsten-monoxide's Map.cpp declares the matching non-indexed MeshSpecification and
  // derives primitiveCount as vertexCount / 3 rather than from indices.
  const std::string indexDataSection;

  std::string meshMetadataSection;
  for (std::size_t i = 0; i < meshCount; ++i) {
    const tox::GeometryBatch& batch = track.geometry[i];
    // Mirrors writeMesh(): str name, u32 primitiveType, u32 primitiveCount, str material,
    // u32 numVertexBuffers, vertexBufferId[numVertexBuffers], u32 indexStreamId. One vertex
    // stream per mesh, added in mesh order, so vertex stream id == mesh index; no index stream.
    appendString(meshMetadataSection, batch.id);
    appendU32(meshMetadataSection, static_cast<std::uint32_t>(PrimitiveType::Triangles));
    // Non-indexed, so primitiveCount comes from the vertex count. Identical to the old
    // indices.size() / 3 (see the IndexData comment above), just no longer routed via indices.
    appendU32(meshMetadataSection, static_cast<std::uint32_t>(batch.vertices.size() / 3));
    appendString(meshMetadataSection, resolveMaterialKey(batch.materialKey, trackMaterialToMaterial));
    appendU32(meshMetadataSection, 1);  // numVertexBuffers
    appendU32(meshMetadataSection, static_cast<std::uint32_t>(i));
    appendU32(meshMetadataSection, kNoIndexStream);  // indexStreamId: none
  }

  // Header (12 bytes) + directory (6 x 16 bytes = 96 bytes) = 108 bytes before section 0 starts.
  constexpr std::uint32_t kHeaderBytes = 12;
  constexpr std::uint32_t kDirectoryBytes = static_cast<std::uint32_t>(DirectoryEntryType::Count) * 16;
  std::uint32_t cursor = kHeaderBytes + kDirectoryBytes;

  auto makeEntry = [&](DirectoryEntryType type, const std::string& section, std::size_t count) {
    const std::uint32_t start = cursor;
    cursor += static_cast<std::uint32_t>(section.size());
    return DirectoryEntry{type, start, cursor, static_cast<std::uint32_t>(count)};
  };

  const DirectoryEntry unusedEntry{DirectoryEntryType::Unused, 0, 0, 0};
  const DirectoryEntry materialNamesEntry = makeEntry(DirectoryEntryType::MaterialNames, materialNamesSection, 0);
  const DirectoryEntry materialsEntry = makeEntry(DirectoryEntryType::Materials, materialsSection, 0);
  const DirectoryEntry vertexDataEntry = makeEntry(DirectoryEntryType::VertexData, vertexDataSection, meshCount);
  const DirectoryEntry indexDataEntry = makeEntry(DirectoryEntryType::IndexData, indexDataSection, 0);
  const DirectoryEntry meshMetadataEntry = makeEntry(DirectoryEntryType::MeshMetadata, meshMetadataSection, meshCount);

  std::string file;
  file.reserve(cursor);

  // Header: mirrors writeHeader() -- magic 'MPPM', u16 versionMajor=1, u16 versionMinor=1, u32
  // flags. FLAG_INDEXED_VERTICES (0x0001) is deliberately CLEAR: this export writes no index
  // streams (see the IndexData comment above). Upstream's own writeHeader() sets it
  // unconditionally, but nothing in ModelSerializer::load() reads the flag back -- readIndexBuffers
  // is driven purely by the IndexData directory entry's count, which is 0 here -- so a cleared
  // flag costs no compatibility and honestly describes the file.
  file.append("MPPM", 4);
  appendU16(file, 1);
  appendU16(file, 1);
  appendU32(file, 0x0000);

  // Directory: mirrors writeDirectory()'s entry order (Unused, MaterialNames, Materials,
  // VertexData, IndexData, MeshMetadata) -- written with final values directly, no placeholder
  // pass or backpatch.
  appendDirectoryEntry(file, unusedEntry);
  appendDirectoryEntry(file, materialNamesEntry);
  appendDirectoryEntry(file, materialsEntry);
  appendDirectoryEntry(file, vertexDataEntry);
  appendDirectoryEntry(file, indexDataEntry);
  appendDirectoryEntry(file, meshMetadataEntry);

  file += materialNamesSection;
  file += materialsSection;
  file += vertexDataSection;
  file += indexDataSection;
  file += meshMetadataSection;

  return {std::move(file), meshCount};
}

namespace {

// The materials TrackBake.cpp/TrackMesh.cpp always assign to rail/mesh-region/shell/zone/trigger
// geometry, regardless of what any path is assigned -- see MppModelExport.hpp's comment.
constexpr char kDefaultRailMaterial[] = "Tracks/DefaultRailMaterial";
constexpr char kDefaultMeshMaterial[] = "Tracks/DefaultMeshMaterial";
constexpr char kDefaultShellMaterial[] = "Tracks/DefaultShellMaterial";
constexpr char kDefaultZoneMaterial[] = "Tracks/DefaultZoneMaterial";
constexpr char kDefaultTriggerMaterial[] = "Tracks/DefaultTriggerMaterial";

std::string xmlEscape(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (char c : value) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c; break;
    }
  }
  return out;
}

}  // namespace

std::string buildTrackResourceXmlForName(const TrackDefinition& track, const tox::Track& bakedTrack,
                                         const std::string& resourceName, const std::string& mppModelFileName,
                                         const std::string& trackDataFileName,
                                         const std::map<std::string, std::string>& trackMaterialToMaterial) {
  // Every distinct material this track's curves are actually assigned to, in first-seen order,
  // plus the fixed rail/mesh/shell/zone/trigger materials every export depends on regardless of
  // curve content. Resolved through trackMaterialToMaterial first (see MppModelExport.hpp's
  // comment) so this dependency list always matches what the exported mesh's own material
  // reference resolves to -- two different TrackMaterials that happen to wrap the same underlying
  // Material collapse to one dependency here, which `seen` already handles.
  std::vector<std::string> materials;
  std::set<std::string> seen;
  for (const auto& path : track.paths) {
    if (path.material.empty()) continue;
    const std::string resolved = resolveMaterialKey(path.material, trackMaterialToMaterial);
    if (!seen.insert(resolved).second) continue;
    materials.push_back(resolved);
  }
  for (const char* fixed :
       {kDefaultRailMaterial, kDefaultMeshMaterial, kDefaultShellMaterial, kDefaultZoneMaterial, kDefaultTriggerMaterial}) {
    if (seen.insert(fixed).second) materials.push_back(fixed);
  }

  std::string xml = "<?xml version=\"1.0\"?>\n<Resources>\n\t<Namespace name=\"Tracks\">\n";
  // No `location=` attribute: Track is always composite (it lists TrackMaterial dependents below),
  // and ResourceManager::instantiateResource() unconditionally discards a composite resource's own
  // `location`/source. The .mppmodel filename instead travels via <Definition><File> below, which
  // MapTungstenMonoxideDefinitionFactory::create() reads into Map::mModelFileName.
  xml += "\t\t<Resource type=\"Track\" name=\"" + xmlEscape(resourceName) + "\">\n";

  if (!materials.empty()) {
    xml += "\t\t\t<DependentResources>\n";
    // id == ref (the qualified name) -- Map::load() (cpp/tungsten-monoxide/src/Map.cpp) resolves
    // each mesh's material by calling getDependentResource() with the exact "Tracks/..." string
    // baked into the mesh's GeometryBatch.materialKey, so the id must match that verbatim. `seen`
    // above already dedupes by qualified name, so no id collision is possible here.
    for (const std::string& qualifiedName : materials) {
      xml += "\t\t\t\t<DependentResource id=\"" + xmlEscape(qualifiedName) + "\" ref=\"" + xmlEscape(qualifiedName) + "\" />\n";
    }
    xml += "\t\t\t</DependentResources>\n";
  }

  // <File> carries the .mppmodel filename (see the no-`location=` comment above). The
  // <Definitions> block itself is also mandatory even apart from that: a resource with none at all
  // still gets an implicit factory="" one synthesized by ResourceLocation::scanResourceElement(),
  // and no (Map, "") definition factory is registered -- only (Map, "Track") is (see
  // cpp/tungsten-monoxide/src/DLL.cpp). Omitting it throws "could not find a definition factory".
  xml += "\t\t\t<Definitions>\n\t\t\t\t<Definition factory=\"Track\">\n";
  xml += "\t\t\t\t\t<ModelFile>" + xmlEscape(mppModelFileName) + "</ModelFile>\n";
  xml += "\t\t\t\t\t<TrackData>" + xmlEscape(trackDataFileName) + "</TrackData>\n";
  xml += "\t\t\t\t\t<TrackMeshes>\n";
  for (const tox::GeometryBatch& batch : bakedTrack.geometry) {
    if (batch.kind != tox::GeometryKind::PathSurface && batch.kind != tox::GeometryKind::MeshSurface) continue;
    xml += "\t\t\t\t\t\t<Mesh>" + xmlEscape(batch.id) + "</Mesh>\n";
  }
  xml += "\t\t\t\t\t</TrackMeshes>\n";
  xml += "\t\t\t\t</Definition>\n\t\t\t</Definitions>\n";

  xml += "\t\t</Resource>\n\t</Namespace>\n</Resources>\n";
  return xml;
}

std::string buildTrackResourceXml(const TrackDefinition& track, const tox::Track& bakedTrack,
                                  const std::string& mppModelFileName, const std::string& trackDataFileName,
                                  const std::map<std::string, std::string>& trackMaterialToMaterial) {
  return buildTrackResourceXmlForName(track, bakedTrack, track.name, mppModelFileName, trackDataFileName,
                                      trackMaterialToMaterial);
}

}  // namespace editor
