// MppSave.hpp — saves a BuiltModel out as a real .mppmodel file via mpp::ModelSerializer directly
// (not a from-scratch byte writer like cpp/editor's MppModelExport.cpp -- that avoided linking
// mpp::ModelSerializer specifically to dodge a GLEW-vs-gl3w GL-loader conflict that doesn't apply
// to model-tool, which links real mpp fully anyway for its viewport). See
// docs/adr/0001-model-tool.md, D5.
//
// Unlike the track exporter, real materials are embedded (ModelSerializer::addMaterial() with the
// same live ProgrammaticMaterialStream objects the viewport renders with) and meshes keep their
// real AssImp shared-vertex indexing rather than being flattened to a non-indexed triangle soup.
#pragma once

#include <string>

#include "ModelResources.hpp"

namespace modeltool {

// Returns true on success; on failure, fills `outError` and leaves no partial file behind
// (ModelSerializer::save() throws on a write failure, which this catches and reports).
bool saveModelAsMppModel(const BuiltModel& built, const std::string& utf8Path, std::string* outError);

}  // namespace modeltool
