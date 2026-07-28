#include "AssImpImport.hpp"

#include <filesystem>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace modeltool {
namespace {

std::uint8_t normalizedByte(float c) {
  const float clamped = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
  return static_cast<std::uint8_t>(clamped * 255.0f + 0.5f);
}

ImportedMesh convertMesh(const aiMesh& mesh) {
  ImportedMesh out;
  out.name = mesh.mName.length > 0 ? mesh.mName.C_Str() : "mesh";
  out.materialIndex = static_cast<int>(mesh.mMaterialIndex);

  out.vertices.resize(mesh.mNumVertices);
  const bool hasColours = mesh.HasVertexColors(0);
  const bool hasUv = mesh.HasTextureCoords(0);
  for (unsigned int i = 0; i < mesh.mNumVertices; ++i) {
    ImportedVertex& v = out.vertices[i];
    const aiVector3D& p = mesh.mVertices[i];
    v.px = p.x;
    v.py = p.y;
    v.pz = p.z;
    if (mesh.HasNormals()) {
      const aiVector3D& n = mesh.mNormals[i];
      v.nx = n.x;
      v.ny = n.y;
      v.nz = n.z;
    }
    if (hasUv) {
      const aiVector3D& uv = mesh.mTextureCoords[0][i];
      v.u = uv.x;
      v.v = uv.y;
    }
    if (hasColours) {
      const aiColor4D& c = mesh.mColors[0][i];
      v.r = normalizedByte(c.r);
      v.g = normalizedByte(c.g);
      v.b = normalizedByte(c.b);
      v.a = normalizedByte(c.a);
    }
  }

  out.indices.reserve(static_cast<std::size_t>(mesh.mNumFaces) * 3);
  for (unsigned int f = 0; f < mesh.mNumFaces; ++f) {
    const aiFace& face = mesh.mFaces[f];
    // aiProcess_Triangulate guarantees exactly 3 indices/face; skip a degenerate 0/1/2-index face
    // rather than reading out of bounds if some importer plugin ever violates that.
    if (face.mNumIndices != 3) continue;
    out.indices.push_back(face.mIndices[0]);
    out.indices.push_back(face.mIndices[1]);
    out.indices.push_back(face.mIndices[2]);
  }
  return out;
}

// Diffuse/base-color texture only (ADR 0001 D4) -- aiTextureType_DIFFUSE first (the classic slot
// every format maps *something* to), falling back to aiTextureType_BASE_COLOR for glTF/PBR-style
// materials that only set the newer slot.
ImportedMaterial convertMaterial(const aiScene& scene, const aiMaterial& material, const std::filesystem::path& sourceDir) {
  ImportedMaterial out;
  aiString name;
  out.name = (material.Get(AI_MATKEY_NAME, name) == AI_SUCCESS && name.length > 0) ? name.C_Str() : "material";

  aiTextureType type = aiTextureType_DIFFUSE;
  if (material.GetTextureCount(type) == 0) type = aiTextureType_BASE_COLOR;
  if (material.GetTextureCount(type) == 0) return out;

  aiString texPath;
  if (material.GetTexture(type, 0, &texPath) != AI_SUCCESS || texPath.length == 0) return out;

  // Embedded textures (common in .glb) are referenced either by the "*<index>" convention or, for
  // some formats, by a filename AssImp can still resolve to an aiTexture via GetEmbeddedTexture --
  // either way, skip them for v1 (ADR 0001 D4) rather than decoding in-memory image data.
  if (texPath.C_Str()[0] == '*' || scene.GetEmbeddedTexture(texPath.C_Str()) != nullptr) {
    out.skippedEmbeddedTexture = true;
    return out;
  }

  std::filesystem::path resolved(texPath.C_Str());
  if (resolved.is_relative()) resolved = sourceDir / resolved;
  out.diffuseTexturePath = resolved.lexically_normal().string();
  return out;
}

}  // namespace

std::optional<ImportedModel> importModel(const std::string& utf8Path, std::string* outError) {
  Assimp::Importer importer;
  // Triangulate: every face becomes a triangle (ModelSerializer/mppmodel only ever deals in
  // triangles). JoinIdenticalVertices: real shared-vertex indexing, not a per-face vertex soup
  // (ADR 0001 D5 -- model-tool deliberately keeps real indices, unlike track geometry's soup).
  // GenSmoothNormals: only fills in normals for meshes that don't already have any. Pre
  // TransformVertices: bakes node-hierarchy transforms into vertex data, since .mppmodel has no
  // node concept to preserve them in otherwise (ADR 0001 D3) -- a real gap in ModelConvert's own
  // AssImpModelLoader precedent, which never applies node transforms at all.
  constexpr unsigned int flags = static_cast<unsigned int>(aiProcess_Triangulate) | aiProcess_JoinIdenticalVertices |
                                  aiProcess_GenSmoothNormals | aiProcess_PreTransformVertices;
  const aiScene* scene = importer.ReadFile(utf8Path, flags);
  if (scene == nullptr || scene->mRootNode == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0) {
    if (outError) *outError = importer.GetErrorString();
    return std::nullopt;
  }
  if (scene->mNumMeshes == 0) {
    if (outError) *outError = "the file contains no mesh data";
    return std::nullopt;
  }

  ImportedModel out;
  out.sourcePath = utf8Path;
  const std::filesystem::path sourceDir = std::filesystem::path(utf8Path).parent_path();

  out.materials.reserve(scene->mNumMaterials);
  for (unsigned int i = 0; i < scene->mNumMaterials; ++i) out.materials.push_back(convertMaterial(*scene, *scene->mMaterials[i], sourceDir));
  // ModelResources.cpp always resolves a mesh's material by indexing ImportedModel::materials, so
  // there must always be at least one entry (a scene with literally no materials still has meshes
  // to draw) -- an untextured default (ADR 0001 D7's "__mpp_tex_none__" sentinel gets applied
  // downstream whenever diffuseTexturePath is nullopt, which a default-constructed
  // ImportedMaterial already satisfies).
  if (out.materials.empty()) out.materials.push_back(ImportedMaterial{"default", std::nullopt, false});

  out.meshes.reserve(scene->mNumMeshes);
  for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
    ImportedMesh mesh = convertMesh(*scene->mMeshes[i]);
    // Guard against a mesh whose materialIndex a malformed file leaves out of range -- fall back
    // to the first (guaranteed-present) material rather than indexing out of bounds later.
    if (mesh.materialIndex < 0 || static_cast<std::size_t>(mesh.materialIndex) >= out.materials.size()) mesh.materialIndex = 0;
    out.meshes.push_back(std::move(mesh));
  }

  return out;
}

}  // namespace modeltool
