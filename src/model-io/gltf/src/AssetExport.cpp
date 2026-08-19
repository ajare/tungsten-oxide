#include "modelio/AssetExport.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include <assimp/Exporter.hpp>
#include <assimp/GltfMaterial.h>
#include <assimp/material.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>

namespace modelio {
namespace {

// The inverse of AssetImport.cpp's kSamplerBindings: which aiTextureType a ModelData sampler
// name maps to when writing a material property. aiTextureType_METALNESS matches
// glTF2Exporter.cpp's own fallback lookup for the combined metallic-roughness texture.
struct SamplerBinding {
  const char* sampler;
  aiTextureType type;
};
constexpr SamplerBinding kSamplerBindings[] = {
    {"PBR_BASE_COLOUR_MAP", aiTextureType_BASE_COLOR},
    {"PBR_METALLIC_ROUGHNESS_MAP", aiTextureType_METALNESS},
    {"PBR_NORMAL_MAP", aiTextureType_NORMALS},
    {"PBR_OCCLUSION_MAP", aiTextureType_LIGHTMAP},
    {"PBR_EMISSIVE_MAP", aiTextureType_EMISSIVE},
};

aiTextureType textureTypeForSampler(const std::string& sampler) {
  for (const SamplerBinding& binding : kSamplerBindings)
    if (sampler == binding.sampler) return binding.type;
  return aiTextureType_UNKNOWN;
}

// One entry per texture path actually referenced by some material, built once so several
// materials sharing a texture (e.g. the same base-colour map) only embed/copy it once.
struct TextureAsset {
  std::string syntheticName;  // what both the material property and the aiTexture::mFilename hold
  std::filesystem::path sourcePath;
};

std::string extensionOf(const std::filesystem::path& path) {
  std::string ext = path.extension().string();
  if (!ext.empty() && ext.front() == '.') ext.erase(0, 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

// Collects the unique texture paths across every material, assigning each a synthetic
// "textureN.ext" name used consistently as both the aiTexture::mFilename (for .glb embedding) and
// the copied-file name beside the output (for .gltf external references).
std::map<std::string, TextureAsset> collectTextureAssets(const ModelData& model) {
  std::map<std::string, TextureAsset> byPath;
  int next = 0;
  for (const MaterialData& material : model.materials) {
    for (const TextureRef& texture : material.textures) {
      if (byPath.count(texture.path)) continue;
      const std::filesystem::path source(texture.path);
      TextureAsset asset;
      asset.sourcePath = source;
      const std::string ext = extensionOf(source);
      asset.syntheticName = "texture" + std::to_string(next++) + (ext.empty() ? "" : "." + ext);
      byPath.emplace(texture.path, std::move(asset));
    }
  }
  return byPath;
}

void setColourProperty(aiMaterial& material, const float rgba[4]) {
  const aiColor4D colour(rgba[0], rgba[1], rgba[2], rgba[3]);
  material.AddProperty(&colour, 1, AI_MATKEY_BASE_COLOR);
  // AI_MATKEY_COLOR_DIFFUSE too, so non-PBR-aware readers of this same aiScene (there are none in
  // this pipeline today, but it costs nothing) still see a plausible colour.
  material.AddProperty(&colour, 1, AI_MATKEY_COLOR_DIFFUSE);
}

void buildMaterial(aiMaterial& material, const MaterialData& source, const std::map<std::string, TextureAsset>& textureAssets) {
  const aiString name(source.name);
  material.AddProperty(&name, AI_MATKEY_NAME);

  setColourProperty(material, source.baseColourFactor);

  float metallic = source.metallicFactor;
  material.AddProperty(&metallic, 1, AI_MATKEY_METALLIC_FACTOR);
  float roughness = source.roughnessFactor;
  material.AddProperty(&roughness, 1, AI_MATKEY_ROUGHNESS_FACTOR);

  const aiColor3D emissive(source.emissiveFactor[0], source.emissiveFactor[1], source.emissiveFactor[2]);
  material.AddProperty(&emissive, 1, AI_MATKEY_COLOR_EMISSIVE);

  int twoSided = source.doubleSided ? 1 : 0;
  material.AddProperty(&twoSided, 1, AI_MATKEY_TWOSIDED);

  const char* alphaModeStr = source.alphaMode == AlphaMode::Mask ? "MASK" : source.alphaMode == AlphaMode::Blend ? "BLEND"
                                                                                                                 : "OPAQUE";
  const aiString alphaMode(alphaModeStr);
  material.AddProperty(&alphaMode, AI_MATKEY_GLTF_ALPHAMODE);
  float alphaCutoff = source.alphaCutoff;
  material.AddProperty(&alphaCutoff, 1, AI_MATKEY_GLTF_ALPHACUTOFF);

  for (const TextureRef& texture : source.textures) {
    const aiTextureType type = textureTypeForSampler(texture.sampler);
    if (type == aiTextureType_UNKNOWN) continue;
    const auto found = textureAssets.find(texture.path);
    if (found == textureAssets.end()) continue;
    const aiString path(found->second.syntheticName);
    material.AddProperty(&path, AI_MATKEY_TEXTURE(type, 0));
  }
}

void buildMesh(aiMesh& mesh, const MeshData& source) {
  mesh.mName = aiString(source.name);
  mesh.mMaterialIndex = static_cast<unsigned int>(source.materialIndex);
  mesh.mPrimitiveTypes = aiPrimitiveType_TRIANGLE;

  mesh.mNumVertices = static_cast<unsigned int>(source.vertices.size());
  mesh.mVertices = new aiVector3D[mesh.mNumVertices];
  mesh.mNormals = new aiVector3D[mesh.mNumVertices];
  mesh.mTextureCoords[0] = new aiVector3D[mesh.mNumVertices];
  mesh.mNumUVComponents[0] = 2;
  mesh.mColors[0] = new aiColor4D[mesh.mNumVertices];
  mesh.mTangents = new aiVector3D[mesh.mNumVertices];
  mesh.mBitangents = new aiVector3D[mesh.mNumVertices];

  for (unsigned int v = 0; v < mesh.mNumVertices; ++v) {
    const Vertex& vertex = source.vertices[v];
    mesh.mVertices[v] = aiVector3D(vertex.position[0], vertex.position[1], vertex.position[2]);
    mesh.mNormals[v] = aiVector3D(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
    mesh.mTextureCoords[0][v] = aiVector3D(vertex.uv[0], vertex.uv[1], 0.0f);
    mesh.mColors[0][v] = aiColor4D(vertex.colour[0] / 255.0f, vertex.colour[1] / 255.0f, vertex.colour[2] / 255.0f,
                                   vertex.colour[3] / 255.0f);

    const aiVector3D normal(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
    const aiVector3D tangent(vertex.tangent[0], vertex.tangent[1], vertex.tangent[2]);
    mesh.mTangents[v] = tangent;
    mesh.mBitangents[v] = (normal ^ tangent) * vertex.tangent[3];
  }

  mesh.mNumFaces = static_cast<unsigned int>(source.indices.size() / 3);
  mesh.mFaces = new aiFace[mesh.mNumFaces];
  for (unsigned int f = 0; f < mesh.mNumFaces; ++f) {
    aiFace& face = mesh.mFaces[f];
    face.mNumIndices = 3;
    face.mIndices = new unsigned int[3];
    face.mIndices[0] = source.indices[f * 3 + 0];
    face.mIndices[1] = source.indices[f * 3 + 1];
    face.mIndices[2] = source.indices[f * 3 + 2];
  }
}

// Reads a texture file's bytes into an embedded aiTexture, named `asset.syntheticName` so a
// material property carrying that same string resolves via aiScene::GetEmbeddedTexture (matched
// by short filename -- see glTF2Exporter.cpp's GetMatTex). mHeight==0 is AssImp's convention for
// "pcData is a compressed image file's raw bytes, mWidth is the byte count", not a raw pixel array.
bool embedTexture(aiTexture& texture, const TextureAsset& asset, Report& report) {
  std::ifstream file(asset.sourcePath, std::ios::binary);
  if (!file) {
    report.error("export.texture-unreadable", "could not open '" + asset.sourcePath.string() + "' to embed it");
    return false;
  }
  const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (bytes.empty()) {
    report.error("export.texture-unreadable", "'" + asset.sourcePath.string() + "' is empty");
    return false;
  }

  texture.mFilename = aiString(asset.syntheticName);
  texture.mHeight = 0;
  texture.mWidth = static_cast<unsigned int>(bytes.size());
  const std::size_t texelCount = (bytes.size() + sizeof(aiTexel) - 1) / sizeof(aiTexel);
  texture.pcData = new aiTexel[texelCount];
  std::memcpy(texture.pcData, bytes.data(), bytes.size());

  const std::string ext = extensionOf(asset.sourcePath);
  std::memset(texture.achFormatHint, 0, sizeof(texture.achFormatHint));
  const std::size_t copyLength = std::min(ext.size(), sizeof(texture.achFormatHint) - 1);
  std::memcpy(texture.achFormatHint, ext.data(), copyLength);
  return true;
}

}  // namespace

bool exportAsset(const ModelData& model, const std::filesystem::path& outPath, const ExportOptions& options, Report& report) {
  if (model.meshes.empty()) {
    report.error("export.no-meshes", "nothing was selected to export");
    return false;
  }

  const std::map<std::string, TextureAsset> textureAssets = collectTextureAssets(model);

  auto scene = std::make_unique<aiScene>();
  scene->mNumMaterials = static_cast<unsigned int>(model.materials.size());
  scene->mMaterials = new aiMaterial*[scene->mNumMaterials];
  for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
    scene->mMaterials[i] = new aiMaterial();
    buildMaterial(*scene->mMaterials[i], model.materials[i], textureAssets);
  }

  if (options.binary && !textureAssets.empty()) {
    scene->mNumTextures = static_cast<unsigned int>(textureAssets.size());
    scene->mTextures = new aiTexture*[scene->mNumTextures];
    unsigned int i = 0;
    for (const auto& [path, asset] : textureAssets) {
      scene->mTextures[i] = new aiTexture();
      if (!embedTexture(*scene->mTextures[i], asset, report)) return false;
      ++i;
    }
  }

  scene->mNumMeshes = static_cast<unsigned int>(model.meshes.size());
  scene->mMeshes = new aiMesh*[scene->mNumMeshes];
  for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
    scene->mMeshes[i] = new aiMesh();
    buildMesh(*scene->mMeshes[i], model.meshes[i]);
  }

  scene->mRootNode = new aiNode("RootNode");
  scene->mRootNode->mNumMeshes = scene->mNumMeshes;
  scene->mRootNode->mMeshes = new unsigned int[scene->mNumMeshes];
  for (unsigned int i = 0; i < scene->mNumMeshes; ++i) scene->mRootNode->mMeshes[i] = i;

  scene->mFlags = 0;

  Assimp::Exporter exporter;
  std::error_code ec;
  if (outPath.has_parent_path()) std::filesystem::create_directories(outPath.parent_path(), ec);

  const aiReturn result = exporter.Export(scene.get(), options.binary ? "glb2" : "gltf2", outPath.string());
  if (result != aiReturn_SUCCESS) {
    report.error("export.failed", std::string("AssImp could not write '") + outPath.string() + "': " + exporter.GetErrorString());
    std::filesystem::remove(outPath, ec);
    return false;
  }

  // Non-embedded (text .gltf) textures are referenced by the synthetic name alone; copy the real
  // file to sit beside the output under that name so the reference resolves.
  if (!options.binary) {
    for (const auto& [path, asset] : textureAssets) {
      const std::filesystem::path destination = outPath.parent_path() / asset.syntheticName;
      std::error_code copyEc;
      std::filesystem::copy_file(asset.sourcePath, destination, std::filesystem::copy_options::overwrite_existing, copyEc);
      if (copyEc) {
        report.warn("export.texture-copy-failed",
                    "could not copy '" + asset.sourcePath.string() + "' to '" + destination.string() + "': " + copyEc.message());
      }
    }
  }

  return true;
}

}  // namespace modelio
