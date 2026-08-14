// ExportAll.hpp — track_editor's "Export All..." action: baked track meshes + placed "other 3D
// objects" -> a single modelio::ModelData, ready for modelio::exportAsset (glTF/GLB).
//
// Scope (docs/GLTF_IMPORT_PLAN.md's "no export" decision, reversed for this feature):
//   - Track meshes: baked tox::GeometryBatch entries of kind PathSurface/PathShell/PathRail --
//     the only kinds TrackBake.cpp actually emits real triangle data for. MeshSurface/MeshRail/
//     ReservationWall are declared but never emitted (dead, from the removed MeshRegion system);
//     ZoneSurface/TriggerSurface are the visual halo of zones/triggers, excluded along with
//     triggers and path splines per the feature's own scope.
//   - Placed objects: TrackDefinition::meshObjects (placements), each referencing an embedded
//     TrackDefinition::models entry's .mppmodel, read directly via mpp::ModelSerializer (through
//     modelio::readMppModel) -- the same file the editor's own viewport already reads for
//     rendering, just with materials resolved this time too.
//
// Material fidelity is intentionally uneven, not an oversight: a placement mesh whose .mppmodel
// carries an *embedded* PbrMaterial gets full round-trip fidelity (via modelio::
// readEmbeddedPbrMaterial); everything else -- track path materials (MaterialCatalog is an
// editor-authoring/preview reader with texture paths but no PBR scalar factors) and placements
// bound to a material *by name* -- gets a name plus whatever texture MaterialCatalog already
// tracks, since neither is resolvable to real PBR values without the game's runtime package,
// which track_editor does not link.
#pragma once

#include <string>
#include <vector>

#include "modelio/Diagnostics.hpp"
#include "modelio/ModelData.hpp"

#include "EditorState.hpp"
#include "MaterialCatalog.hpp"
#include "Track.hpp"

namespace editor {

struct ExportableItem {
  enum class Kind { TrackMesh,
                    Placement };
  Kind kind{Kind::TrackMesh};
  std::string label;  // for the checklist row
  // Index into `baked.geometry` (TrackMesh) or `track().meshObjects` (Placement) -- see
  // collectExportableItems/buildExportModelData, which always share one such list.
  std::size_t index{0};
};

// One entry per exportable track-mesh geometry batch, in `baked.geometry` order, followed by one
// entry per placement, in `track.meshObjects` order.
std::vector<ExportableItem> collectExportableItems(const TrackDefinition& track, const tox::Track& baked);

// Builds one modelio::ModelData from every item in `items` whose matching `checked` slot is true.
// `modelBaseDir` resolves a placement's ModelFile reference exactly as TopDownCanvas.cpp's own
// loadCachedPlacementGeometry does (relative to the track's save directory, or used as-is when the
// reference is already absolute). Never throws; a placement whose model can't be read is skipped
// with a warning rather than failing the whole export.
modelio::ModelData buildExportModelData(const EditorState& state, const tox::Track& baked,
                                        const std::vector<ExportableItem>& items, const std::vector<bool>& checked,
                                        const MaterialCatalog& materialCatalog, const std::filesystem::path& modelBaseDir,
                                        modelio::Report& report);

}  // namespace editor
