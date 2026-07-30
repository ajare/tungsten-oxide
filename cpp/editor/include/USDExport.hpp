// USDExport.hpp — ASCII USD (.usda) export.
//
// Does not re-derive road/shell surface geometry itself: core already bakes exactly that into
// tox::Track::geometry (GeometryBatch: positions/normals/uvs/indices, semantic materialKey) for
// exactly this kind of consumption -- see cpp/README.md's "renderer-neutral indexed batches". This
// exporter just walks those batches into USD Mesh prims, one Material per unique materialKey.
#pragma once

#include <string>
#include <vector>

#include "Track.hpp"

namespace editor {

struct USDExportResult {
  std::string text;
  std::size_t meshCount{0};
};

USDExportResult exportTrackToUSDA(const tox::Track& track);

}  // namespace editor
