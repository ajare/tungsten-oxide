// ModelResourceExport.hpp — builds a Resources.xml-shaped fragment declaring one Image + Material
// resource pair per ImportedModel::materials entry, written beside the saved .mppmodel (see
// main.cpp's Save As flow). Mirrors cpp/editor/src/MppModelExport.cpp's buildTrackResourceXml,
// which does the analogous thing for a Track's exported .mppmodel.
//
// Unlike Track's export (MPPMODEL_EXPORT_SPEC.md 5, option 1: materials are referenced by name
// only, authored separately in the consuming project's own Resources.xml), model-tool actually
// knows each material's real texture, so this writes complete, self-contained Material
// declarations rather than bare <DependentResource> refs to externally-authored ones.
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

// `namespaceName` becomes the <Namespace name="..."> wrapping every declared resource -- callers
// typically pass the saved .mppmodel's own filename stem, keeping resource names stable and
// collision-free across repeated exports of different models into the same Resources.xml-style
// catalog. Material/Image resource names are derived from ImportedMaterial::name, deduplicated
// with a numeric suffix when two materials share a name (or when a name is empty).
std::string buildModelMaterialsXml(const ImportedModel& model, const std::string& namespaceName);

}  // namespace modeltool
