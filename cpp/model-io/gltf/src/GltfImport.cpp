#include "modelio/GltfImport.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace modelio {
namespace {

std::string uniqueName(const std::string& candidate, std::set<std::string>& used) {
  std::string name = candidate.empty() ? "mesh" : candidate;
  if (used.insert(name).second) return name;
  for (int suffix = 1;; ++suffix) {
    std::string attempt = name + "-" + std::to_string(suffix);
    if (used.insert(attempt).second) return attempt;
  }
}

// The 3x3 inverse-transpose, for directions. A node scale that isn't uniform makes the plain
// matrix wrong for normals, and a mirrored (negative-determinant) transform additionally flips
// winding -- handled separately below.
aiMatrix3x3 normalMatrixOf(const aiMatrix4x4& transform) {
  aiMatrix3x3 linear(transform);
  aiMatrix3x3 inverse = linear;
  inverse.Inverse();
  inverse.Transpose();
  return inverse;
}

float determinantOf(const aiMatrix4x4& transform) {
  const aiMatrix3x3 m(transform);
  return m.a1 * (m.b2 * m.c3 - m.b3 * m.c2) - m.a2 * (m.b1 * m.c3 - m.b3 * m.c1) +
         m.a3 * (m.b1 * m.c2 - m.b2 * m.c1);
}

struct SamplerBinding {
  aiTextureType type;
  const char* sampler;
};

// Matches AssImp's own glTF2 importer assignments (code/AssetLib/glTF2/glTF2Importer.cpp): base
// colour lands on BASE_COLOR, metallic-roughness on METALNESS, occlusion on LIGHTMAP.
constexpr SamplerBinding kSamplerBindings[] = {
    {aiTextureType_BASE_COLOR, "PBR_BASE_COLOUR_MAP"},
    {aiTextureType_METALNESS, "PBR_METALLIC_ROUGHNESS_MAP"},
    {aiTextureType_NORMALS, "PBR_NORMAL_MAP"},
    {aiTextureType_LIGHTMAP, "PBR_OCCLUSION_MAP"},
    {aiTextureType_EMISSIVE, "PBR_EMISSIVE_MAP"},
};

// Features that fall outside mpp's PbrMaterialFeature set entirely (docs/adr/0004-gltf-import.md,
// D3). These are properties of the source asset, independent of any target layout, so they are
// caught here at read time rather than in the target-dependent validation pass.
//
// A zero-valued factor means the glTF extension is present but inert (Blender writes several of
// these unconditionally), so each is compared against zero rather than merely tested for presence.
bool reportUnsupportedFeatures(const aiMaterial& source, const std::string& materialName, Report& report) {
  struct ScalarFeature {
    const char* key;
    unsigned int type;
    unsigned int index;
    const char* description;
  };
  const ScalarFeature scalarFeatures[] = {
      {AI_MATKEY_TRANSMISSION_FACTOR, "KHR_materials_transmission"},
      {AI_MATKEY_CLEARCOAT_FACTOR, "KHR_materials_clearcoat"},
      {AI_MATKEY_VOLUME_THICKNESS_FACTOR, "KHR_materials_volume"},
      {AI_MATKEY_ANISOTROPY_FACTOR, "KHR_materials_anisotropy"},
  };

  bool ok = true;
  for (const ScalarFeature& feature : scalarFeatures) {
    float value = 0.0f;
    if (source.Get(feature.key, feature.type, feature.index, value) != AI_SUCCESS || value == 0.0f) continue;
    report.error("material.unsupported-feature",
                 std::string("material uses ") + feature.description +
                     ", which mpp's PBR model cannot express",
                 {}, materialName);
    ok = false;
  }

  aiColor3D sheen;
  if (source.Get(AI_MATKEY_SHEEN_COLOR_FACTOR, sheen) == AI_SUCCESS &&
      (sheen.r != 0.0f || sheen.g != 0.0f || sheen.b != 0.0f)) {
    report.error("material.unsupported-feature",
                 "material uses KHR_materials_sheen, which mpp's PBR model cannot express", {}, materialName);
    ok = false;
  }

  // Spelled literally rather than via AI_MATKEY_GLTF_UNLIT: that macro lives in AssImp's
  // deprecated pbrmaterial.h, while the glTF2 importer writes this exact key directly.
  bool unlit = false;
  if (source.Get("$mat.gltf.unlit", 0, 0, unlit) == AI_SUCCESS && unlit) {
    report.error("material.unsupported-feature",
                 "material uses KHR_materials_unlit, which mpp's PBR model cannot express", {}, materialName);
    ok = false;
  }

  return ok;
}

AlphaMode alphaModeOf(const aiMaterial& material) {
  aiString mode;
  if (material.Get(AI_MATKEY_GLTF_ALPHAMODE, mode) != AI_SUCCESS) return AlphaMode::Opaque;
  const std::string value = mode.C_Str();
  if (value == "MASK") return AlphaMode::Mask;
  if (value == "BLEND") return AlphaMode::Blend;
  return AlphaMode::Opaque;
}

bool readMaterials(const aiScene& scene, const std::filesystem::path& sourceDirectory, const std::string& modelStem,
                   ModelData& model, Report& report) {
  std::set<std::string> usedNames;
  bool ok = true;

  for (unsigned int i = 0; i < scene.mNumMaterials; ++i) {
    const aiMaterial& source = *scene.mMaterials[i];

    aiString sourceName;
    source.Get(AI_MATKEY_NAME, sourceName);
    const std::string bare = sourceName.length > 0 ? sourceName.C_Str() : ("material-" + std::to_string(i));

    MaterialData material;
    material.name = uniqueName(modelStem + "/" + bare, usedNames);

    if (!reportUnsupportedFeatures(source, material.name, report)) ok = false;

    aiColor4D baseColour;
    if (source.Get(AI_MATKEY_BASE_COLOR, baseColour) == AI_SUCCESS ||
        source.Get(AI_MATKEY_COLOR_DIFFUSE, baseColour) == AI_SUCCESS) {
      material.baseColourFactor[0] = baseColour.r;
      material.baseColourFactor[1] = baseColour.g;
      material.baseColourFactor[2] = baseColour.b;
      material.baseColourFactor[3] = baseColour.a;
    }

    float scalar = 0.0f;
    if (source.Get(AI_MATKEY_METALLIC_FACTOR, scalar) == AI_SUCCESS) material.metallicFactor = scalar;
    if (source.Get(AI_MATKEY_ROUGHNESS_FACTOR, scalar) == AI_SUCCESS) material.roughnessFactor = scalar;

    aiColor3D emissive;
    if (source.Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS) {
      material.emissiveFactor[0] = emissive.r;
      material.emissiveFactor[1] = emissive.g;
      material.emissiveFactor[2] = emissive.b;
    }

    int twoSided = 0;
    if (source.Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS) material.doubleSided = twoSided != 0;

    material.alphaMode = alphaModeOf(source);
    if (source.Get(AI_MATKEY_GLTF_ALPHACUTOFF, scalar) == AI_SUCCESS) material.alphaCutoff = scalar;

    for (const SamplerBinding& binding : kSamplerBindings) {
      if (source.GetTextureCount(binding.type) == 0) continue;

      aiString texturePath;
      if (source.GetTexture(binding.type, 0, &texturePath) != AI_SUCCESS) continue;
      const std::string raw = texturePath.C_Str();

      // AssImp reports a container-embedded image as "*<index>"; a glTF may also inline one as a
      // data: URI. Neither can be named by a path from an embedded material.
      if (!raw.empty() && raw[0] == '*') {
        report.error("texture.embedded-in-container",
                     "sampler " + std::string(binding.sampler) +
                         " uses an image packed inside the container; re-export with external images",
                     {}, material.name);
        ok = false;
        continue;
      }
      if (raw.rfind("data:", 0) == 0) {
        report.error("texture.data-uri",
                     "sampler " + std::string(binding.sampler) +
                         " uses an inline data: URI image; re-export with external images",
                     {}, material.name);
        ok = false;
        continue;
      }

      std::filesystem::path resolved(raw);
      if (resolved.is_relative()) resolved = sourceDirectory / resolved;
      material.textures.push_back({binding.sampler, resolved.lexically_normal().string()});
    }

    model.materials.push_back(std::move(material));
  }

  if (model.materials.empty()) {
    MaterialData fallback;
    fallback.name = modelStem + "/default";
    model.materials.push_back(std::move(fallback));
  }

  return ok;
}

void appendMesh(const aiScene& scene, const aiMesh& source, const aiMatrix4x4& transform, const std::string& nodeName,
                std::set<std::string>& usedNames, ModelData& model, Report& report) {
  MeshData mesh;
  mesh.name = uniqueName(nodeName.empty() ? source.mName.C_Str() : nodeName, usedNames);
  mesh.materialIndex =
      static_cast<int>(std::min<std::size_t>(source.mMaterialIndex, model.materials.size() - 1));

  mesh.source.normals = source.HasNormals();
  mesh.source.uvs = source.HasTextureCoords(0);
  mesh.source.colours = source.HasVertexColors(0);
  mesh.source.tangents = source.HasTangentsAndBitangents();

  if (source.GetNumUVChannels() > 1)
    report.warn("mesh.extra-uv-sets",
                "source carries " + std::to_string(source.GetNumUVChannels()) +
                    " UV sets; only the first is converted",
                mesh.name);
  if (source.HasBones())
    report.warn("mesh.skinning-dropped", "source is skinned; joints and weights are not converted", mesh.name);

  const aiMatrix3x3 normalMatrix = normalMatrixOf(transform);

  mesh.vertices.reserve(source.mNumVertices);
  for (unsigned int v = 0; v < source.mNumVertices; ++v) {
    Vertex vertex;

    aiVector3D position = source.mVertices[v];
    position *= transform;
    vertex.position[0] = position.x;
    vertex.position[1] = position.y;
    vertex.position[2] = position.z;

    if (source.HasNormals()) {
      aiVector3D normal = normalMatrix * source.mNormals[v];
      normal.NormalizeSafe();
      vertex.normal[0] = normal.x;
      vertex.normal[1] = normal.y;
      vertex.normal[2] = normal.z;
    }

    if (source.HasTextureCoords(0)) {
      vertex.uv[0] = source.mTextureCoords[0][v].x;
      vertex.uv[1] = source.mTextureCoords[0][v].y;
    }

    if (source.HasVertexColors(0)) {
      const aiColor4D& colour = source.mColors[0][v];
      vertex.colour[0] = static_cast<std::uint8_t>(std::lround(std::clamp(colour.r, 0.0f, 1.0f) * 255.0f));
      vertex.colour[1] = static_cast<std::uint8_t>(std::lround(std::clamp(colour.g, 0.0f, 1.0f) * 255.0f));
      vertex.colour[2] = static_cast<std::uint8_t>(std::lround(std::clamp(colour.b, 0.0f, 1.0f) * 255.0f));
      vertex.colour[3] = static_cast<std::uint8_t>(std::lround(std::clamp(colour.a, 0.0f, 1.0f) * 255.0f));
    }

    if (source.HasTangentsAndBitangents()) {
      aiVector3D tangent = normalMatrix * source.mTangents[v];
      tangent.NormalizeSafe();
      aiVector3D bitangent = normalMatrix * source.mBitangents[v];
      aiVector3D normal(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
      vertex.tangent[0] = tangent.x;
      vertex.tangent[1] = tangent.y;
      vertex.tangent[2] = tangent.z;
      vertex.tangent[3] = (normal ^ tangent) * bitangent < 0.0f ? -1.0f : 1.0f;
    }

    mesh.vertices.push_back(vertex);
  }

  // A mirroring transform reverses triangle winding; flip it back so front faces stay front.
  const bool mirrored = determinantOf(transform) < 0.0f;
  mesh.indices.reserve(static_cast<std::size_t>(source.mNumFaces) * 3);
  for (unsigned int f = 0; f < source.mNumFaces; ++f) {
    const aiFace& face = source.mFaces[f];
    if (face.mNumIndices != 3) continue;  // aiProcess_Triangulate guarantees this; defensive only
    if (mirrored) {
      mesh.indices.push_back(face.mIndices[0]);
      mesh.indices.push_back(face.mIndices[2]);
      mesh.indices.push_back(face.mIndices[1]);
    } else {
      mesh.indices.push_back(face.mIndices[0]);
      mesh.indices.push_back(face.mIndices[1]);
      mesh.indices.push_back(face.mIndices[2]);
    }
  }

  if (mesh.indices.empty()) {
    report.warn("mesh.no-triangles", "mesh contributes no triangles and is skipped", mesh.name);
    return;
  }

  model.meshes.push_back(std::move(mesh));
}

void walkNode(const aiScene& scene, const aiNode& node, const aiMatrix4x4& parentTransform,
              std::set<std::string>& usedNames, ModelData& model, Report& report) {
  const aiMatrix4x4 transform = parentTransform * node.mTransformation;
  const std::string nodeName = node.mName.length > 0 ? node.mName.C_Str() : std::string{};

  for (unsigned int i = 0; i < node.mNumMeshes; ++i) {
    const aiMesh& mesh = *scene.mMeshes[node.mMeshes[i]];

    if (mesh.mPrimitiveTypes != aiPrimitiveType_TRIANGLE) {
      report.warn("mesh.non-triangle-primitive",
                  "mesh uses a non-triangle primitive and is skipped",
                  nodeName.empty() ? mesh.mName.C_Str() : nodeName);
      continue;
    }

    // One node may carry several meshes; qualify by the mesh's own name so they stay distinct.
    std::string name = nodeName;
    if (node.mNumMeshes > 1) {
      const std::string meshName = mesh.mName.length > 0 ? mesh.mName.C_Str() : std::to_string(i);
      name = name.empty() ? meshName : name + "-" + meshName;
    }
    appendMesh(scene, mesh, transform, name, usedNames, model, report);
  }

  for (unsigned int i = 0; i < node.mNumChildren; ++i)
    walkNode(scene, *node.mChildren[i], transform, usedNames, model, report);
}

}  // namespace

std::optional<ModelData> importGltf(const std::filesystem::path& path, const ImportOptions& options, Report& report) {
  Assimp::Importer importer;

  // Deliberately no aiProcess_GenSmoothNormals: normals are generated later, only when absent, so
  // that the synthesis can be reported. Deliberately no aiProcess_PreTransformVertices: see D6.
  unsigned int flags = aiProcess_Triangulate | aiProcess_SortByPType;
  if (options.joinIdenticalVertices) flags |= aiProcess_JoinIdenticalVertices;

  const aiScene* scene = importer.ReadFile(path.string(), flags);
  if (scene == nullptr || scene->mRootNode == nullptr) {
    report.error("source.unreadable",
                 "could not read '" + path.string() + "': " + importer.GetErrorString());
    return std::nullopt;
  }

  ModelData model;
  model.sourcePath = path.string();

  const std::filesystem::path sourceDirectory =
      path.has_parent_path() ? path.parent_path() : std::filesystem::current_path();
  const std::string stem = path.stem().string();

  if (!readMaterials(*scene, sourceDirectory, stem, model, report)) return std::nullopt;

  std::set<std::string> usedNames;
  walkNode(*scene, *scene->mRootNode, aiMatrix4x4(), usedNames, model, report);

  if (model.meshes.empty()) {
    report.error("source.no-meshes", "'" + path.string() + "' contains no triangle mesh data");
    return std::nullopt;
  }

  return model;
}

}  // namespace modelio
