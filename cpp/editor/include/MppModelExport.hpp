// MppModelExport.hpp — .mppmodel binary export (MPPMODEL_EXPORT_SPEC.md), a from-scratch native
// writer of MassivePolyPusher's model format, targeting byte-for-byte compatibility with what
// mpp::ModelSerializer::load() (ext/massivepolypusher/mpp/src/ModelSerializer.cpp) reads back.
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

#include <string>

#include "Track.hpp"

namespace editor {

struct MppModelExportResult {
  std::string bytes;  // the complete .mppmodel file content; write verbatim (binary mode) to disk
  std::size_t meshCount{0};
};

// One tox::GeometryBatch -> one mppmodel mesh entry, one vertex stream, one index stream (see
// MPPMODEL_EXPORT_SPEC.md 4.4). Materials are referenced by name only (batch.materialKey) and
// never added via addMaterial-equivalent machinery (spec 5, option 1) -- the target
// MassivePolyPusher project is expected to define "road"/"shell"/"rail"/"mesh-region"/
// "zone-<effect>" materials itself.
MppModelExportResult exportTrackToMppModel(const tox::Track& track);

}  // namespace editor
