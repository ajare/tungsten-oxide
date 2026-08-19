#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mono {
constexpr std::size_t LegacyPbrVertexStride = 36;
constexpr std::size_t PbrVertexStride = 52;

// Expands the legacy position/normal/uv/colour layout with an orthonormal tangent4.
// Indices are used only to accumulate indexed tangent frames; output vertex ordering is unchanged.
std::vector<std::int8_t> addPbrTangents(std::span<const std::int8_t> vertices,
                                        std::size_t vertexCount,
                                        std::span<const std::uint32_t> triangleIndices);

std::vector<std::int8_t> addPbrTangentsToFlatTriangles(std::span<const std::int8_t> vertices,
                                                       std::size_t vertexCount);
}  // namespace mono
