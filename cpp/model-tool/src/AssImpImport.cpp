#include "AssImpImport.hpp"

#include <filesystem>
#include <set>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "NormalSmoothing.hpp"

namespace modeltool {
namespace {

// Deliberately not std::filesystem::path::stem() -- utf8Path is UTF-8, and on Windows
// std::filesystem::path built from a narrow std::string is interpreted through the system ANSI
// codepage, silently mangling non-ASCII text (the same class of bug FileDialog.hpp's pathToUtf8
// exists to avoid). '/'/'\\'/'.' are all single ASCII bytes that never appear as part of a UTF-8
// multi-byte sequence, so plain byte-wise scanning is safe here.
std::string utf8FileStem(const std::string& utf8Path) {
  const std::size_t slash = utf8Path.find_last_of("/\\");
  const std::string filename = slash == std::string::npos ? utf8Path : utf8Path.substr(slash + 1);
  const std::size_t dot = filename.find_last_of('.');
  return dot == std::string::npos ? filename : filename.substr(0, dot);
}

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
  // No GenSmoothNormals: normals are always recomputed from winding order after import instead
  // (recomputeSmoothNormalsAcrossMeshes below, DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 4.2) --
  // the source file's own normals (real or AssImp-per-mesh-synthesized) are never trusted, and
  // per-mesh smoothing wouldn't be continuous across a multi-sub-mesh model's own internal seams
  // anyway. PreTransformVertices: bakes node-hierarchy transforms into vertex data, since
  // .mppmodel has no node concept to preserve them in otherwise (ADR 0001 D3) -- a real gap in
  // ModelConvert's own AssImpModelLoader precedent, which never applies node transforms at all.
  constexpr unsigned int flags =
      static_cast<unsigned int>(aiProcess_Triangulate) | aiProcess_JoinIdenticalVertices | aiProcess_PreTransformVertices;
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

  // Qualify every material's raw AssImp name into a MaterialLibrary key: "<model-filename-stem>/
  // <raw-name>", deduplicated within this one import (AssImp commonly hands back generic/repeated
  // names like "material" or "" across multiple materials in one file). This keeps two unrelated
  // models that each happen to have a material literally called "material" from colliding with
  // each other in MaterialLibrary's single shared namespace; re-importing the exact same file will
  // still collide with itself (by design -- see MaterialLibrary.hpp's Replace/Ignore handling).
  const std::string modelStem = utf8FileStem(utf8Path);
  std::set<std::string> seenNames;
  for (ImportedMaterial& material : out.materials) {
    const std::string base = modelStem + "/" + material.name;
    std::string candidate = base;
    while (!seenNames.insert(candidate).second) candidate += "_";
    material.name = candidate;
  }

  out.meshes.reserve(scene->mNumMeshes);
  for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
    ImportedMesh mesh = convertMesh(*scene->mMeshes[i]);
    // Guard against a mesh whose materialIndex a malformed file leaves out of range -- fall back
    // to the first (guaranteed-present) material rather than indexing out of bounds later.
    if (mesh.materialIndex < 0 || static_cast<std::size_t>(mesh.materialIndex) >= out.materials.size()) mesh.materialIndex = 0;
    out.meshes.push_back(std::move(mesh));
  }

  // AssImp commonly synthesizes an implicit placeholder material (e.g. a "DefaultMaterial" with
  // no texture) for meshes that don't explicitly reference one, in addition to whatever real
  // materials the scene actually declares -- if nothing ends up using it, don't create/display/
  // declare it at all (matches ModelResourceExport.cpp's own "only export materials actually
  // referenced" rule, just applied here at import time instead of at export time).
  std::vector<bool> referenced(out.materials.size(), false);
  for (const ImportedMesh& mesh : out.meshes) referenced[static_cast<std::size_t>(mesh.materialIndex)] = true;

  std::vector<ImportedMaterial> prunedMaterials;
  std::vector<int> remappedIndex(out.materials.size(), -1);
  for (std::size_t i = 0; i < out.materials.size(); ++i) {
    if (!referenced[i]) continue;
    remappedIndex[i] = static_cast<int>(prunedMaterials.size());
    prunedMaterials.push_back(std::move(out.materials[i]));
  }
  out.materials = std::move(prunedMaterials);
  for (ImportedMesh& mesh : out.meshes) mesh.materialIndex = remappedIndex[static_cast<std::size_t>(mesh.materialIndex)];

  recomputeSmoothNormalsAcrossMeshes(out);

  return out;
}

}  // namespace modeltool
