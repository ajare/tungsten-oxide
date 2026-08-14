#include "modelio/Tangents.hpp"

#include <cmath>
#include <cstddef>

namespace modelio {
namespace {

struct Vec3 {
  float x{0.0f}, y{0.0f}, z{0.0f};
};

Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(const Vec3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
void operator+=(Vec3& a, const Vec3& b) { a = a + b; }

float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

bool finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
// Matches the tungsten-monoxide original's `usable()`: finite and not vanishingly short.
bool usable(const Vec3& v) { return finite(v) && dot(v, v) > 1e-20f; }

Vec3 normalise(const Vec3& v) {
  const float length = std::sqrt(dot(v, v));
  return length > 0.0f ? v * (1.0f / length) : v;
}

Vec3 position(const Vertex& v) { return {v.position[0], v.position[1], v.position[2]}; }
Vec3 normal(const Vertex& v) { return {v.normal[0], v.normal[1], v.normal[2]}; }

Vec3 fallbackTangent(const Vec3& n) {
  const Vec3 axis = std::fabs(n.y) < 0.9f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
  return normalise(cross(axis, n));
}

}  // namespace

void generateSmoothNormals(MeshData& mesh) {
  const std::size_t vertexCount = mesh.vertices.size();
  std::vector<Vec3> sums(vertexCount);

  for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    const std::uint32_t i0 = mesh.indices[i], i1 = mesh.indices[i + 1], i2 = mesh.indices[i + 2];
    if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) continue;
    const Vec3 p0 = position(mesh.vertices[i0]), p1 = position(mesh.vertices[i1]), p2 = position(mesh.vertices[i2]);
    // Unnormalised cross product is twice the triangle area, which is exactly the area weighting
    // we want -- a large face should dominate a sliver sharing the same vertex.
    const Vec3 faceNormal = cross(p1 - p0, p2 - p0);
    if (!usable(faceNormal)) continue;
    for (const std::uint32_t index : {i0, i1, i2}) sums[index] += faceNormal;
  }

  for (std::size_t v = 0; v < vertexCount; ++v) {
    if (!usable(sums[v])) continue;
    const Vec3 n = normalise(sums[v]);
    mesh.vertices[v].normal[0] = n.x;
    mesh.vertices[v].normal[1] = n.y;
    mesh.vertices[v].normal[2] = n.z;
  }
}

void generateTangents(MeshData& mesh) {
  const std::size_t vertexCount = mesh.vertices.size();
  std::vector<Vec3> tangentSums(vertexCount);
  std::vector<Vec3> bitangentSums(vertexCount);

  for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    const std::uint32_t i0 = mesh.indices[i], i1 = mesh.indices[i + 1], i2 = mesh.indices[i + 2];
    if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) continue;

    const Vertex& v0 = mesh.vertices[i0];
    const Vertex& v1 = mesh.vertices[i1];
    const Vertex& v2 = mesh.vertices[i2];
    const Vec3 edge1 = position(v1) - position(v0);
    const Vec3 edge2 = position(v2) - position(v0);
    const float du1 = v1.uv[0] - v0.uv[0], dv1 = v1.uv[1] - v0.uv[1];
    const float du2 = v2.uv[0] - v0.uv[0], dv2 = v2.uv[1] - v0.uv[1];

    const float determinant = du1 * dv2 - dv1 * du2;
    if (!std::isfinite(determinant) || std::fabs(determinant) <= 1e-12f) continue;

    const float inverse = 1.0f / determinant;
    const Vec3 tangent = (edge1 * dv2 - edge2 * dv1) * inverse;
    const Vec3 bitangent = (edge2 * du1 - edge1 * du2) * inverse;
    if (!usable(tangent) || !usable(bitangent)) continue;
    for (const std::uint32_t index : {i0, i1, i2}) {
      tangentSums[index] += tangent;
      bitangentSums[index] += bitangent;
    }
  }

  for (std::size_t v = 0; v < vertexCount; ++v) {
    Vertex& vertex = mesh.vertices[v];
    Vec3 n = normal(vertex);
    if (!usable(n)) n = {0.0f, 1.0f, 0.0f};
    n = normalise(n);

    // Gram-Schmidt: drop the component of the accumulated tangent that lies along the normal.
    Vec3 tangent = tangentSums[v] - n * dot(n, tangentSums[v]);
    tangent = usable(tangent) ? normalise(tangent) : fallbackTangent(n);

    const bool mirrored = usable(bitangentSums[v]) && dot(cross(n, tangent), bitangentSums[v]) < 0.0f;
    vertex.tangent[0] = tangent.x;
    vertex.tangent[1] = tangent.y;
    vertex.tangent[2] = tangent.z;
    vertex.tangent[3] = mirrored ? -1.0f : 1.0f;
  }
}

}  // namespace modelio
