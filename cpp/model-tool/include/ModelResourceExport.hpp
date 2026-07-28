// ModelResourceExport.hpp — builds a Resources.xml-shaped fragment declaring one Image + Material
// resource pair per material at least one mesh in the model actually references (NOT necessarily
// every ImportedModel::materials entry -- a .mppmodel's own embedded materials are all created and
// displayed regardless of use per MppModelImport.hpp, but nothing unused belongs in what a model
// exports), written beside the saved .mppmodel (see main.cpp's Save As flow). Mirrors
// cpp/editor/src/MppModelExport.cpp's buildTrackResourceXml,
// which does the analogous thing for a Track's exported .mppmodel -- and, like that export
// (MPPMODEL_EXPORT_SPEC.md 5, option 1), materials are referenced by name only in the saved
// .mppmodel binary itself (see MppSave.hpp): this XML fragment is the ONLY place a saved model's
// materials are actually declared/described. This also sidesteps mpp::ResourceStreamSerializer's
// save/load round-trip entirely, which turned out to have several independent bugs when actually
// exercised (a directory-offset miscalculation, a corrupted string-length prefix, a missing
// re-attached texture-load function, and a uniform count/size mismatch) -- none of that machinery
// is invoked at all once materials are described here instead of embedded in the binary.
//
// Every declared Material references mpp::RenderSystem's built-in core program
// ("__mpp_p3d_tris_p3n3t2c4__") by name rather than declaring a <Resource type="Program"> of its
// own -- see ModelResources.hpp's header comment: that program is registered by
// RenderSystem::createCoreResources() in every process that constructs a RenderSystem (this app
// included), the same way mpp's other "__mpp_*__" sentinels are, so no XML Program declaration is
// needed or possible (nothing in the willpower resource-factory system builds one from XML). A
// material with no real texture (ADR 0001 D7) references mpp's other built-in sentinel,
// "__mpp_tex_none__", likewise requiring no Image declaration.
#pragma once

#include <string>

#include "AssImpImport.hpp"

namespace modeltool {

// Each ImportedMaterial::name is already a fully qualified MaterialLibrary key ("namespace/leaf",
// or just "leaf" for an unnamespaced one -- see AssImpImport.cpp/MppModelImport.cpp) by the time
// this runs, so the qualified name is split back into (namespace, leaf) per material and grouped
// into one <Namespace> block per distinct namespace (plus a flat, unwrapped group for any
// unnamespaced entries) -- this is NOT a single namespace wrapping every resource, unlike an
// earlier version of this function. `defaultFallbackMaterialName` is the resource name a
// DefaultFallback-origin material entry (MaterialOrigin) actually resolves to at render time (see
// MaterialLibrary::defaultFallbackMaterial()) -- used in place of that entry's own `name` (which
// otherwise holds the original, never-actually-resolved bare material name for display purposes
// only), so the exported XML always matches what the saved .mppmodel's own mesh.material fields
// reference.
std::string buildModelMaterialsXml(const ImportedModel& model, const std::string& defaultFallbackMaterialName);

}  // namespace modeltool
