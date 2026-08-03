#include "MppModelImport.hpp"

#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

#include <mpp/ModelSerializer.h>
#include <mpp/ProgrammaticTextureStream.h>
#include <mpp/mesh/Primitive.h>
#include <utils/FileSystem.h>

#include "CollidableFlag.hpp"
#include "TextureLoad.hpp"

namespace modeltool {
namespace {

// mpp::ResourceStreamSerializer::deserialize() (called inside ModelSerializer::readMaterial())
// deliberately does NOT restore a deserialized Texture child's image-load function pointer -- a
// C++ function pointer can't be serialized to disk at all, so ResourceStreamSerializer.cpp's own
// readTextureStream() comment says as much ("Don't read the image load function, this will be
// provided by whatever loads this"). Without this, Resource::load() eventually calls through an
// unset load function when it gets to the texture's actual GL upload -- a silent native crash,
// not a caught C++ exception (this is exactly what mpp::MppModelStream::
// createChildResourceStreamsImpl() re-attaches for its own texture children, via the same
// setImageLoadFunction() call; mirrored here for the deserialized Embedded materials this file
// produces). Relative texture paths are also fixed up against the model file's own directory,
// matching that same precedent, in case some other tool wrote a non-absolute source path (every
// texture path model-tool itself ever writes is already absolute, so this is a no-op for a
// model-tool-authored round-trip).
void reattachTextureLoadFunctions(const mpp::ResourceStreamPtr& materialStream, const std::string& modelFileDir) {
  for (const auto& [name, child] : materialStream->getChildren()) {
    child->setFileBasePaths(modelFileDir);
    if (child->getType() == "Texture") {
      static_cast<mpp::ProgrammaticTextureStream*>(child.get())->setImageLoadFunction(&loadImage);
    }
  }
}

std::vector<ImportedVertex> unpackVertices(const std::int8_t* data, std::size_t vertexCount) {
  std::vector<ImportedVertex> out(vertexCount);
  const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(data);
  for (ImportedVertex& v : out) {
    std::memcpy(&v.px, p, 12);
    p += 12;
    std::memcpy(&v.nx, p, 12);
    p += 12;
    std::memcpy(&v.u, p, 8);
    p += 8;
    v.r = p[0];
    v.g = p[1];
    v.b = p[2];
    v.a = p[3];
    p += 4;
  }
  return out;
}

std::vector<std::uint32_t> unpackIndices(const std::uint8_t* data, std::size_t indexCount, std::size_t indexWidthBits) {
  std::vector<std::uint32_t> out(indexCount);
  const std::size_t bytesPerIndex = indexWidthBits / 8;
  const std::uint8_t* p = data;
  for (std::uint32_t& index : out) {
    if (bytesPerIndex == 2) {
      std::uint16_t narrow;
      std::memcpy(&narrow, p, 2);
      index = narrow;
    } else {
      std::uint32_t wide;
      std::memcpy(&wide, p, 4);
      index = wide;
    }
    p += bytesPerIndex;
  }
  return out;
}

// mpp::ModelSerializer::getIndexData()/getIndexWidth() (ModelSerializer.cpp, both one-liners) do an
// unchecked `mIndexStreams[mMeshes[meshIndex].indexStream]` with no bounds check at all. A mesh
// written with NO index stream -- a real, legitimate shape: cpp/editor's own Track exports
// (MppModelExport.cpp's exportTrackToMppModel) deliberately write an empty IndexData section and
// the sentinel indexStreamId 0xFFFFFFFF for every mesh -- makes that index dereference an
// out-of-bounds vector access against an EMPTY vector: real undefined behavior (observed as an
// access violation, not a thrown exception), not a value we can inspect after the fact. There is no
// public ModelSerializer accessor that exposes a mesh's raw indexStream id safely, so the only way
// to detect this case before it's too late is to read the on-disk MeshMetadata section ourselves,
// mirroring ModelSerializer::readMesh()'s exact byte layout (mpp/src/ModelSerializer.cpp), and never
// call getIndexData()/getIndexWidth() for a mesh whose raw id is that sentinel.
constexpr std::uint32_t kNoIndexStream = 0xFFFFFFFFu;

std::uint32_t readRawU32(std::ifstream& fp) {
  std::uint32_t value = 0;
  fp.read(reinterpret_cast<char*>(&value), sizeof(value));
  if (!fp) throw std::runtime_error("unexpected end of file while reading .mppmodel mesh metadata");
  return value;
}

// Mirrors ModelSerializer::readString(): u32 length prefix, no null terminator on disk.
void skipRawString(std::ifstream& fp) {
  const std::uint32_t len = readRawU32(fp);
  fp.seekg(static_cast<std::streamoff>(len), std::ios::cur);
  if (!fp) throw std::runtime_error("unexpected end of file while reading .mppmodel mesh metadata");
}

// Independently parses just enough of the file to recover each mesh's raw indexStream id (see the
// comment above) -- mirrors ModelSerializer::readHeader()/readDirectory()/readMesh() byte-for-byte,
// but reads nothing else and never touches mMeshes/mIndexStreams.
std::vector<std::uint32_t> readMeshIndexStreamIds(const std::string& utf8Path, std::size_t meshCount) {
  std::ifstream fp(utf8Path, std::ios::binary);
  if (!fp) throw std::runtime_error("could not reopen '" + utf8Path + "' to inspect mesh metadata");

  // Header: 4-byte magic + u16 + u16 + u32 = 12 bytes (readHeader()).
  fp.seekg(12, std::ios::beg);

  // Directory: 6 entries x (type, startOffset, endOffset, count), each field a u32 on disk
  // (readDirectoryEntry()), in order Unused/MaterialNames/Materials/VertexData/IndexData/
  // MeshMetadata -- only the last entry's startOffset is needed.
  std::uint32_t meshMetadataStart = 0;
  for (int entryIndex = 0; entryIndex < 6; ++entryIndex) {
    const std::uint32_t type = readRawU32(fp);
    const std::uint32_t startOffset = readRawU32(fp);
    readRawU32(fp);  // endOffset, unused
    readRawU32(fp);  // count, unused
    if (type == 5) meshMetadataStart = startOffset;  // Directory::Entry::Type::MeshMetadata
  }

  fp.seekg(meshMetadataStart, std::ios::beg);

  std::vector<std::uint32_t> indexStreamIds;
  indexStreamIds.reserve(meshCount);
  for (std::size_t i = 0; i < meshCount; ++i) {
    skipRawString(fp);        // name
    readRawU32(fp);            // primitiveType
    readRawU32(fp);            // primitiveCount
    skipRawString(fp);        // material
    const std::uint32_t numVertexBuffers = readRawU32(fp);
    for (std::uint32_t v = 0; v < numVertexBuffers; ++v) readRawU32(fp);  // vertex buffer ids
    indexStreamIds.push_back(readRawU32(fp));                            // indexStream
  }
  return indexStreamIds;
}

}  // namespace

std::optional<MppModelImportResult> importMppModel(const std::string& utf8Path, mpp::ResourceManager& resourceMgr,
                                                     const MaterialLibrary& materialLibrary, std::string* outError) {
  try {
    mpp::ModelSerializer ser(&resourceMgr);
    ser.load(utf8Path);

    if (ser.getMeshCount() == 0) {
      if (outError) *outError = "the file contains no mesh data";
      return std::nullopt;
    }

    MppModelImportResult result;
    result.model.sourcePath = utf8Path;

    // Every material the file itself declares is created+displayed regardless of whether any
    // mesh currently references it ("if the model has embedded material definitions, create them
    // and display them").
    const std::vector<std::string>& materialNames = ser.getMaterialNames();
    const std::vector<mpp::ResourceStreamPtr>& materialStreams = ser.getMaterials();
    const std::string modelFileDir = utils::FileSystem::baseDirectory(utf8Path);
    std::map<std::string, std::size_t> embeddedIndexByName;
    for (std::size_t i = 0; i < materialNames.size(); ++i) {
      ImportedMaterial material;
      material.name = materialNames[i];
      material.origin = MaterialOrigin::Embedded;
      // texturePath stays nullopt: the deserialized stream already carries its own texture
      // binding internally (see MaterialLibrary::declareModelOwnedFromStream()'s comment) --
      // this app doesn't introspect it back out into a display-able file path.
      reattachTextureLoadFunctions(materialStreams[i], modelFileDir);
      embeddedIndexByName[material.name] = result.model.materials.size();
      result.model.materials.push_back(std::move(material));
      result.embeddedMaterialStreams.push_back(materialStreams[i]);
    }

    std::map<std::string, std::size_t> externalIndexByName;
    std::optional<std::size_t> fallbackIndex;
    std::set<std::string> seenUnresolved;

    const std::size_t meshCount = ser.getMeshCount();
    result.model.meshes.reserve(meshCount);

    // See readMeshIndexStreamIds()'s comment: must be known BEFORE calling getIndexData()/
    // getIndexWidth() below, since those are unsafe to call at all for a mesh with no index stream.
    const std::vector<std::uint32_t> indexStreamIds = readMeshIndexStreamIds(utf8Path, meshCount);

    for (std::size_t i = 0; i < meshCount; ++i) {
      if (ser.getPrimitiveType(i) != mpp::mesh::Primitive::Type::Triangles) {
        if (outError) *outError = "Mesh '" + ser.getName(i) + "' uses a non-triangle primitive type, which this app doesn't support.";
        return std::nullopt;
      }

      // Only vertex stream index 0 is ever read -- this app (like its own MppSave.cpp) never
      // writes more than one vertex stream per mesh, so a well-formed model-tool-authored file
      // never has a second one to miss.
      std::size_t vertexCount = 0, vertexStride = 0;
      std::shared_ptr<const std::int8_t> vertexData;
      ser.getVertexStream(i, 0, &vertexCount, &vertexStride, &vertexData);
      if (vertexStride != 36) {
        if (outError)
          *outError = "Mesh '" + ser.getName(i) + "' uses a " + std::to_string(vertexStride) +
                       "-byte vertex layout; this app only supports its own fixed 36-byte layout.";
        return std::nullopt;
      }

      ImportedMesh mesh;
      // Collidable/decorative flag, round-tripped back out of the exported name (see
      // CollidableFlag.hpp) -- a file this feature never touched decodes as "collidable", the
      // least-surprising default (matches ImportedMesh::collidable's own default).
      const DecodedMeshName decodedName = decodeCollidableFromName(ser.getName(i));
      mesh.name = decodedName.name;
      mesh.collidable = decodedName.collidable;
      mesh.vertices = unpackVertices(vertexData.get(), vertexCount);

      // A mesh written with NO index stream (a real, legitimate shape: cpp/editor's own Track
      // exports, MppModelExport.cpp, deliberately write an empty IndexData section and the
      // sentinel kNoIndexStream id per mesh, since every tox geometry batch is already a triangle
      // soup -- vertices.size() == 3 * triangleCount, indices[k] == k) -- calling
      // getIndexData()/getIndexWidth() at all for such a mesh is undefined behavior (see
      // readMeshIndexStreamIds()'s comment), so the id read independently above is what gates this,
      // never those two accessors. Rather than rejecting the file, synthesize the identity index
      // buffer a non-indexed mesh implies: drawing the vertex buffer straight through is
      // byte-identical to what an explicit 0,1,2,3,... index buffer would produce, and everything
      // downstream of ImportedMesh (buildModel/packVertices/MppSave) already assumes indexed
      // triangles, so this is the only shape that fits without a second, parallel non-indexed code
      // path through the rest of the app.
      if (indexStreamIds[i] == kNoIndexStream) {
        mesh.indices.resize(vertexCount);
        for (std::size_t v = 0; v < vertexCount; ++v) mesh.indices[v] = static_cast<std::uint32_t>(v);
      } else {
        const int indexWidth = ser.getIndexWidth(i);
        if (indexWidth != 16 && indexWidth != 32) {
          if (outError)
            *outError = "Mesh '" + ser.getName(i) + "' has a corrupt index buffer (unexpected index width).";
          return std::nullopt;
        }
        const std::size_t indexCount = static_cast<std::size_t>(ser.getPrimitiveCount(i)) * 3;
        const std::shared_ptr<const std::uint8_t> indexData = ser.getIndexData(i);
        if (!indexData) {
          if (outError) *outError = "Mesh '" + ser.getName(i) + "' has no readable index data.";
          return std::nullopt;
        }
        mesh.indices = unpackIndices(indexData.get(), indexCount, static_cast<std::size_t>(indexWidth));
      }

      const std::string& materialName = ser.getMaterial(i);
      if (const auto embeddedIt = embeddedIndexByName.find(materialName); embeddedIt != embeddedIndexByName.end()) {
        mesh.materialIndex = static_cast<int>(embeddedIt->second);
      } else if (materialLibrary.contains(materialName)) {
        // "otherwise, check that the named materials are loaded and use them" -- a direct lookup,
        // never a name collision/conflict: acquiring a reference to something already loaded can't
        // collide with itself.
        auto externalIt = externalIndexByName.find(materialName);
        if (externalIt == externalIndexByName.end()) {
          ImportedMaterial material;
          material.name = materialName;
          material.origin = MaterialOrigin::ExternalReference;
          material.diffuseTexturePath = materialLibrary.materials().at(materialName).texturePath;
          externalIt = externalIndexByName.emplace(materialName, result.model.materials.size()).first;
          result.model.materials.push_back(std::move(material));
          result.embeddedMaterialStreams.emplace_back();  // no stream for a non-embedded entry
        }
        mesh.materialIndex = static_cast<int>(externalIt->second);
      } else {
        // Neither embedded nor loaded -- one shared DefaultFallback entry for the whole model
        // (every unresolved mesh points at the same entry), with the bare name recorded once per
        // distinct miss for the caller's warning UI.
        if (!fallbackIndex.has_value()) {
          ImportedMaterial material;
          material.name = materialName;
          material.origin = MaterialOrigin::DefaultFallback;
          fallbackIndex = result.model.materials.size();
          result.model.materials.push_back(std::move(material));
          result.embeddedMaterialStreams.emplace_back();
        }
        mesh.materialIndex = static_cast<int>(*fallbackIndex);
        if (seenUnresolved.insert(materialName).second) result.unresolvedMaterialNames.push_back(materialName);
      }

      result.model.meshes.push_back(std::move(mesh));
    }

    return result;
  } catch (const std::exception& error) {
    if (outError) *outError = error.what();
    return std::nullopt;
  }
}

}  // namespace modeltool
