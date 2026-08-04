// ObjSmoothingGroups.hpp — recovers OBJ's authored smoothing-group ids (`s <n>` / `s off`),
// something AssImp's public `aiMesh` API does not expose for ANY format (verified empirically:
// aiMesh carries no smoothing-related field at all, and enabling aiProcess_GenSmoothNormals smooths
// across an authored group boundary anyway, since it's a fixed crease-angle heuristic, not
// group-aware -- see docs/model-tool.md's "Normal recomputation" section). This bypasses AssImp for
// this one piece of data by re-parsing the raw `.obj` text file directly, alongside AssImp's own
// import.
//
// Only OBJ gets this treatment: it's a documented, simple-to-parse plaintext format. FBX/glTF/USD
// have no equivalent public-API-free path available without a much larger investment (a binary/
// format-specific parser), so they -- and a `.mppmodel` reimport, whose format has no
// smoothing-group concept at all -- fall back to NormalSmoothing.hpp's per-mesh mode instead.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "AssImpImport.hpp"
#include "NormalSmoothing.hpp"

namespace modeltool {

// Returns nullopt when `utf8Path` doesn't have a `.obj` extension, or when the file couldn't be
// re-read as text (AssImp already loaded it successfully by the time this runs, so a read failure
// here just means no group data recovered -- never a reason to fail the whole import).
//
// Matches AssImp's post-Triangulate/-JoinIdenticalVertices/-PreTransformVertices output back to the
// original file's `f` declarations by vertex POSITION, not by face declaration order (order isn't
// guaranteed stable across those steps; positions are copied through them untouched). A `.obj`
// face's smoothing group is whatever `s` value was most recently set before it in the file; `s off`
// or `s 0` gets a unique synthetic group per such face, so it never smooths with anything, matching
// OBJ's own "no smoothing" semantics for that face.
std::optional<std::vector<MeshTriangleGroups>> extractObjSmoothingGroups(const std::string& utf8Path, const ImportedModel& model);

}  // namespace modeltool
