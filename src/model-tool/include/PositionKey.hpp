// PositionKey.hpp — a quantized 3D position used as a merge key wherever two vertices from
// independently-authored data (different ImportedMesh entries, or a raw OBJ file re-parsed
// alongside AssImp's own processed output) need to be recognized as "the same" position despite
// being bit-for-bit-different floats/doubles. Shared by NormalSmoothing.cpp (merging vertices for
// normal averaging) and ObjSmoothingGroups.cpp (matching AssImp's triangulated output back to the
// original file's faces) so both use exactly one quantization scale -- a mismatch between the two
// would silently break the OBJ face-matching lookup.
#pragma once

#include <cmath>
#include <cstdint>
#include <tuple>

namespace modeltool {

// 1e-4 world units is far finer than any real modeling seam, so this never merges two genuinely
// distinct positions in practice, while still absorbing ordinary floating-point round-trip noise.
constexpr double kPositionQuantizeScale = 10000.0;

struct PositionKey {
  std::int64_t x, y, z;
  bool operator<(const PositionKey& other) const { return std::tie(x, y, z) < std::tie(other.x, other.y, other.z); }
  bool operator==(const PositionKey& other) const { return x == other.x && y == other.y && z == other.z; }
};

inline PositionKey quantizePosition(double x, double y, double z) {
  return {std::llround(x * kPositionQuantizeScale), std::llround(y * kPositionQuantizeScale), std::llround(z * kPositionQuantizeScale)};
}

}  // namespace modeltool
