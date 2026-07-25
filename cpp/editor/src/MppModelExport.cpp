#include "MppModelExport.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

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

enum class PrimitiveType : std::uint32_t { Points = 0, Lines = 1, Triangles = 2 };

enum class DirectoryEntryType : std::uint32_t { Unused = 0, MaterialNames = 1, Materials = 2, VertexData = 3, IndexData = 4, MeshMetadata = 5, Count = 6 };

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

// Mirrors AssImpModelLoader.cpp's index-packing byte order (lines 370-393): little-endian, 2 or 4
// bytes per index depending on whether 16-bit indices can address every vertex.
std::pair<std::string, std::uint32_t> packIndices(const tox::GeometryBatch& batch) {
  const bool wide = batch.vertices.size() > 65535;
  const std::uint32_t indexWidthBits = wide ? 32 : 16;
  std::string out;
  out.reserve(batch.indices.size() * (indexWidthBits / 8));
  for (std::uint32_t idx : batch.indices) {
    out.push_back(static_cast<char>(idx & 0xff));
    out.push_back(static_cast<char>((idx >> 8) & 0xff));
    if (wide) {
      out.push_back(static_cast<char>((idx >> 16) & 0xff));
      out.push_back(static_cast<char>((idx >> 24) & 0xff));
    }
  }
  return {out, indexWidthBits};
}

}  // namespace

MppModelExportResult exportTrackToMppModel(const tox::Track& track) {
  const std::size_t meshCount = track.geometry.size();

  // Build every section's content in memory first, so every directory offset/count is known
  // before the header/directory are written -- no seek-and-backpatch needed at all (see
  // MppModelExport.hpp's header comment on why the upstream backpatch approach is unsafe to
  // imitate).
  std::vector<std::string> packedVertices(meshCount), packedIndices(meshCount);
  std::vector<std::uint32_t> indexWidths(meshCount);
  for (std::size_t i = 0; i < meshCount; ++i) {
    packedVertices[i] = packVertices(track.geometry[i]);
    auto [indexBytes, indexWidthBits] = packIndices(track.geometry[i]);
    packedIndices[i] = std::move(indexBytes);
    indexWidths[i] = indexWidthBits;
  }

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

  std::string indexDataSection;
  for (std::size_t i = 0; i < meshCount; ++i) {
    // Mirrors writeIndexBuffer(): u32 dataSizeBytes, u32 indexWidthBits, raw bytes.
    appendU32(indexDataSection, static_cast<std::uint32_t>(packedIndices[i].size()));
    appendU32(indexDataSection, indexWidths[i]);
    appendBytes(indexDataSection, packedIndices[i].data(), packedIndices[i].size());
  }

  std::string meshMetadataSection;
  for (std::size_t i = 0; i < meshCount; ++i) {
    const tox::GeometryBatch& batch = track.geometry[i];
    // Mirrors writeMesh(): str name, u32 primitiveType, u32 primitiveCount, str material,
    // u32 numVertexBuffers, vertexBufferId[numVertexBuffers], u32 indexStreamId. One vertex
    // stream and one index stream per mesh, added in mesh order, so stream id == mesh index.
    appendString(meshMetadataSection, batch.id);
    appendU32(meshMetadataSection, static_cast<std::uint32_t>(PrimitiveType::Triangles));
    appendU32(meshMetadataSection, static_cast<std::uint32_t>(batch.indices.size() / 3));
    appendString(meshMetadataSection, batch.materialKey);
    appendU32(meshMetadataSection, 1);  // numVertexBuffers
    appendU32(meshMetadataSection, static_cast<std::uint32_t>(i));
    appendU32(meshMetadataSection, static_cast<std::uint32_t>(i));  // indexStreamId
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
  const DirectoryEntry indexDataEntry = makeEntry(DirectoryEntryType::IndexData, indexDataSection, meshCount);
  const DirectoryEntry meshMetadataEntry = makeEntry(DirectoryEntryType::MeshMetadata, meshMetadataSection, meshCount);

  std::string file;
  file.reserve(cursor);

  // Header: mirrors writeHeader() -- magic 'MPPM', u16 versionMajor=1, u16 versionMinor=1,
  // u32 flags (FLAG_INDEXED_VERTICES, 0x0001, unconditionally set).
  file.append("MPPM", 4);
  appendU16(file, 1);
  appendU16(file, 1);
  appendU32(file, 0x0001);

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

}  // namespace editor
