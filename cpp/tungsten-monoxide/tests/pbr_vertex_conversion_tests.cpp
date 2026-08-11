#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/glm.hpp>
#pragma warning(pop)

#include "PbrVertexConversion.h"

namespace {
void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

void appendVertex(std::vector<std::int8_t>& bytes, glm::vec3 position, glm::vec3 normal, glm::vec2 uv) {
  std::size_t const start = bytes.size();
  bytes.resize(start + mono::LegacyPbrVertexStride);
  std::memcpy(bytes.data() + start, &position, sizeof(position));
  std::memcpy(bytes.data() + start + 12, &normal, sizeof(normal));
  std::memcpy(bytes.data() + start + 24, &uv, sizeof(uv));
  std::uint8_t const colour[4] = {255, 255, 255, 255};
  std::memcpy(bytes.data() + start + 32, colour, sizeof(colour));
}

glm::vec4 tangentAt(std::vector<std::int8_t> const& bytes, std::size_t vertex) {
  glm::vec4 tangent;
  std::memcpy(&tangent, bytes.data() + vertex * mono::PbrVertexStride + mono::LegacyPbrVertexStride,
              sizeof(tangent));
  return tangent;
}

void checkFrame(glm::vec4 tangent, glm::vec3 normal) {
  require(std::isfinite(tangent.x) && std::isfinite(tangent.y) && std::isfinite(tangent.z) &&
              std::isfinite(tangent.w),
          "tangent contains a non-finite value");
  require(std::abs(glm::length(glm::vec3(tangent)) - 1.0f) < 1e-5f, "tangent is not normalized");
  require(std::abs(glm::dot(glm::vec3(tangent), normal)) < 1e-5f, "tangent is not orthogonal to normal");
  require(tangent.w == 1.0f || tangent.w == -1.0f, "tangent handedness is invalid");
}
}  // namespace

int main() {
  try {
    std::vector<std::int8_t> triangle;
    appendVertex(triangle, {0, 0, 0}, {0, 0, 1}, {0, 0});
    appendVertex(triangle, {1, 0, 0}, {0, 0, 1}, {1, 0});
    appendVertex(triangle, {0, 1, 0}, {0, 0, 1}, {0, 1});
    auto converted = mono::addPbrTangentsToFlatTriangles(triangle, 3);
    require(converted.size() == 3 * mono::PbrVertexStride, "conversion did not produce a 52-byte stride");
    for (std::size_t i = 0; i < 3; ++i) {
      require(std::memcmp(converted.data() + i * mono::PbrVertexStride,
                          triangle.data() + i * mono::LegacyPbrVertexStride,
                          mono::LegacyPbrVertexStride) == 0,
              "conversion altered a legacy vertex channel");
      glm::vec4 const tangent = tangentAt(converted, i);
      checkFrame(tangent, {0, 0, 1});
      require(glm::dot(glm::vec3(tangent), glm::vec3(1, 0, 0)) > 0.999f, "unexpected tangent direction");
      require(tangent.w == 1.0f, "unexpected tangent handedness");
    }

    std::vector<std::int8_t> indexedQuad;
    appendVertex(indexedQuad, {0, 0, 0}, {0, 0, 1}, {0, 0});
    appendVertex(indexedQuad, {1, 0, 0}, {0, 0, 1}, {1, 0});
    appendVertex(indexedQuad, {1, 1, 0}, {0, 0, 1}, {1, 1});
    appendVertex(indexedQuad, {0, 1, 0}, {0, 0, 1}, {0, 1});
    std::vector<std::uint32_t> const quadIndices{0, 1, 2, 0, 2, 3};
    auto indexedConverted = mono::addPbrTangents(indexedQuad, 4, quadIndices);
    for (std::size_t i = 0; i < 4; ++i) {
      glm::vec4 const tangent = tangentAt(indexedConverted, i);
      checkFrame(tangent, {0, 0, 1});
      require(glm::dot(glm::vec3(tangent), glm::vec3(1, 0, 0)) > 0.999f,
              "shared indexed vertex accumulated an unexpected tangent");
    }

    std::vector<std::int8_t> mirrored;
    appendVertex(mirrored, {0, 0, 0}, {0, 0, 1}, {0, 0});
    appendVertex(mirrored, {1, 0, 0}, {0, 0, 1}, {0, 1});
    appendVertex(mirrored, {0, 1, 0}, {0, 0, 1}, {1, 0});
    auto mirroredConverted = mono::addPbrTangentsToFlatTriangles(mirrored, 3);
    require(tangentAt(mirroredConverted, 0).w == -1.0f, "mirrored UVs did not produce negative handedness");

    // Mirrors the post-placement input Map produces after rotating and translating a mesh object:
    // the generated frame must follow transformed position and normal channels.
    std::vector<std::int8_t> transformedPlacement;
    appendVertex(transformedPlacement, {3, -2, 5}, {1, 0, 0}, {0, 0});
    appendVertex(transformedPlacement, {3, -2, 4}, {1, 0, 0}, {1, 0});
    appendVertex(transformedPlacement, {3, -1, 5}, {1, 0, 0}, {0, 1});
    auto transformedConverted = mono::addPbrTangentsToFlatTriangles(transformedPlacement, 3);
    for (std::size_t i = 0; i < 3; ++i) {
      glm::vec4 const tangent = tangentAt(transformedConverted, i);
      checkFrame(tangent, {1, 0, 0});
      require(glm::dot(glm::vec3(tangent), glm::vec3(0, 0, -1)) > 0.999f,
              "placed mesh tangent did not follow transformed geometry");
    }

    std::vector<std::int8_t> degenerate;
    appendVertex(degenerate, {0, 0, 0}, {0, 1, 0}, {0, 0});
    appendVertex(degenerate, {1, 0, 0}, {0, 1, 0}, {0, 0});
    appendVertex(degenerate, {0, 0, 1}, {0, 1, 0}, {0, 0});
    auto degenerateConverted = mono::addPbrTangentsToFlatTriangles(degenerate, 3);
    for (std::size_t i = 0; i < 3; ++i) checkFrame(tangentAt(degenerateConverted, i), {0, 1, 0});

    auto requireNonFiniteRejected = [&](std::size_t componentOffset) {
      auto invalid = triangle;
      float const nan = std::numeric_limits<float>::quiet_NaN();
      std::memcpy(invalid.data() + componentOffset, &nan, sizeof(nan));
      bool rejected = false;
      try {
        (void)mono::addPbrTangentsToFlatTriangles(invalid, 3);
      } catch (std::invalid_argument const&) {
        rejected = true;
      }
      require(rejected, "non-finite PBR vertex input was accepted");
    };
    requireNonFiniteRejected(0);   // position
    requireNonFiniteRejected(12);  // normal
    requireNonFiniteRejected(24);  // texture coordinate

    bool outOfRangeRejected = false;
    try {
      std::vector<std::uint32_t> const invalidIndices{0, 1, 3};
      (void)mono::addPbrTangents(triangle, 3, invalidIndices);
    } catch (std::invalid_argument const&) {
      outOfRangeRejected = true;
    }
    require(outOfRangeRejected, "out-of-range PBR triangle index was accepted");

    std::cout << "PBR vertex conversion tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
