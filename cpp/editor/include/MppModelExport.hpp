// MppModelExport.hpp — .mppmodel binary export (MPPMODEL_EXPORT_SPEC.md), a from-scratch native
// writer of MassivePolyPusher's model format, targeting byte-for-byte compatibility with what
// mpp::ModelSerializer::load() (ext/massive-poly-pusher/mpp/src/ModelSerializer.cpp) reads back.
//
// Deliberately does NOT link mpp::ModelSerializer itself: mpp/include/mpp/ModelSerializer.h
// unconditionally includes <glew/glew.h>/<gl/gl.h> and transitively drags in
// mpp/ResourceManager.h -> mpp/RenderSystem.h (MassivePolyPusher's whole OpenGL rendering/
// resource-management subsystem) just to compile the header, even though ModelSerializer::save()
// itself never calls a GL function. Linking that in would mean adding GLEW as a second GL loader
// alongside cpp/editor's existing gl3w (real duplicate-symbol risk) and compiling/linking a large
// slice of mpp's resource subsystem just to satisfy the linker for code paths (writeMaterial) this
// exporter never exercises. This writer instead emits the documented binary layout directly (see
// MPPMODEL_EXPORT_SPEC.md 2.1) using only tox::GeometryBatch, verified field-for-field against
// ModelSerializer.cpp's write*/read* pairs -- no new build dependency, no GL-loader risk.
//
// One correction versus what ModelSerializer::save() actually does on disk: its
// updateDirectoryEntry() seeks to `sizeof(Header) + type * sizeof(Directory::Entry)` to backpatch
// each directory entry after writing that section, but writeDirectoryEntry() only ever writes 16
// bytes (4x uint32_t) per entry -- while sizeof(Directory::Entry) is 32 (size_t-sized
// start/end/count fields, confirmed by compiling a standalone probe against the same struct
// layout: sizeof(Header)==12, sizeof(Directory::Entry)==32). That backpatch therefore seeks to the
// wrong byte offset for every entry after the first (Unused, type 0, where 0*anything==0 hides the
// bug), corrupting the file it just wrote. The *read* path (readDirectory/readDirectoryEntry) has
// no such bug -- it just reads six 16-byte entries sequentially, no seeking -- so a file whose
// directory is simply computed and written correctly up front (this writer builds each section
// into memory first, so every offset/count is known before anything is written, with no
// backpatching needed at all) loads back correctly despite that latent upstream bug.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "EditorTrackDefinition.hpp"
#include "ModelXml.hpp"
#include "Ship.hpp"
#include "Track.hpp"

namespace editor {

struct MppModelExportResult {
  std::string bytes;  // the complete .mppmodel file content; write verbatim (binary mode) to disk
  std::size_t meshCount{0};
};

// One tox::GeometryBatch -> one mppmodel mesh entry and one vertex stream. GeometryBatch is an
// already-unshared triangle soup, so no redundant identity index stream is written (see
// MPPMODEL_EXPORT_SPEC.md 4.4). Materials are referenced by name only (batch.materialKey) and
// never added via addMaterial-equivalent machinery (spec 5, option 1) -- the target
// MassivePolyPusher project is expected to define "road"/"rail"/"mesh-region" materials itself,
// plus Tracks/DefaultShellMaterial, Tracks/DefaultZoneMaterial, and Tracks/DefaultTriggerMaterial
// for shells/zones/triggers (all declared in Resources.xml).
//
// `trackMaterialToMaterial` maps an editor-facing track material choice (what path.material/
// batch.materialKey holds for path-surface geometry -- see MaterialsPanel.cpp) to the stable
// `PbrMaterialBinding` dependency key in MaterialCatalog::MaterialEntry::materialQualifiedName.
// A mesh's material reference is written as that resolved key whenever batch.materialKey has an
// entry here. Fixed rail/mesh/shell/zone/trigger keys and an empty/legacy "road" literal pass
// through unchanged. Defaults to empty (no resolution, matching this function's
// old behavior) so existing self-check call sites that don't have a MaterialCatalog handy still
// compile unchanged.
MppModelExportResult exportTrackToMppModel(const tox::Track& track,
                                           const std::map<std::string, std::string>& trackMaterialToMaterial = {});

// Builds a standalone Willpower <Resources> XML document (same schema as
// cpp/tungsten-monoxide/resources/Resources.xml) declaring this track as a `type="Track"`
// resource, listing -- by namespace-qualified ref, not re-declaring -- every Material this
// track's curves are actually assigned to (path.material resolved through
// `trackMaterialToMaterial`, deduped -- see exportTrackToMppModel's comment on why: the
// dependency must match whatever the exported mesh's own material reference resolves to), plus
// the fixed rail/mesh/shell/zone/trigger materials every track's export always depends on
// (Tracks/DefaultRailMaterial, Tracks/DefaultMeshMaterial, Tracks/DefaultShellMaterial,
// Tracks/DefaultZoneMaterial, Tracks/DefaultTriggerMaterial -- must stay in sync with
// cpp/core/src/TrackBake.cpp's and TrackMesh.cpp's hardcoded materialKey strings). Meant to be
// merged into the game's real Resources.xml, where matching PbrMaterialBinding resources are
// expected to already be declared. This file only emits <DependentResource ref="..."> entries,
// never package material definitions.
//
// The <Definition>'s <Models> list (TRACK_MODEL_LIST_PLAN.md) always has exactly one entry this
// function regenerates fresh -- the primary Track-type Model, `primaryModelId` (id attribute) with
// `mppModelFileName`/`trackDataFileName` as its <ModelFile>/<TrackData>, and a <Meshes> entry
// (Type=Track, Visible=true) for every baked PathSurface/MeshSurface/ReservationWall/PathRail/
// MeshRail batch id -- everything a ship can physically contact, i.e. Map.cpp's gameplayKind() set
// (PathShell stays out -- render-only). This supersedes the old flat <TrackMeshes> list entirely
// (Milestone 7 migrates the host to read it this way instead). `otherModels` are additional
// Physical/Decorative <Model> entries (Milestone 6's "Load Model") written back verbatim via
// modelxml::writeModelFragment, completely unowned/unedited by this function.
// All paths are resource-directory relative. Starting-grid poses are deliberately not duplicated
// here: Map::load regenerates and triangle-settles the fixed eight-slot grid from TrackData.
//
// Save/load integration keeps Resource@name stable even when TrackDefinition::name (JSON metadata)
// changes, so this always takes the resource identity independently rather than deriving it from
// `track.name`.
std::string buildTrackResourceXmlForName(const TrackDefinition& track, const tox::Track& bakedTrack,
                                         const std::string& resourceName, const std::string& mppModelFileName,
                                         const std::string& trackDataFileName,
                                         const std::map<std::string, std::string>& trackMaterialToMaterial,
                                         const std::string& primaryModelId,
                                         const std::vector<modelxml::ModelXmlDefinition>& otherModels = {});

}  // namespace editor
