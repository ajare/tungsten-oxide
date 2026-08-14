// ModelData.hpp — the renderer-neutral, CPU-side model every model-io path passes around.
//
// Deliberately free of mpp, glm and AssImp types (plain arrays and scalars only), for two reasons:
// it is the type the editor's UI and cpp/core's own baked tox::GeometryBatch conversion both touch,
// and keeping it dependency-free means a consumer can hold a ModelData without dragging the
// serializer's <GL/glew.h> into its translation unit.
//
// The vertex is a *superset*: it carries every channel this converter knows how to supply, and
// packing (VertexPacking.hpp) emits only the subset a given MeshSpecification actually names. A
// channel the source asset didn't provide is filled with the documented default here and recorded
// in SourceChannels, so packing can report exactly what it had to invent
// (docs/adr/0004-gltf-import.md, D7) rather than silently shipping white vertex colours.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace modelio {

struct Vertex {
  float position[3]{0.0f, 0.0f, 0.0f};
  float normal[3]{0.0f, 1.0f, 0.0f};
  float uv[2]{0.0f, 0.0f};
  std::uint8_t colour[4]{255, 255, 255, 255};
  // xyz = tangent, w = handedness (+1/-1), matching the convention cpp/tungsten-monoxide's
  // addPbrTangents established and the game's 52-byte contract depends on.
  float tangent[4]{1.0f, 0.0f, 0.0f, 1.0f};
};

// What the *source* asset actually supplied, as opposed to what Vertex now holds.
struct SourceChannels {
  bool normals{false};
  bool uvs{false};
  bool colours{false};
  bool tangents{false};
};

struct MeshData {
  std::string name;
  std::vector<Vertex> vertices;
  // Always populated as a triangle list, even for a non-indexed source (0,1,2,... identity), so no
  // consumer needs to branch on indexed-ness. Packing de-indexes when the target spec asks for it.
  std::vector<std::uint32_t> indices;
  int materialIndex{0};
  SourceChannels source;
};

// A texture as the source asset referenced it: an absolute filesystem path, made relative to the
// output model only at write time (PbrMaterialBuild.hpp).
struct TextureRef {
  std::string sampler;  // PBR_BASE_COLOUR_MAP, PBR_NORMAL_MAP, ...
  std::string path;
};

// Mirrors mpp::PbrMaterialSpecification::PbrAlphaMode without including it here.
enum class AlphaMode { Opaque = 0,
                       Mask = 1,
                       Blend = 2 };

struct MaterialData {
  std::string name;  // qualified "<model-stem>/<source material name>", deduplicated per import
  float baseColourFactor[4]{1.0f, 1.0f, 1.0f, 1.0f};
  float metallicFactor{1.0f};
  float roughnessFactor{1.0f};
  float emissiveFactor[3]{0.0f, 0.0f, 0.0f};
  float normalScale{1.0f};
  float occlusionStrength{1.0f};
  AlphaMode alphaMode{AlphaMode::Opaque};
  float alphaCutoff{0.5f};
  bool doubleSided{false};
  std::vector<TextureRef> textures;
  // Set when the source bound a texture that had to be dropped because it was packed inside the
  // container rather than sitting beside it as a file, and the caller asked for
  // EmbeddedTexturePolicy::Skip. Surfaced so a caller can say so rather than silently presenting
  // the material as untextured.
  bool skippedEmbeddedTexture{false};
};

struct ModelData {
  std::string sourcePath;
  std::vector<MeshData> meshes;
  // Always at least one entry; every mesh's materialIndex is a valid index into it.
  std::vector<MaterialData> materials;
};

}  // namespace modelio
