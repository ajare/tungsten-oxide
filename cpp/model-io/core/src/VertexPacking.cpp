#include "modelio/VertexPacking.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "modelio/MeshLayout.hpp"

namespace modelio {
namespace {

using Component = mpp::mesh::Vertex::Component;
using DataType = mpp::mesh::Vertex::DataType;

// Every component this converter can supply, expressed as up to four floats. Colour arrives
// normalised to 0..1 so that a target asking for float32 colour and one asking for unorm8 both
// round-trip exactly.
bool componentValues(Component component, const Vertex& vertex, float* out, std::size_t& count) {
  switch (component) {
    case Component::Position2:
      out[0] = vertex.position[0];
      out[1] = vertex.position[1];
      count = 2;
      return true;
    case Component::Position3:
      out[0] = vertex.position[0];
      out[1] = vertex.position[1];
      out[2] = vertex.position[2];
      count = 3;
      return true;
    case Component::Position4:
      out[0] = vertex.position[0];
      out[1] = vertex.position[1];
      out[2] = vertex.position[2];
      out[3] = 1.0f;
      count = 4;
      return true;
    case Component::Normal3:
      out[0] = vertex.normal[0];
      out[1] = vertex.normal[1];
      out[2] = vertex.normal[2];
      count = 3;
      return true;
    case Component::TexCoord2:
      out[0] = vertex.uv[0];
      out[1] = vertex.uv[1];
      count = 2;
      return true;
    case Component::Colour3:
      out[0] = vertex.colour[0] / 255.0f;
      out[1] = vertex.colour[1] / 255.0f;
      out[2] = vertex.colour[2] / 255.0f;
      count = 3;
      return true;
    case Component::Colour4:
      out[0] = vertex.colour[0] / 255.0f;
      out[1] = vertex.colour[1] / 255.0f;
      out[2] = vertex.colour[2] / 255.0f;
      out[3] = vertex.colour[3] / 255.0f;
      count = 4;
      return true;
    case Component::Tangent4:
      out[0] = vertex.tangent[0];
      out[1] = vertex.tangent[1];
      out[2] = vertex.tangent[2];
      out[3] = vertex.tangent[3];
      count = 4;
      return true;
    default:
      return false;
  }
}

template <typename T>
void writeScalar(std::int8_t* destination, T value) {
  std::memcpy(destination, &value, sizeof(T));
}

// Encodes one already-float component value into `dataType`. Normalised integer targets map the
// full [0,1] (unsigned) or [-1,1] (signed) range onto the type's range, matching GL's own
// interpretation of a normalised vertex attribute.
bool encode(float value, DataType dataType, bool normalised, std::int8_t* destination) {
  switch (dataType) {
    case DataType::Float:
      writeScalar(destination, value);
      return true;
    case DataType::UnsignedByte:
      writeScalar(destination, static_cast<std::uint8_t>(
                                   normalised ? std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)
                                              : std::lround(std::clamp(value, 0.0f, 255.0f))));
      return true;
    case DataType::Byte:
      writeScalar(destination, static_cast<std::int8_t>(
                                   normalised ? std::lround(std::clamp(value, -1.0f, 1.0f) * 127.0f)
                                              : std::lround(std::clamp(value, -128.0f, 127.0f))));
      return true;
    case DataType::UnsignedShort:
      writeScalar(destination, static_cast<std::uint16_t>(
                                   normalised ? std::lround(std::clamp(value, 0.0f, 1.0f) * 65535.0f)
                                              : std::lround(std::clamp(value, 0.0f, 65535.0f))));
      return true;
    case DataType::Short:
      writeScalar(destination, static_cast<std::int16_t>(
                                   normalised ? std::lround(std::clamp(value, -1.0f, 1.0f) * 32767.0f)
                                              : std::lround(std::clamp(value, -32768.0f, 32767.0f))));
      return true;
    default:
      // float16/float64 and the packed 2_10_10_10 forms are legal in mpp but unused by anything in
      // this repository; refusing loudly beats emitting a plausible-looking wrong buffer.
      return false;
  }
}

}  // namespace

bool packMesh(const MeshData& mesh, const mpp::mesh::MeshSpecification& spec, PackedMesh& out, Report& report) {
  out = {};

  if (spec.getNumVertexBufferAttributeLayouts() != 1) {
    report.error("layout.multiple-buffers",
                 "the target material declares " + std::to_string(spec.getNumVertexBufferAttributeLayouts()) +
                     " vertex buffers; only a single interleaved buffer is supported",
                 mesh.name);
    return false;
  }

  const auto& layout = spec.getVertexBufferAttributeLayout(0);
  const std::size_t stride = spec.getVertexStrideInBytes();
  if (stride == 0) {
    report.error("layout.empty", "the target material's vertex layout declares no attributes", mesh.name);
    return false;
  }

  // A non-indexed target consumes the source's indices by expanding them into a flat stream; an
  // indexed target keeps the source's shared vertices and carries the indices through.
  const bool indexed = spec.verticesIndexed();
  std::vector<std::uint32_t> order;
  if (indexed) {
    order.resize(mesh.vertices.size());
    for (std::uint32_t i = 0; i < order.size(); ++i) order[i] = i;
    out.indices = mesh.indices;
  } else {
    order = mesh.indices;
  }

  for (const std::uint32_t index : order) {
    if (index < mesh.vertices.size()) continue;
    report.error("mesh.index-out-of-range",
                 "index " + std::to_string(index) + " addresses beyond the mesh's " +
                     std::to_string(mesh.vertices.size()) + " vertices",
                 mesh.name);
    return false;
  }

  out.vertexCount = order.size();
  out.vertexBytes.assign(out.vertexCount * stride, 0);

  for (std::size_t attributeIndex = 0; attributeIndex < layout.getNumAttributes(); ++attributeIndex) {
    const auto& attribute = layout.getAttribute(attributeIndex);

    float probe[4]{};
    std::size_t componentCount = 0;
    if (!componentValues(attribute.component, Vertex{}, probe, componentCount)) {
      report.error("layout.unsupported-component",
                   std::string("the target material's vertex layout names '") + componentName(attribute.component) +
                       "', which this converter cannot supply from a glTF source",
                   mesh.name);
      return false;
    }

    const std::size_t elementSize = mpp::mesh::Vertex::getDataTypeSize(attribute.dataType);
    std::int8_t encodeProbe[8]{};
    if (!encode(0.0f, attribute.dataType, attribute.normalised, encodeProbe)) {
      report.error("layout.unsupported-datatype",
                   std::string("channel '") + componentName(attribute.component) + "' uses datatype '" +
                       dataTypeName(attribute.dataType) + "', which this converter cannot encode",
                   mesh.name);
      return false;
    }

    for (std::size_t v = 0; v < out.vertexCount; ++v) {
      const Vertex& vertex = mesh.vertices[order[v]];
      float values[4]{};
      std::size_t count = 0;
      componentValues(attribute.component, vertex, values, count);

      std::int8_t* destination = out.vertexBytes.data() + v * stride + static_cast<std::size_t>(attribute.offsetInBytes);
      for (std::size_t c = 0; c < count; ++c) encode(values[c], attribute.dataType, attribute.normalised, destination + c * elementSize);
    }
  }

  return true;
}

}  // namespace modelio
