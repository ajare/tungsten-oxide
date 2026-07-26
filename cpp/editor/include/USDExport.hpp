// USDExport.hpp — ASCII USD (.usda) export (EDITOR_CPP_PORT_PLAN.md M7a), a from-scratch native
// counterpart to web/js/usd-export.js.
//
// Deliberately NOT a port of usd-export.js's implementation: that module re-derives road/shell
// surface geometry itself, straight from TrackCore's centerline/edge/cross-section math (adaptive
// breakpoints, ring stitching, shell extrusion...), because in the browser that was the only baked
// geometry available to export from. Here, core already bakes exactly this into
// tox::Track::geometry (GeometryBatch: positions/normals/uvs/indices, semantic materialKey) for
// exactly this kind of consumption -- see cpp/README.md's "renderer-neutral indexed batches". This
// exporter just walks those batches into USD Mesh prims, one Material per unique materialKey. Any
// future divergence between this and usd-export.js's output is therefore a core-baking difference,
// not an independent reimplementation drifting out of sync.
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
