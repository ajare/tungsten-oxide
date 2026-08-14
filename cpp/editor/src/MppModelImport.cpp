#include "MppModelImport.hpp"

#include <cstring>
#include <fstream>
#include <stdexcept>

#include "modelio/MeshLayout.hpp"
#include "modelio/MppModelIo.hpp"

namespace editor {
namespace {

// Peeks the on-disk IndexData directory entry's own stream count, without touching
// mpp::ModelSerializer at all -- see MppModelImport.hpp's header comment on why that's necessary
// (getIndexData()/getIndexWidth() are unsafe to call speculatively). Mirrors
// mpp::ModelSerializer::Directory's fixed entry layout and order (Unused, MaterialNames, Materials,
// VertexData, IndexData, MeshMetadata), each entry a flat 4x uint32_t (type, start, end, count).
bool mppModelIsIndexed(const std::filesystem::path& path) {
  std::ifstream fp(path, std::ios::binary);
  if (!fp) throw std::runtime_error("Could not open '" + path.string() + "'.");

  char magic[4] = {};
  fp.read(magic, 4);
  if (magic[0] != 'M' || magic[1] != 'P' || magic[2] != 'P' || magic[3] != 'M')
    throw std::runtime_error("'" + path.string() + "' is not a valid .mppmodel file (bad magic).");
  fp.seekg(8, std::ios::cur);  // skip u16 versionMajor + u16 versionMinor + u32 flags (12-byte
                               // header total, 4 bytes of it already consumed by the magic read).

  for (int entryIndex = 0; entryIndex < 5; ++entryIndex) {
    std::uint32_t type = 0, start = 0, end = 0, count = 0;
    fp.read(reinterpret_cast<char*>(&type), 4);
    fp.read(reinterpret_cast<char*>(&start), 4);
    fp.read(reinterpret_cast<char*>(&end), 4);
    fp.read(reinterpret_cast<char*>(&count), 4);
    if (!fp) throw std::runtime_error("'" + path.string() + "' is not a valid .mppmodel file (truncated directory).");
    if (type == 4) return count > 0;  // Directory::Entry::Type::IndexData
  }
  throw std::runtime_error("'" + path.string() + "' is not a valid .mppmodel file (missing IndexData directory entry).");
}

float readPackedF32(const std::int8_t* cursor) {
  float value = 0.0f;
  std::memcpy(&value, cursor, sizeof(value));
  return value;
}

// position3/normal3/texcoord2 sit at fixed offsets 0/12/24 in both layouts this reader accepts
// (modelio::LegacyPbrVertexStride's 36-byte legacy form and modelio::PbrVertexStride's 52-byte PBR
// form, which just appends tangent4) -- so no branching on which one a given mesh uses.
std::vector<MppModelVertex> unpackVertices(const modelio::ReadMesh& mesh, const std::filesystem::path& path) {
  if (mesh.vertexStride != modelio::LegacyPbrVertexStride && mesh.vertexStride != modelio::PbrVertexStride)
    throw std::runtime_error("'" + path.string() + "': mesh '" + mesh.name + "' uses a " +
                             std::to_string(mesh.vertexStride) +
                             "-byte vertex layout; this reader only supports the " +
                             std::to_string(modelio::LegacyPbrVertexStride) + "-byte legacy or " +
                             std::to_string(modelio::PbrVertexStride) + "-byte PBR layout.");
  std::vector<MppModelVertex> vertices(mesh.vertexCount);
  for (std::size_t i = 0; i < mesh.vertexCount; ++i) {
    const std::int8_t* cursor = mesh.vertexBytes.data() + i * mesh.vertexStride;
    MppModelVertex v;
    v.position.x = readPackedF32(cursor + 0);
    v.position.y = readPackedF32(cursor + 4);
    v.position.z = readPackedF32(cursor + 8);
    v.normal.x = readPackedF32(cursor + 12);
    v.normal.y = readPackedF32(cursor + 16);
    v.normal.z = readPackedF32(cursor + 20);
    v.u = readPackedF32(cursor + 24);
    v.v = readPackedF32(cursor + 28);
    vertices[i] = v;
  }
  return vertices;
}

}  // namespace

ImportedMppModel readMppModelGeometry(const std::filesystem::path& path) {
  const bool indexed = mppModelIsIndexed(path);
  const modelio::ReadModel raw = modelio::readMppModel(path, indexed);

  ImportedMppModel model;
  model.meshes.reserve(raw.meshes.size());
  for (const modelio::ReadMesh& mesh : raw.meshes) {
    ImportedMppMesh imported;
    imported.name = mesh.name;
    imported.material = mesh.material;
    imported.vertices = unpackVertices(mesh, path);
    if (indexed) {
      imported.indices = mesh.indices;
    } else {
      // No index stream -- matches MppModelExport.cpp's own non-indexed convention: the vertex
      // buffer is already a triangle soup, indices[k] == k.
      imported.indices.resize(mesh.vertexCount);
      for (std::size_t i = 0; i < imported.indices.size(); ++i) imported.indices[i] = static_cast<std::uint32_t>(i);
    }
    model.meshes.push_back(std::move(imported));
  }
  return model;
}

}  // namespace editor
