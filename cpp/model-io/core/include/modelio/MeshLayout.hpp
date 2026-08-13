// MeshLayout.hpp — the vertex-layout contract, and queries over an arbitrary mpp MeshSpecification.
//
// This is the single home for the game's two layouts (docs/adr/0004-gltf-import.md, D2). The
// definitions previously lived as mono::gameMeshSpecification in cpp/tungsten-monoxide, where the
// editor -- which now bakes 52-byte tracks itself -- could not reach them.
//
//   legacy (36 bytes): position3 f32, normal3 f32, texcoord2 f32, colour4 unorm8
//   pbr    (52 bytes): the above plus tangent4 f32
#pragma once

#include <cstddef>
#include <optional>

#include <mpp/mesh/MeshSpecification.h>

namespace modelio {

inline constexpr std::size_t LegacyPbrVertexStride = 36;
inline constexpr std::size_t PbrVertexStride = 52;

// Throws std::logic_error if the constructed layout doesn't match the declared stride above --
// the same self-check the tungsten-monoxide original carried, kept because these two numbers are
// hard-coded in Map.cpp's stride dispatch and a silent drift there is unrecoverable.
mpp::mesh::MeshSpecification gameMeshSpecification(bool indexed, bool pbr);

struct AttributeRef {
  mpp::mesh::Vertex::Component component{mpp::mesh::Vertex::Component::Unused};
  mpp::mesh::Vertex::DataType dataType{mpp::mesh::Vertex::DataType::None};
  bool normalised{false};
  std::size_t offsetInBytes{0};
};

bool specHasComponent(const mpp::mesh::MeshSpecification& spec, mpp::mesh::Vertex::Component component);

// Searches the first vertex-buffer layout only; see VertexPacking.hpp on why multi-buffer
// specifications are rejected rather than handled.
std::optional<AttributeRef> findAttribute(const mpp::mesh::MeshSpecification& spec,
                                          mpp::mesh::Vertex::Component component);

const char* componentName(mpp::mesh::Vertex::Component component);
const char* dataTypeName(mpp::mesh::Vertex::DataType dataType);

}  // namespace modelio
