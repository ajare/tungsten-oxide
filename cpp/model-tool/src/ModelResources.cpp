#include "ModelResources.hpp"

#include <atomic>
#include <cstring>

#include <mpp/ProgrammaticModelStream.h>
#include <mpp/ResourceManager.h>
#include <mpp/ResourceWrangler.h>
#include <mpp/mesh/MeshSpecification.h>

namespace modeltool {
namespace {

// The live Model resource still needs a fresh unique name every load (unlike materials, it's never
// looked up by a caller-meaningful name, so a generation counter is simplest).
std::atomic<int> gModelGeneration{0};

}  // namespace

mpp::mesh::MeshSpecification fixedMeshSpecification() {
  mpp::mesh::MeshSpecification spec(mpp::mesh::Primitive::Type::Triangles);
  mpp::mesh::VertexBufferAttributeLayout* layout = spec.createVertexBufferAttributeLayout(false);
  layout->createAttribute(mpp::mesh::Vertex::Component::Position3, mpp::mesh::Vertex::DataType::Float, false);
  layout->createAttribute(mpp::mesh::Vertex::Component::Normal3, mpp::mesh::Vertex::DataType::Float, false);
  layout->createAttribute(mpp::mesh::Vertex::Component::TexCoord2, mpp::mesh::Vertex::DataType::Float, false);
  layout->createAttribute(mpp::mesh::Vertex::Component::Colour4, mpp::mesh::Vertex::DataType::UnsignedByte, true);
  spec.setStorageType(mpp::mesh::VertexBufferStorageType::Static);
  spec.setIndexedVertices(true);
  return spec;
}

// Packs ImportedVertex into the fixed 36-byte layout (see AssImpImport.hpp), shared by the live
// ProgrammaticModelStream upload here and MppSave's file serialization -- one packing routine, one
// definition of the layout's byte order.
std::vector<std::uint8_t> packVertices(const std::vector<ImportedVertex>& vertices) {
  std::vector<std::uint8_t> out(vertices.size() * 36);
  std::uint8_t* p = out.data();
  for (const ImportedVertex& v : vertices) {
    std::memcpy(p, &v.px, 12);
    p += 12;
    std::memcpy(p, &v.nx, 12);
    p += 12;
    std::memcpy(p, &v.u, 8);
    p += 8;
    p[0] = v.r;
    p[1] = v.g;
    p[2] = v.b;
    p[3] = v.a;
    p += 4;
  }
  return out;
}

mpp::ResourcePtr rebuildModelResource(mpp::ResourceManager& resourceMgr, mpp::ResourceWrangler& wrangler, const ImportedModel& imported,
                                       const std::string& defaultFallbackMaterialName) {
  const int generation = gModelGeneration.fetch_add(1);
  const mpp::mesh::MeshSpecification meshSpec = fixedMeshSpecification();

  auto* modelStream = new mpp::ProgrammaticModelStream(&resourceMgr);
  for (const ImportedMesh& mesh : imported.meshes) {
    const ImportedMaterial& material = imported.materials[static_cast<std::size_t>(mesh.materialIndex)];
    const std::string& materialName = material.origin == MaterialOrigin::DefaultFallback ? defaultFallbackMaterialName : material.name;
    // Real shared-vertex indexing (16-bit unless the mesh needs 32), not a flattened triangle soup
    // -- unlike track geometry (ADR 0001 D5), an imported mesh's vertices are genuinely shared.
    const int indexWidth = mesh.vertices.size() > 65535 ? 32 : 16;
    const std::size_t meshIndex = modelStream->createMesh(mesh.name, meshSpec, materialName, indexWidth);
    // addVertexData() only overloads on vector<int8_t> (or mesh::VertexData) -- reinterpret the
    // packed bytes (packVertices() returns uint8_t for the unsigned-byte colour channel to make
    // sense as written) as signed bytes; the bit pattern is what matters, not the signedness.
    const std::vector<std::uint8_t> packed = packVertices(mesh.vertices);
    modelStream->addVertexData(meshIndex, std::vector<std::int8_t>(packed.begin(), packed.end()));
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
      modelStream->addTriangle(meshIndex, mesh.indices[t], mesh.indices[t + 1], mesh.indices[t + 2]);
  }

  const std::string modelName = "ModelTool.Model." + std::to_string(generation);
  mpp::ResourcePtr resource = resourceMgr.declareResource(modelName, mpp::ResourceStreamPtr(modelStream)).first;
  resource->acquire(&wrangler);
  resource->load();
  return resource;
}

BuiltModel buildModel(mpp::ResourceManager& resourceMgr, mpp::ResourceWrangler& wrangler, ImportedModel imported,
                       std::vector<std::optional<MaterialReference>> materialRefs, const std::string& defaultFallbackMaterialName) {
  BuiltModel built;
  built.modelResource = rebuildModelResource(resourceMgr, wrangler, imported, defaultFallbackMaterialName);
  built.materialRefs = std::move(materialRefs);
  built.source = std::move(imported);
  return built;
}

void releaseBuiltModel(BuiltModel& built, mpp::ResourceWrangler& wrangler, MaterialLibrary& materialLibrary) {
  // Release order: the model first (it depends on the materials being drawable, not the other way
  // around), then the materials themselves -- mirrors
  // StatePlayTungstenMonoxide::destroyGameObjects()'s release-against-the-same-wrangler pattern.
  if (built.modelResource) built.modelResource->release(&wrangler);
  for (std::optional<MaterialReference>& ref : built.materialRefs) {
    if (ref.has_value()) materialLibrary.releaseModelReference(*ref);
  }
  built.modelResource.reset();
  built.materialRefs.clear();
}

}  // namespace modeltool
