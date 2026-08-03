// CollidableFlag.hpp — encodes a sub-mesh's collidable/decorative flag directly into its exported
// .mppmodel mesh name (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 4.1/4.3): mpp::ModelSerializer's
// MeshMetadata section (name/material/primitiveType only, see ModelSerializer.h) has no free-form
// per-mesh flag field to carry this in instead, and there is no lightweight way to add one without
// changing the binary format every other .mppmodel writer/reader in this codebase already depends
// on. A naming convention is one of the two options DRIVABLE_MESH_OBJECTS_PLAN.md's Milestone 4.3
// step explicitly allows.
//
// Collidable is the common case (see AssImpImport.hpp's ImportedMesh::collidable default), so only
// a DECORATIVE sub-mesh's exported name carries the marker -- an ordinary collidable mesh's name is
// written completely unchanged, which also means a .mppmodel exported before this feature existed
// (or by any other tool) reads back as "every mesh collidable", the least-surprising default.
#pragma once

#include <string>

namespace modeltool {

// Distinct enough that no real modeling tool's own naming convention should ever produce it by
// accident; not validated/escaped beyond that (a name that already happens to end with this exact
// suffix would be misread on export -- an accepted, documented limitation, not a real modeling
// convention in the wild).
inline constexpr char kDecorativeMeshNameSuffix[] = "~decorative";

// Returns `name` unchanged when `collidable` is true; otherwise appends the decorative marker.
std::string encodeCollidableInName(const std::string& name, bool collidable);

struct DecodedMeshName {
  std::string name;  // marker stripped, if it was present
  bool collidable;
};

// Splits the marker back off a name read from a .mppmodel file. `collidable` is true (and `name`
// returned verbatim) whenever the marker isn't present -- including every mesh from a file this
// feature never touched.
DecodedMeshName decodeCollidableFromName(const std::string& name);

}  // namespace modeltool
