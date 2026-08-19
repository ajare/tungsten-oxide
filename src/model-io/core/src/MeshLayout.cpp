#include "modelio/MeshLayout.hpp"

#include <stdexcept>
#include <string>

namespace modelio {

using mpp::mesh::MeshSpecification;
using Component = mpp::mesh::Vertex::Component;
using DataType = mpp::mesh::Vertex::DataType;

MeshSpecification gameMeshSpecification(bool indexed, bool pbr) {
  MeshSpecification specification(mpp::mesh::Primitive::Type::Triangles);
  auto* layout = specification.createVertexBufferAttributeLayout(false);
  layout->createAttribute(Component::Position3, DataType::Float, false);
  layout->createAttribute(Component::Normal3, DataType::Float, false);
  layout->createAttribute(Component::TexCoord2, DataType::Float, false);
  layout->createAttribute(Component::Colour4, DataType::UnsignedByte, true);
  if (pbr) layout->createAttribute(Component::Tangent4, DataType::Float, false);
  specification.setStorageType(mpp::mesh::VertexBufferStorageType::Static);
  specification.setIndexedVertices(indexed);

  const std::size_t expectedStride = pbr ? PbrVertexStride : LegacyPbrVertexStride;
  if (specification.getVertexStrideInBytes() != expectedStride)
    throw std::logic_error("game mesh specification does not match the declared vertex contract");
  return specification;
}

bool specHasComponent(const MeshSpecification& spec, Component component) {
  return findAttribute(spec, component).has_value();
}

std::optional<AttributeRef> findAttribute(const MeshSpecification& spec, Component component) {
  if (spec.getNumVertexBufferAttributeLayouts() == 0) return std::nullopt;
  const auto& layout = spec.getVertexBufferAttributeLayout(0);
  for (std::size_t i = 0; i < layout.getNumAttributes(); ++i) {
    const auto& attribute = layout.getAttribute(i);
    if (attribute.component != component) continue;
    return AttributeRef{attribute.component, attribute.dataType, attribute.normalised,
                        static_cast<std::size_t>(attribute.offsetInBytes)};
  }
  return std::nullopt;
}

const char* componentName(Component component) {
  switch (component) {
    case Component::Position2: return "position2";
    case Component::Position3: return "position3";
    case Component::Position4: return "position4";
    case Component::Normal3: return "normal3";
    case Component::Normal4: return "normal4";
    case Component::TexCoord2: return "texcoord2";
    case Component::TexCoord3: return "texcoord3";
    case Component::TexCoord4: return "texcoord4";
    case Component::Colour1: return "colour1";
    case Component::Colour3: return "colour3";
    case Component::Colour4: return "colour4";
    case Component::Tangent4: return "tangent4";
    case Component::UserDefined1: return "user1";
    case Component::UserDefined2: return "user2";
    case Component::UserDefined3: return "user3";
    case Component::UserDefined4: return "user4";
    case Component::Unused: break;
  }
  return "unused";
}

const char* dataTypeName(DataType dataType) {
  switch (dataType) {
    case DataType::Byte: return "int8";
    case DataType::UnsignedByte: return "uint8";
    case DataType::Short: return "int16";
    case DataType::UnsignedShort: return "uint16";
    case DataType::Int: return "int32";
    case DataType::UnsignedInt: return "uint32";
    case DataType::HalfFloat: return "float16";
    case DataType::Float: return "float32";
    case DataType::Double: return "float64";
    case DataType::Int_2_10_10_10_REV: return "int_2_10_10_10_rev";
    case DataType::UnsignedInt_2_10_10_10_REV: return "uint_2_10_10_10_rev";
    case DataType::None: break;
  }
  return "none";
}

}  // namespace modelio
