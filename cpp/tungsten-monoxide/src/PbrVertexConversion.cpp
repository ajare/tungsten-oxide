#include "PbrVertexConversion.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/glm.hpp>
#pragma warning(pop)

namespace {
float readFloat(const std::int8_t* data, std::size_t offset) {
  float value;
  std::memcpy(&value, data + offset, sizeof(value));
  return value;
}

glm::vec3 readVec3(const std::int8_t* data, std::size_t offset) {
  return {readFloat(data, offset), readFloat(data, offset + 4), readFloat(data, offset + 8)};
}

glm::vec2 readVec2(const std::int8_t* data, std::size_t offset) {
  return {readFloat(data, offset), readFloat(data, offset + 4)};
}

bool finite(glm::vec2 const& value) {
  return std::isfinite(value.x) && std::isfinite(value.y);
}

bool finite(glm::vec3 const& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool usable(glm::vec3 const& value) {
  return finite(value) && glm::dot(value, value) > 1e-20f;
}

glm::vec3 fallbackTangent(glm::vec3 const& normal) {
  glm::vec3 axis = std::abs(normal.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
  return glm::normalize(glm::cross(axis, normal));
}
}  // namespace

namespace mono {
std::vector<std::int8_t> addPbrTangents(std::span<const std::int8_t> vertices,
                                        std::size_t vertexCount,
                                        std::span<const std::uint32_t> triangleIndices) {
  if (vertices.size() != vertexCount * LegacyPbrVertexStride)
    throw std::invalid_argument("legacy PBR vertex data must have a 36-byte stride");
  if (triangleIndices.size() % 3 != 0)
    throw std::invalid_argument("PBR tangent index count must be divisible by three");

  for (std::size_t vertex = 0; vertex < vertexCount; ++vertex) {
    auto const* source = vertices.data() + vertex * LegacyPbrVertexStride;
    if (!finite(readVec3(source, 0)))
      throw std::invalid_argument("PBR vertex position contains a non-finite value at vertex " +
                                  std::to_string(vertex));
    if (!finite(readVec3(source, 12)))
      throw std::invalid_argument("PBR vertex normal contains a non-finite value at vertex " +
                                  std::to_string(vertex));
    if (!finite(readVec2(source, 24)))
      throw std::invalid_argument("PBR vertex texture coordinate contains a non-finite value at vertex " +
                                  std::to_string(vertex));
  }

  std::vector<glm::vec3> tangentSums(vertexCount, glm::vec3(0.0f));
  std::vector<glm::vec3> bitangentSums(vertexCount, glm::vec3(0.0f));
  for (std::size_t triangle = 0; triangle < triangleIndices.size(); triangle += 3) {
    std::uint32_t const i0 = triangleIndices[triangle];
    std::uint32_t const i1 = triangleIndices[triangle + 1];
    std::uint32_t const i2 = triangleIndices[triangle + 2];
    if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
      throw std::invalid_argument("PBR tangent index is out of range");

    auto const* v0 = vertices.data() + static_cast<std::size_t>(i0) * LegacyPbrVertexStride;
    auto const* v1 = vertices.data() + static_cast<std::size_t>(i1) * LegacyPbrVertexStride;
    auto const* v2 = vertices.data() + static_cast<std::size_t>(i2) * LegacyPbrVertexStride;
    glm::vec3 const edge1 = readVec3(v1, 0) - readVec3(v0, 0);
    glm::vec3 const edge2 = readVec3(v2, 0) - readVec3(v0, 0);
    glm::vec2 const delta1 = readVec2(v1, 24) - readVec2(v0, 24);
    glm::vec2 const delta2 = readVec2(v2, 24) - readVec2(v0, 24);
    float const determinant = delta1.x * delta2.y - delta1.y * delta2.x;
    if (!std::isfinite(determinant) || std::abs(determinant) <= 1e-12f) continue;

    float const inverse = 1.0f / determinant;
    glm::vec3 const tangent = (edge1 * delta2.y - edge2 * delta1.y) * inverse;
    glm::vec3 const bitangent = (edge2 * delta1.x - edge1 * delta2.x) * inverse;
    if (!usable(tangent) || !usable(bitangent)) continue;
    for (std::uint32_t const index : {i0, i1, i2}) {
      tangentSums[index] += tangent;
      bitangentSums[index] += bitangent;
    }
  }

  std::vector<std::int8_t> result(vertexCount * PbrVertexStride);
  for (std::size_t vertex = 0; vertex < vertexCount; ++vertex) {
    auto const* source = vertices.data() + vertex * LegacyPbrVertexStride;
    auto* destination = result.data() + vertex * PbrVertexStride;
    std::memcpy(destination, source, LegacyPbrVertexStride);

    glm::vec3 normal = readVec3(source, 12);
    if (!usable(normal)) normal = {0.0f, 1.0f, 0.0f};
    normal = glm::normalize(normal);
    glm::vec3 tangent = tangentSums[vertex] - normal * glm::dot(normal, tangentSums[vertex]);
    if (!usable(tangent))
      tangent = fallbackTangent(normal);
    else
      tangent = glm::normalize(tangent);
    float const handedness = usable(bitangentSums[vertex]) && glm::dot(glm::cross(normal, tangent), bitangentSums[vertex]) < 0.0f
                                 ? -1.0f
                                 : 1.0f;
    float const tangent4[4] = {tangent.x, tangent.y, tangent.z, handedness};
    std::memcpy(destination + LegacyPbrVertexStride, tangent4, sizeof(tangent4));
  }
  return result;
}

std::vector<std::int8_t> addPbrTangentsToFlatTriangles(std::span<const std::int8_t> vertices,
                                                       std::size_t vertexCount) {
  if (vertexCount % 3 != 0)
    throw std::invalid_argument("flat PBR vertex count must be divisible by three");
  std::vector<std::uint32_t> indices(vertexCount);
  for (std::size_t i = 0; i < vertexCount; ++i) indices[i] = static_cast<std::uint32_t>(i);
  return addPbrTangents(vertices, vertexCount, indices);
}
}  // namespace mono
