// MppSave.hpp — saves a BuiltModel out as a real .mppmodel file via mpp::ModelSerializer directly
// (not a from-scratch byte writer like cpp/editor's MppModelExport.cpp -- that avoided linking
// mpp::ModelSerializer specifically to dodge a GLEW-vs-gl3w GL-loader conflict that doesn't apply
// to model-tool, which links real mpp fully anyway for its viewport). See
// docs/adr/0001-model-tool.md, D5.
//
// Materials are referenced by name only -- MaterialNames/Materials stay empty, exactly like
// cpp/editor's own MppModelExport.cpp (MPPMODEL_EXPORT_SPEC.md 5, option 1). This is a deliberate
// change from an earlier version of this file, which embedded real ProgrammaticBasicMaterialStream
// objects via ModelSerializer::addMaterial(): that round-trip through mpp::ResourceStreamSerializer
// turned out to have several independent bugs once actually exercised (a directory-offset
// miscalculation, a corrupted string-length prefix, a missing re-attached texture-load function, and
// a uniform count/size mismatch causing a native crash) -- none of that machinery is invoked at all
// once materials are referenced by name instead of embedded in the binary. Meshes still keep their
// real AssImp shared-vertex indexing rather than being flattened to a non-indexed triangle soup,
// matching MPPMODEL_EXPORT_SPEC.md 4.2.
//
// TRACK_MODEL_LIST_PLAN.md's own follow-up: the companion Resources.xml-shaped materials fragment
// this file used to describe (ModelResourceExport.hpp, since deleted) is retired -- the saved
// .mppmodel's material references are no longer separately declared by model-tool at all, matching
// how a Track resource already worked (its materials must already be loaded/authored in the
// consuming project, per cpp/editor/src/MppModelExport.cpp's own comment). Reopening a model-tool-
// saved .mppmodel with nothing else done still shows every mesh's material as unresolved (default
// white, with a warning) until matching materials are loaded some other way (e.g. "Import Materials
// XML..." against an unrelated, hand-authored Resources.xml).
#pragma once

#include <string>

#include "MaterialLibrary.hpp"
#include "ModelResources.hpp"

namespace modeltool {

// Returns true on success; on failure, fills `outError` and leaves no partial file behind
// (ModelSerializer::save() throws on a write failure, which this catches and reports). Fails if
// any of `built`'s referenced materials is no longer loaded in `materialLibrary` (shouldn't happen
// in practice -- BuiltModel::materialRefs holds a live reference to each one for exactly this
// reason -- but is checked rather than assumed, since the exported name must still resolve to
// something real for the companion XML to describe accurately).
bool saveModelAsMppModel(const BuiltModel& built, MaterialLibrary& materialLibrary, const std::string& utf8Path, std::string* outError);

}  // namespace modeltool
