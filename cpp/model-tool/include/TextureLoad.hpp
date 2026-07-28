// TextureLoad.hpp — a mpp::ImageLoadFunction backed by stb_image, for
// mpp::ProgrammaticTextureStream::setFile(). model-tool only ever loads externally file-referenced
// diffuse/base-color textures (embedded textures are skipped at import time, see AssImpImport.hpp)
// -- there is no existing stb_image+mpp combination anywhere else in this codebase (cpp/editor uses
// stb_image but never links mpp; ext/massivepolypusher/demo-suite's loadImage uses FreeImage
// instead, a dependency not otherwise used or vendored under cpp/), so this is new, self-contained
// glue rather than a port of either precedent.
#pragma once

#include <mpp/TextureStream.h>

namespace modeltool {

// Decodes `filename` (any format stb_image supports -- PNG/JPG/etc.) into a `TextureData` whose
// `data` is heap-allocated with `new[]`, matching what `mpp::TextureStream` expects to own and
// later `delete[]` -- stb_image's own malloc'd buffer is copied and freed internally, not handed
// out directly, to avoid a new[]/malloc allocator mismatch. Always requested as 4-channel RGBA8
// regardless of the source file's real channel count, so every loaded texture matches a single
// GL_RGBA/GL_UNSIGNED_BYTE format. On decode failure, returns a 1x1 opaque-white TextureData
// instead of a null/empty one, so a bad texture reference degrades to a blank material rather than
// crashing whatever uploads it.
mpp::TextureData loadImage(std::string const& filename);

}  // namespace modeltool
