#include "PbrMeshSpecification.h"

#include <stdexcept>

#include "PbrVertexConversion.h"

namespace mono {
mpp::mesh::MeshSpecification gameMeshSpecification(bool indexed, bool pbr) {
  mpp::mesh::MeshSpecification specification(mpp::mesh::Primitive::Type::Triangles);
  auto layout = specification.createVertexBufferAttributeLayout(false);
  layout->createAttribute(mpp::mesh::Vertex::Component::Position3, mpp::mesh::Vertex::DataType::Float, false);
  layout->createAttribute(mpp::mesh::Vertex::Component::Normal3, mpp::mesh::Vertex::DataType::Float, false);
  layout->createAttribute(mpp::mesh::Vertex::Component::TexCoord2, mpp::mesh::Vertex::DataType::Float, false);
  layout->createAttribute(mpp::mesh::Vertex::Component::Colour4, mpp::mesh::Vertex::DataType::UnsignedByte, true);
  if (pbr)
    layout->createAttribute(mpp::mesh::Vertex::Component::Tangent4, mpp::mesh::Vertex::DataType::Float, false);
  specification.setStorageType(mpp::mesh::VertexBufferStorageType::Static);
  specification.setIndexedVertices(indexed);
  std::size_t const expectedStride = pbr ? PbrVertexStride : LegacyPbrVertexStride;
  if (specification.getVertexStrideInBytes() != expectedStride)
    throw std::logic_error("game mesh specification does not match the declared vertex contract");
  return specification;
}
}  // namespace mono
