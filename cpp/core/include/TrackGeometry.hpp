#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "Vec3.hpp"

namespace tox {
struct Vec2d {
  double x{0}, y{0};
};
struct Color4 {
  double r{1}, g{1}, b{1}, a{1};
};
struct RenderVertex {
  Vec3 position, normal;
  Vec2d uv;
  Color4 rgba;
};
enum class GeometryKind { PathSurface,
                          PathShell,
                          PathRail,
                          MeshSurface,
                          MeshRail,
                          ZoneSurface };
struct TextureBinding {
  std::string assetId;
  int tile{0};
};
struct GeometryBatch {
  std::string id, materialKey;
  GeometryKind kind{GeometryKind::PathSurface};
  std::vector<RenderVertex> vertices;
  std::vector<std::uint32_t> indices;
  bool hasUv{false};
  std::optional<TextureBinding> texture;
};
}  // namespace tox
