#include "ModelResources.hpp"

#include <atomic>
#include <cstring>

#include <mpp/ProgrammaticMaterialStream.h>
#include <mpp/ProgrammaticModelStream.h>
#include <mpp/ProgrammaticTextureStream.h>
#include <mpp/ResourceManager.h>
#include <mpp/ResourceWrangler.h>
#include <mpp/mesh/MeshSpecification.h>

#include "TextureLoad.hpp"

namespace modeltool {
namespace {

// Every resource name declared here carries a monotonically increasing generation number so a
// second import (which replaces the current single loaded model, see main.cpp) never collides
// with a still-being-torn-down previous generation's resource names.
std::atomic<int> gGeneration{0};

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

}  // namespace

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

BuiltModel buildModel(mpp::ResourceManager& resourceMgr, mpp::ResourceWrangler& wrangler, ImportedModel imported) {
  const int generation = gGeneration.fetch_add(1);
  const mpp::mesh::MeshSpecification meshSpec = fixedMeshSpecification();

  BuiltModel built;
  built.materials.reserve(imported.materials.size());
  for (std::size_t i = 0; i < imported.materials.size(); ++i) {
    const ImportedMaterial& src = imported.materials[i];
    BuiltMaterial material;
    material.name = "ModelTool.Material." + std::to_string(generation) + "." + std::to_string(i);

    // mpp's built-in sentinel texture (ADR 0001 D7) needs no resource declaration of its own --
    // RenderSystem::createCoreResources() already registers it. A real diffuse texture gets its
    // own uniquely-named Texture resource, loaded via modeltool::loadImage (TextureLoad.hpp).
    std::string textureName = "__mpp_tex_none__";
    if (src.diffuseTexturePath.has_value()) {
      textureName = "ModelTool.Texture." + std::to_string(generation) + "." + std::to_string(i);
      auto* texStream = new mpp::ProgrammaticTextureStream(&resourceMgr);
      // setTarget() must be called before setFile() -- every real (non-render-target,
      // non-sentinel) texture in this codebase does this (e.g.
      // ext/massivepolypusher/demo-suite/src/ModelScene.cpp's createSharedTextures()). Omitting it
      // leaves mTarget at 0, which crashed here: Texture::createImpl()/loadImpl() binds/uploads
      // against that invalid target, and real GL drivers do not treat glBindTexture(0, id) as a
      // safe no-op the way one might hope.
      texStream->setTarget(mpp::TextureTarget::Texture2D);
      texStream->setFiltering(mpp::TextureParams::MinFilter::Linear, mpp::TextureParams::MagFilter::Linear);
      texStream->setFile(*src.diffuseTexturePath, &loadImage);
      material.textureResource = resourceMgr.declareResource(textureName, mpp::ResourceStreamPtr(texStream)).first;
      material.textureResource->acquire(&wrangler);
      material.textureResource->load();
    }

    // No setProgram() call: leaving the material's program at its default (Type::Default, is2d
    // false) resolves to mpp::RenderSystem's own core "__mpp_p3d_tris_p3n3t2c4__" program via
    // ResourceManager::getDefault3dProgram() -- the exact ambient+lights+diffuse-texture shading
    // ADR 0001 D6 asked for, already declared and already matching this fixed vertex layout. See
    // this header's top comment; matches StatePlayTungstenMonoxide::createTorusMaterial() exactly.
    auto* matStream = new mpp::ProgrammaticMaterialStream(&resourceMgr);
    matStream->setProgram2d(false);
    matStream->setMeshSpecification(meshSpec);
    matStream->setTexture("TEX1", textureName);
    material.stream = mpp::ResourceStreamPtr(matStream);

    material.materialResource = resourceMgr.declareResource(material.name, material.stream).first;
    material.materialResource->acquire(&wrangler);
    material.materialResource->load();

    built.materials.push_back(std::move(material));
  }

  auto* modelStream = new mpp::ProgrammaticModelStream(&resourceMgr);
  for (const ImportedMesh& mesh : imported.meshes) {
    const std::string& materialName = built.materials[static_cast<std::size_t>(mesh.materialIndex)].name;
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
  built.modelResource = resourceMgr.declareResource(modelName, mpp::ResourceStreamPtr(modelStream)).first;
  built.modelResource->acquire(&wrangler);
  built.modelResource->load();

  built.source = std::move(imported);
  return built;
}

void releaseBuiltModel(BuiltModel& built, mpp::ResourceWrangler& wrangler) {
  // Release order: the model first (it depends on the materials/textures being drawable, not the
  // other way around), then materials, then their textures -- mirrors
  // StatePlayTungstenMonoxide::destroyGameObjects()'s release-against-the-same-wrangler pattern.
  if (built.modelResource) built.modelResource->release(&wrangler);
  for (BuiltMaterial& material : built.materials) {
    if (material.materialResource) material.materialResource->release(&wrangler);
    if (material.textureResource) material.textureResource->release(&wrangler);
  }
  built.modelResource.reset();
  built.materials.clear();
}

}  // namespace modeltool
