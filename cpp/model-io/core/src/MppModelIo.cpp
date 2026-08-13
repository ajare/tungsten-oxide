#include "modelio/MppModelIo.hpp"

#include <cstring>
#include <exception>
#include <memory>
#include <set>
#include <stdexcept>
#include <system_error>

#include <mpp/ModelSerializer.h>

#include "modelio/PbrMaterialBuild.hpp"
#include "modelio/VertexPacking.hpp"

namespace modelio {
namespace {

// mpp records index width in BITS (writeIndexBuffer divides by 8), not bytes.
constexpr std::size_t kNarrowIndexBits = 16;
constexpr std::size_t kWideIndexBits = 32;

std::size_t indexBitsFor(std::size_t vertexCount) {
  return vertexCount <= 0xFFFFu ? kNarrowIndexBits : kWideIndexBits;
}

std::shared_ptr<const std::uint8_t> packIndices(const std::vector<std::uint32_t>& indices, std::size_t indexBits) {
  const std::size_t width = indexBits / 8;
  auto buffer = std::shared_ptr<std::uint8_t[]>(new std::uint8_t[indices.size() * width]);
  for (std::size_t i = 0; i < indices.size(); ++i) {
    if (indexBits == kNarrowIndexBits) {
      const std::uint16_t narrow = static_cast<std::uint16_t>(indices[i]);
      std::memcpy(buffer.get() + i * width, &narrow, sizeof(narrow));
    } else {
      std::memcpy(buffer.get() + i * width, &indices[i], sizeof(std::uint32_t));
    }
  }
  return std::shared_ptr<const std::uint8_t>(buffer, buffer.get());
}

std::shared_ptr<const std::int8_t> copyVertexBytes(const std::vector<std::int8_t>& bytes) {
  auto buffer = std::shared_ptr<std::int8_t[]>(new std::int8_t[bytes.size()]);
  if (!bytes.empty()) std::memcpy(buffer.get(), bytes.data(), bytes.size());
  return std::shared_ptr<const std::int8_t>(buffer, buffer.get());
}

}  // namespace

bool writeMppModel(const ModelData& model, const TargetMaterial& target, const std::filesystem::path& outPath,
                   Report& report) {
  if (model.meshes.empty()) {
    report.error("model.no-meshes", "the source asset produced no meshes to write");
    return false;
  }

  const std::filesystem::path modelDirectory =
      outPath.has_parent_path() ? outPath.parent_path() : std::filesystem::current_path();

  mpp::ModelSerializer serializer(nullptr);
  serializer.setMeshCount(model.meshes.size());

  // Materials first: a mesh's material reference must name one of these. Only those some mesh
  // actually uses are embedded -- AssImp's glTF2 importer appends a default material to every
  // scene whether or not anything references it, and embedding that would put a material in the
  // file that no mesh can ever resolve to.
  std::set<int> usedMaterials;
  for (const MeshData& mesh : model.meshes) usedMaterials.insert(mesh.materialIndex);

  for (const int index : usedMaterials) {
    const MaterialData& material = model.materials[static_cast<std::size_t>(index)];
    mpp::ResourceStreamPtr stream = buildEmbeddedPbrMaterial(material, target, modelDirectory, report);
    if (!stream) return false;
    serializer.addMaterial(material.name, stream);
  }

  const bool indexed = target.meshSpec.verticesIndexed();

  for (std::size_t i = 0; i < model.meshes.size(); ++i) {
    const MeshData& mesh = model.meshes[i];

    PackedMesh packed;
    if (!packMesh(mesh, target.meshSpec, packed, report)) return false;

    if (packed.vertexCount == 0) {
      report.error("mesh.empty", "mesh has no triangles after conversion", mesh.name);
      return false;
    }

    serializer.setName(i, mesh.name);
    serializer.setPrimitiveType(i, mpp::mesh::Primitive::Type::Triangles);
    serializer.setMaterial(i, model.materials[static_cast<std::size_t>(mesh.materialIndex)].name);
    serializer.addVertexStream(i, packed.vertexCount, target.meshSpec.getVertexStrideInBytes(),
                               copyVertexBytes(packed.vertexBytes));

    if (indexed) {
      // primitiveCount drives how many bytes writeIndexBuffer emits, so it must be set before
      // setIndexBuffer's stream is written -- both happen at save() time, but the ordering of the
      // two setters relative to each other does not matter, only that both are set.
      serializer.setPrimitiveCount(i, packed.indices.size() / 3);
      const std::size_t indexBits = indexBitsFor(packed.vertexCount);
      serializer.setIndexBuffer(i, packIndices(packed.indices, indexBits), indexBits);
    } else {
      serializer.setPrimitiveCount(i, packed.vertexCount / 3);
    }
  }

  std::error_code error;
  if (modelDirectory != outPath && !std::filesystem::exists(modelDirectory))
    std::filesystem::create_directories(modelDirectory, error);

  try {
    serializer.save(outPath.string());
  } catch (const std::exception& failure) {
    report.error("write.failed", std::string("could not write '") + outPath.string() + "': " + failure.what());
    std::filesystem::remove(outPath, error);
    return false;
  }

  return true;
}

ReadModel readMppModel(const std::filesystem::path& path, bool indexed) {
  mpp::ModelSerializer serializer(nullptr);
  serializer.load(path.string());

  ReadModel out;
  out.materialNames = serializer.getMaterialNames();
  out.materials = serializer.getMaterials();

  for (std::size_t i = 0; i < serializer.getMeshCount(); ++i) {
    ReadMesh mesh;
    mesh.name = serializer.getName(i);
    mesh.material = serializer.getMaterial(i);
    mesh.primitiveCount = static_cast<std::size_t>(serializer.getPrimitiveCount(i));

    std::shared_ptr<const std::int8_t> vertexData;
    serializer.getVertexStream(i, 0, &mesh.vertexCount, &mesh.vertexStride, &vertexData);
    mesh.vertexBytes.assign(vertexData.get(), vertexData.get() + mesh.vertexCount * mesh.vertexStride);

    if (indexed) {
      const std::size_t indexBits = static_cast<std::size_t>(serializer.getIndexWidth(i));
      const std::shared_ptr<const std::uint8_t> indexData = serializer.getIndexData(i);
      const std::size_t indexCount = mesh.primitiveCount * 3;
      mesh.indices.reserve(indexCount);
      for (std::size_t k = 0; k < indexCount; ++k) {
        if (indexBits == kNarrowIndexBits) {
          std::uint16_t narrow = 0;
          std::memcpy(&narrow, indexData.get() + k * 2, sizeof(narrow));
          mesh.indices.push_back(narrow);
        } else {
          std::uint32_t wide = 0;
          std::memcpy(&wide, indexData.get() + k * 4, sizeof(wide));
          mesh.indices.push_back(wide);
        }
      }
    }

    out.meshes.push_back(std::move(mesh));
  }

  return out;
}

}  // namespace modelio
