#include "MppModelImport.hpp"

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace editor {
namespace {

std::uint16_t readU16(std::ifstream& fp) {
  std::uint16_t value = 0;
  fp.read(reinterpret_cast<char*>(&value), sizeof(value));
  return value;
}

std::uint32_t readU32(std::ifstream& fp) {
  std::uint32_t value = 0;
  fp.read(reinterpret_cast<char*>(&value), sizeof(value));
  return value;
}

// Mirrors ModelSerializer::readString(): u32 length prefix, no null terminator on disk.
std::string readString(std::ifstream& fp) {
  const std::uint32_t len = readU32(fp);
  if (len == 0) return {};
  std::string value(len, '\0');
  fp.read(value.data(), len);
  return value;
}

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

DirectoryEntry readDirectoryEntry(std::ifstream& fp) {
  DirectoryEntry entry;
  entry.type = static_cast<DirectoryEntryType>(readU32(fp));
  entry.startOffset = readU32(fp);
  entry.endOffset = readU32(fp);
  entry.count = readU32(fp);
  return entry;
}

struct RawVertexStream {
  std::uint32_t vertexCount{0}, vertexStride{0};
  std::string data;  // raw bytes, vertexCount * vertexStride
};

RawVertexStream readVertexBuffer(std::ifstream& fp) {
  RawVertexStream vs;
  const std::uint32_t dataSize = readU32(fp);
  vs.vertexCount = readU32(fp);
  vs.vertexStride = readU32(fp);
  vs.data.resize(dataSize);
  if (dataSize > 0) fp.read(vs.data.data(), dataSize);
  return vs;
}

struct RawIndexStream {
  std::uint32_t indexWidth{0};  // bits per index -- 16 or 32
  std::string data;
};

RawIndexStream readIndexBuffer(std::ifstream& fp) {
  RawIndexStream is;
  const std::uint32_t dataSize = readU32(fp);
  is.indexWidth = readU32(fp);
  is.data.resize(dataSize);
  if (dataSize > 0) fp.read(is.data.data(), dataSize);
  return is;
}

enum class PrimitiveType : std::uint32_t { Points = 0,
                                           Lines = 1,
                                           Triangles = 2 };

struct RawMesh {
  std::string name;
  PrimitiveType primitiveType{PrimitiveType::Triangles};
  std::uint32_t primitiveCount{0};
  std::string material;
  std::vector<std::uint32_t> vertexStreamIds;
  std::uint32_t indexStreamId{0};
};

constexpr std::uint32_t kNoIndexStream = 0xFFFFFFFFu;
constexpr std::size_t kExpectedVertexStrideBytes = 36;  // Position3 + Normal3 + TexCoord2 + Colour4(unorm8)

RawMesh readMesh(std::ifstream& fp) {
  RawMesh mesh;
  mesh.name = readString(fp);
  mesh.primitiveType = static_cast<PrimitiveType>(readU32(fp));
  mesh.primitiveCount = readU32(fp);
  mesh.material = readString(fp);
  const std::uint32_t numVertexBuffers = readU32(fp);
  mesh.vertexStreamIds.reserve(numVertexBuffers);
  for (std::uint32_t i = 0; i < numVertexBuffers; ++i) mesh.vertexStreamIds.push_back(readU32(fp));
  mesh.indexStreamId = readU32(fp);
  return mesh;
}

// The on-disk layout is 8 packed float32 fields (position xyz, normal xyz, uv) + 4 unorm8 bytes,
// but tox::Vec3 is glm::dvec3 (double) -- see Vec3.hpp -- so every float is read into a temporary
// and widened, never memcpy'd directly into a Vec3's 8-byte double components.
float readPackedF32(const char* cursor) {
  float value = 0.0f;
  std::memcpy(&value, cursor, sizeof(value));
  return value;
}

std::vector<MppModelVertex> unpackVertices(const RawVertexStream& stream) {
  if (stream.vertexStride != kExpectedVertexStrideBytes)
    throw std::runtime_error("mesh uses a " + std::to_string(stream.vertexStride) +
                             "-byte vertex layout; this reader only supports the fixed 36-byte layout.");
  std::vector<MppModelVertex> vertices(stream.vertexCount);
  const char* cursor = stream.data.data();
  for (std::uint32_t i = 0; i < stream.vertexCount; ++i) {
    MppModelVertex v;
    v.position.x = readPackedF32(cursor + 0);
    v.position.y = readPackedF32(cursor + 4);
    v.position.z = readPackedF32(cursor + 8);
    v.normal.x = readPackedF32(cursor + 12);
    v.normal.y = readPackedF32(cursor + 16);
    v.normal.z = readPackedF32(cursor + 20);
    v.u = readPackedF32(cursor + 24);
    v.v = readPackedF32(cursor + 28);
    // Bytes 32..35 (packed unorm8 RGBA colour) are deliberately not unpacked -- ImportedMppMesh has
    // no colour field, since nothing this reader currently feeds (viewport rendering) uses vertex
    // colour, unlike UV/position/normal.
    vertices[i] = v;
    cursor += kExpectedVertexStrideBytes;
  }
  return vertices;
}

std::vector<std::uint32_t> unpackIndices(const RawIndexStream& stream) {
  std::vector<std::uint32_t> indices;
  if (stream.indexWidth == 16) {
    indices.resize(stream.data.size() / 2);
    for (std::size_t i = 0; i < indices.size(); ++i) {
      std::uint16_t value = 0;
      std::memcpy(&value, stream.data.data() + i * 2, 2);
      indices[i] = value;
    }
  } else if (stream.indexWidth == 32) {
    indices.resize(stream.data.size() / 4);
    for (std::size_t i = 0; i < indices.size(); ++i) std::memcpy(&indices[i], stream.data.data() + i * 4, 4);
  } else {
    throw std::runtime_error("index stream uses an unsupported " + std::to_string(stream.indexWidth) + "-bit width.");
  }
  return indices;
}

}  // namespace

ImportedMppModel readMppModelGeometry(const std::filesystem::path& path) {
  std::ifstream fp(path, std::ios::binary);
  if (!fp) throw std::runtime_error("Could not open '" + path.string() + "'.");

  char magic[4] = {};
  fp.read(magic, 4);
  if (magic[0] != 'M' || magic[1] != 'P' || magic[2] != 'P' || magic[3] != 'M')
    throw std::runtime_error("'" + path.string() + "' is not a valid .mppmodel file (bad magic).");
  readU16(fp);  // version major -- not gated on; this reader tracks the on-disk format, not a
                // version number ModelSerializer itself never bumps meaningfully (both writers in
                // this codebase always emit 1.1).
  readU16(fp);  // version minor
  readU32(fp);  // flags -- FLAG_INDEXED_VERTICES is informational only; readIndexBuffers below is
                // driven purely by each directory entry's own count, matching the real reader.

  std::vector<DirectoryEntry> entries(static_cast<std::size_t>(DirectoryEntryType::Count));
  for (auto& entry : entries) entry = readDirectoryEntry(fp);
  const auto& vertexDataEntry = entries[static_cast<std::size_t>(DirectoryEntryType::VertexData)];
  const auto& indexDataEntry = entries[static_cast<std::size_t>(DirectoryEntryType::IndexData)];
  const auto& meshMetadataEntry = entries[static_cast<std::size_t>(DirectoryEntryType::MeshMetadata)];

  // MaterialNames/Materials are deliberately never read (directory-skipped) -- see this file's
  // header comment.

  fp.seekg(vertexDataEntry.startOffset);
  std::vector<RawVertexStream> vertexStreams(vertexDataEntry.count);
  for (auto& stream : vertexStreams) stream = readVertexBuffer(fp);
  if (static_cast<std::uint32_t>(fp.tellg()) != vertexDataEntry.endOffset)
    throw std::runtime_error("'" + path.string() + "': VertexData section did not end where its directory entry said it would.");

  fp.seekg(indexDataEntry.startOffset);
  std::vector<RawIndexStream> indexStreams(indexDataEntry.count);
  for (auto& stream : indexStreams) stream = readIndexBuffer(fp);
  if (static_cast<std::uint32_t>(fp.tellg()) != indexDataEntry.endOffset)
    throw std::runtime_error("'" + path.string() + "': IndexData section did not end where its directory entry said it would.");

  fp.seekg(meshMetadataEntry.startOffset);
  std::vector<RawMesh> rawMeshes(meshMetadataEntry.count);
  for (auto& mesh : rawMeshes) mesh = readMesh(fp);
  if (static_cast<std::uint32_t>(fp.tellg()) != meshMetadataEntry.endOffset)
    throw std::runtime_error("'" + path.string() + "': MeshMetadata section did not end where its directory entry said it would.");

  ImportedMppModel model;
  model.meshes.reserve(rawMeshes.size());
  for (const RawMesh& raw : rawMeshes) {
    if (raw.vertexStreamIds.size() != 1)
      throw std::runtime_error("mesh '" + raw.name + "' has " + std::to_string(raw.vertexStreamIds.size()) +
                               " vertex streams; this reader only supports exactly one per mesh.");
    ImportedMppMesh mesh;
    mesh.name = raw.name;
    mesh.material = raw.material;
    mesh.vertices = unpackVertices(vertexStreams.at(raw.vertexStreamIds.front()));
    if (raw.indexStreamId == kNoIndexStream) {
      // No index stream -- matches MppModelExport.cpp's own non-indexed convention: the vertex
      // buffer is already a triangle soup, indices[k] == k.
      mesh.indices.resize(mesh.vertices.size());
      for (std::size_t i = 0; i < mesh.indices.size(); ++i) mesh.indices[i] = static_cast<std::uint32_t>(i);
    } else {
      mesh.indices = unpackIndices(indexStreams.at(raw.indexStreamId));
    }
    model.meshes.push_back(std::move(mesh));
  }
  return model;
}

}  // namespace editor
