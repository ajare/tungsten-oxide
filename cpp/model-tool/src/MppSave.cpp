#include "MppSave.hpp"

#include <cstring>
#include <memory>

#include <mpp/ModelSerializer.h>
#include <mpp/mesh/Primitive.h>

namespace modeltool {
namespace {

// ModelSerializer::addVertexStream()/setIndexBuffer() take a shared_ptr to a *single* (const)
// int8_t/uint8_t, not shared_ptr<T[]> -- std::make_shared<T[]>(n) doesn't implicitly convert to
// that (array-to-non-array shared_ptr conversion isn't valid), so a custom-deleter shared_ptr<T>
// over a new[] buffer is required instead (matches MPPMODEL_EXPORT_SPEC.md 4.2's own noted
// pattern for the same API, mirroring AssImpModelLoader.cpp's own `[](int8_t* p){ delete[] p; }`).
template <typename T>
std::shared_ptr<T> makeArraySharedPtr(std::size_t count) {
  return std::shared_ptr<T>(new T[count], std::default_delete<T[]>());
}

// Little-endian packed indices, matching cpp/editor's MppModelExport.cpp / ModelConvert's own
// convention (16-bit unless the mesh needs 32).
std::shared_ptr<std::uint8_t> packIndices(const std::vector<std::uint32_t>& indices, std::size_t indexWidthBits) {
  const std::size_t bytesPerIndex = indexWidthBits / 8;
  std::shared_ptr<std::uint8_t> buffer = makeArraySharedPtr<std::uint8_t>(indices.size() * bytesPerIndex);
  std::uint8_t* p = buffer.get();
  for (std::uint32_t index : indices) {
    if (bytesPerIndex == 2) {
      const std::uint16_t narrow = static_cast<std::uint16_t>(index);
      std::memcpy(p, &narrow, 2);
    } else {
      std::memcpy(p, &index, 4);
    }
    p += bytesPerIndex;
  }
  return buffer;
}

}  // namespace

bool saveModelAsMppModel(const BuiltModel& built, MaterialLibrary& materialLibrary, const std::string& utf8Path, std::string* outError) {
  try {
    mpp::ModelSerializer serializer;

    // Materials are referenced by name only -- MaterialNames/Materials stay empty (see this
    // header's top comment) -- but every non-fallback name is still checked against
    // materialLibrary here, so saving fails loudly rather than silently producing a file whose
    // mesh.material fields reference something that's no longer loaded (and so the companion XML,
    // built from this same ImportedModel by main.cpp, is guaranteed to describe something real).
    for (const ImportedMaterial& material : built.source.materials) {
      if (material.origin == MaterialOrigin::DefaultFallback) continue;
      if (!materialLibrary.materials().count(material.name)) {
        if (outError) *outError = "Material '" + material.name + "' is no longer loaded.";
        return false;
      }
    }

    const std::size_t meshCount = built.source.meshes.size();
    serializer.setMeshCount(meshCount);
    for (std::size_t i = 0; i < meshCount; ++i) {
      const ImportedMesh& mesh = built.source.meshes[i];
      const ImportedMaterial& meshMaterial = built.source.materials[static_cast<std::size_t>(mesh.materialIndex)];
      const std::string materialName =
          meshMaterial.origin == MaterialOrigin::DefaultFallback ? materialLibrary.defaultFallbackMaterial()->getName() : meshMaterial.name;
      const std::size_t indexWidth = mesh.vertices.size() > 65535 ? 32 : 16;

      // Per-mesh Type/Visible metadata no longer rides along in the exported name (TRACK_MODEL_LIST_PLAN.md
      // Milestone 3.2 retired that convention) -- it lives only in the associated <Model> XML, so the
      // mesh name is always written completely unchanged now.
      serializer.setName(i, mesh.name);
      serializer.setMaterial(i, materialName);
      serializer.setPrimitiveType(i, mpp::mesh::Primitive::Type::Triangles);
      serializer.setPrimitiveCount(i, mesh.indices.size() / 3);

      const std::vector<std::uint8_t> packed = packVertices(mesh.vertices);
      std::shared_ptr<std::int8_t> vertexBuffer = makeArraySharedPtr<std::int8_t>(packed.size());
      std::memcpy(vertexBuffer.get(), packed.data(), packed.size());
      serializer.addVertexStream(i, mesh.vertices.size(), 36, vertexBuffer);

      serializer.setIndexBuffer(i, packIndices(mesh.indices, indexWidth), indexWidth);
    }

    serializer.save(utf8Path);
    return true;
  } catch (const std::exception& error) {
    if (outError) *outError = error.what();
    return false;
  }
}

}  // namespace modeltool
