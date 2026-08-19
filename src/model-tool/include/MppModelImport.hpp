// MppModelImport.hpp — imports an existing .mppmodel file (as opposed to AssImpImport.hpp's OBJ/
// FBX/USD/glTF path), producing the SAME ImportedModel shape AssImp import does. That's what lets
// "Save As .mppmodel..." round-trip a model that was itself opened via this path (see MppSave.hpp)
// -- both paths feed the exact same buildModel()/packVertices()/MppSave machinery.
//
// Built directly on mpp::ModelSerializer (NOT mpp::MppModelStream, mpp's own ".mppmodel -> live
// resource" wrapper): MppModelStream's own material resolution
// (MetadataReader::getMaterialByMeshId) unconditionally throws if a mesh's material name isn't in
// the file's own embedded Materials section, which can't express "fall back to an already-loaded
// catalog, then to a warning + default white" -- see src/tungsten-monoxide/src/Map.cpp for the
// existing precedent of hand-rolling against ModelSerializer directly for the same reason.
//
// Strictness: only vertex streams laid out EXACTLY as this app's own fixed 36-byte layout
// (position f32x3 + normal f32x3 + uv f32x2 + colour u8x4) and Triangles-primitive meshes are
// supported -- a mismatched mesh fails the whole import with a clear error rather than attempting
// a conversion, since nothing in this app's rendering/save pipeline could interpret a different
// layout anyway. A mesh with more than one vertex stream silently uses only the first (this app,
// like its own MppSave.cpp, never writes more than one per mesh).
//
// Per mesh, its material name is resolved into exactly one of the three MaterialOrigin values
// (AssImpImport.hpp): every material declared in the file's own Materials section becomes an
// Embedded entry in the returned model (created+displayed regardless of whether any mesh
// currently references it -- "if the model has embedded material definitions, create them and
// display them"); a mesh whose material name ISN'T in that set, but IS already loaded in
// `materialLibrary`, gets an ExternalReference entry; otherwise it gets model-tool's shared
// DefaultFallback entry, and its bare name is recorded in `unresolvedMaterialNames` for the
// caller's warning UI.
//
// This function does NOT touch MaterialLibrary itself (only reads it, via contains()/materials()):
// declaring the Embedded materials (and acquiring references to the ExternalReference ones) is
// main.cpp's job, deferred behind the same Replace/Ignore conflict modal every other import uses
// (see MaterialLibrary.hpp) -- importMppModel() only classifies, it never commits anything.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <mpp/ResourceStream.h>

#include "AssImpImport.hpp"
#include "MaterialLibrary.hpp"

namespace mpp {
class ResourceManager;
}  // namespace mpp

namespace modeltool {

struct MppModelImportResult {
  ImportedModel model;
  // Parallel to model.materials -- non-null only for Embedded entries (the deserialized, not-yet-
  // declared ResourceStreamPtr mpp::ModelSerializer::readMaterial() reconstructed for it; see
  // MaterialLibrary::declareModelOwnedFromStream()).
  std::vector<mpp::ResourceStreamPtr> embeddedMaterialStreams;
  // Bare material names (deduplicated) that were neither embedded nor already loaded -- every
  // mesh referencing one of these got model.materials' single DefaultFallback entry instead.
  std::vector<std::string> unresolvedMaterialNames;
};

// `resourceMgr` must be the same ResourceManager `materialLibrary` was constructed against (the
// deserialized material streams are bound to it). Returns nullopt and fills `outError` on any
// failure: unreadable/malformed file, a mesh whose vertex stream isn't exactly 36 bytes/vertex, or
// a non-Triangles mesh.
std::optional<MppModelImportResult> importMppModel(const std::string& utf8Path, mpp::ResourceManager& resourceMgr,
                                                     const MaterialLibrary& materialLibrary, std::string* outError);

}  // namespace modeltool
