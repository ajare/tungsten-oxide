#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include <mpp/ModelSerializer.h>
#include <mpp/ProgrammaticModelStream.h>

#include <utils/FileSystem.h>

#include <applib/ModelInstance.h>
#include <applib/PbrMaterialBinding.h>
#include <applib/TrackMaterial.h>

#include <willpower/application/resourcesystem/DirectoryResourceLocation.h>
#include <willpower/application/resourcesystem/MaterialResource.h>
#include <willpower/application/resourcesystem/ResourceExceptions.h>

#include "Map.h"
#include "PbrMeshSpecification.h"
#include "PbrVertexConversion.h"
#include "Simulation.hpp"
#include "StartGrid.hpp"
#include "TrackCollisionBuild.h"
#include "TungstenMonoxideModel.h"

using namespace std;
using namespace wp;

namespace {

string resolveLegacyMaterialMppName(Map* map, string const& materialKey) {
  auto dependent = map->getDependentResource("Legacy." + materialKey);
  if (dependent->getType() == "TrackMaterial")
    return static_cast<applib::TrackMaterial*>(dependent.get())->getMaterial()->getQualifiedName();
  if (dependent->getType() == "Material") return dependent->getQualifiedName();
  throw application::resourcesystem::ResourceException(
      map, "legacy material '" + materialKey + "' is a '" + dependent->getType() +
               "' resource, expected TrackMaterial or Material.");
}

string resolveMaterialMppName(Map* map, string const& materialKey) {
  auto dependent = map->getDependentResource(materialKey);
  if (dependent->getType() == "PbrMaterialBinding") {
    auto binding = static_cast<applib::PbrMaterialBinding*>(dependent.get())->getBinding();
    auto model = dynamic_cast<TungstenMonoxideModel*>(applib::ModelInstance::get());
    if (!model || !model->pbrPackage)
      throw application::resourcesystem::ResourceException(
          map, "material '" + materialKey + "' uses PBR binding '" + binding + "', but the package service is unavailable.");
    try {
      return model->pbrPackage->resolveMaterial(binding).resourceName;
    } catch (exception const& error) {
      throw application::resourcesystem::ResourceException(
          map, "material '" + materialKey + "' could not resolve PBR binding '" + binding + "': " + error.what());
    }
  }
  if (dependent->getType() == "TrackMaterial")
    return static_cast<applib::TrackMaterial*>(dependent.get())->getMaterial()->getQualifiedName();
  if (dependent->getType() == "Material") return dependent->getQualifiedName();
  throw application::resourcesystem::ResourceException(
      map, "material '" + materialKey + "' is a '" + dependent->getType() +
               "' resource, expected PbrMaterialBinding, TrackMaterial, or Material.");
}

// --- Drivable mesh object placements (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 3) ---------------
//
// core never loads or compiles a placement's referenced .mppmodel -- Track::definition.meshObjects
// carries only authored data (modelId + 6-DOF transform), per the plan's "`.mppmodel` loading is
// host-only" architecture note. This is the one place it's actually resolved: `modelId` is a
// filename relative to the same directory-based resource location as the track's own ModelFile/
// TrackData (resolved via mono::safeRelativePath, exactly like those two), not a declared
// <DependentResource> -- there's no need for the generic named-resource-dependency machinery
// materials use (that exists to support cross-Map GPU resource sharing/refcounting; a placement's
// model is loaded fresh per Map::load() instead, cached only within that one call by modelId so
// multiple placements sharing one model parse it once, matching the plan's own 3.3 wording).
//
// The actual BVH-triangle-building logic (buildCollisionTriangles, buildMeshObjectCollisionTriangles,
// safeRelativePath, the placement transforms, MeshObjectModel/loadMeshObjectModel) lives in
// TrackCollisionBuild.h/.cpp, shared with cpp/app/tools/mesh_physics_diag.cpp (Milestone 6.0's
// headless diagnostic tool) so both build the exact same collision surface. Everything below here
// wraps mono::'s plain std::runtime_error into this class's own ResourceException, and handles the
// render-mesh-append pass that only this DLL needs.

// Rendering (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 3.4): every sub-mesh of every placement --
// collidable AND decorative, unlike buildMeshObjectCollisionTriangles above; a decorative sub-mesh
// still renders, it just isn't in the BVH -- expanded from the referenced model's real indexed
// triangles into the same flat non-indexed PBR layout the road's own meshes use, so it can go into
// the same `modelStream` as ordinary model instancing. Position/normal are transformed by the
// placement; uv/colour pass through unchanged and tangent4 is regenerated after expansion.
// Mesh names are namespaced by placement id ("meshobject-<placement id>-<sub-mesh name>") so
// multiple placements sharing one model, or a sub-mesh name that happens to collide with one of the
// road's own mesh names, never collide in the single shared `modelStream`.
void appendMeshObjectRenderMeshes(Map* map, tox::Track const& track, filesystem::path const& root, mpp::ResourceManager* resourceMgr,
                                  mpp::mesh::MeshSpecification const& meshSpec, mpp::ProgrammaticModelStream* modelStream, bool pbr,
                                  std::map<string, shared_ptr<mono::MeshObjectModel>>& cache, vector<mono::EmbeddedModelRef> const& embeddedModels) {
  for (auto const& placement : track.definition.meshObjects) {
    auto cached = cache.find(placement.modelId);
    if (cached == cache.end()) {
      try {
        filesystem::path const modelPath =
            mono::safeRelativePath(root, mono::resolveModelFileReference(placement.modelId, embeddedModels), "modelId");
        cached = cache.emplace(placement.modelId, mono::loadMeshObjectModel(resourceMgr, modelPath)).first;
      } catch (exception const& error) {
        throw application::resourcesystem::ResourceException(map, error.what());
      }
    }
    mono::MeshObjectModel& model = *cached->second;

    for (size_t meshIndex = 0; meshIndex < model.serializer.getMeshCount(); ++meshIndex) {
      if (model.serializer.getPrimitiveType(meshIndex) != mpp::mesh::Primitive::Type::Triangles) continue;

      string const rawName = model.serializer.getName(meshIndex);
      // Visible=false is game-hidden (unlike the editor's viewport, which renders it regardless --
      // TRACK_MODEL_LIST_PLAN.md's locked-in semantics). Unknown metadata defaults to visible,
      // matching model-tool's own "no XML metadata yet" default.
      mono::ModelMeshMeta const* meta = mono::findMeshMeta(placement.modelId, rawName, embeddedModels);
      if (meta != nullptr && !meta->visible) continue;

      string materialMppName;
      try {
        materialMppName = pbr ? resolveMaterialMppName(map, model.serializer.getMaterial(meshIndex))
                              : resolveLegacyMaterialMppName(map, model.serializer.getMaterial(meshIndex));
      } catch (exception const& error) {
        map->warn("Map '" + map->getQualifiedName() + "': skipping drivable mesh object sub-mesh '" + rawName + "' (placement '" + placement.id +
                  "'): " + error.what());
        continue;
      }

      size_t vertexCount, stride;
      shared_ptr<const int8_t> data;
      model.serializer.getVertexStream(meshIndex, 0, &vertexCount, &stride, &data);
      if (stride != 36) {
        map->warn("Map '" + map->getQualifiedName() + "': skipping drivable mesh object sub-mesh '" + rawName + "' (placement '" + placement.id +
                  "'): unsupported vertex stride.");
        continue;
      }

      // Transform once per source vertex (not per expanded triangle corner) -- cheaper, and keeps
      // this a direct mirror of buildMeshObjectCollisionTriangles's own per-vertex transform pass.
      vector<int8_t> transformed(vertexCount * stride);
      for (size_t v = 0; v < vertexCount; ++v) {
        auto const src = data.get() + v * stride;
        tox::Vec3 const worldPos =
            mono::placementTransformPosition(placement, tox::Vec3(mono::readFloat(src, 0), mono::readFloat(src, 4), mono::readFloat(src, 8)));
        tox::Vec3 const worldNormal = tox::normalizeSafe(
            mono::placementTransformNormal(placement, tox::Vec3(mono::readFloat(src, 12), mono::readFloat(src, 16), mono::readFloat(src, 20))));
        auto dst = transformed.data() + v * stride;
        float const posF[3] = {static_cast<float>(worldPos.x), static_cast<float>(worldPos.y), static_cast<float>(worldPos.z)};
        float const normalF[3] = {static_cast<float>(worldNormal.x), static_cast<float>(worldNormal.y), static_cast<float>(worldNormal.z)};
        memcpy(dst, posF, 12);
        memcpy(dst + 12, normalF, 12);
        memcpy(dst + 24, src + 24, 12);  // uv (8 bytes) + colour (4 bytes), unchanged
      }

      vector<uint32_t> indices;
      if (model.indexStreamIds[meshIndex] == mono::kMeshObjectNoIndexStream) {
        indices.resize(vertexCount);
        for (size_t v = 0; v < vertexCount; ++v) indices[v] = static_cast<uint32_t>(v);
      } else {
        int const indexWidth = model.serializer.getIndexWidth(meshIndex);
        size_t const indexCount = static_cast<size_t>(model.serializer.getPrimitiveCount(meshIndex)) * 3;
        shared_ptr<const uint8_t> const indexData = model.serializer.getIndexData(meshIndex);
        indices.resize(indexCount);
        uint8_t const* p = indexData.get();
        size_t const bytesPerIndex = static_cast<size_t>(indexWidth) / 8;
        for (size_t k = 0; k < indexCount; ++k) {
          uint32_t index = 0;
          memcpy(&index, p, bytesPerIndex);
          indices[k] = index;
          p += bytesPerIndex;
        }
      }

      // Expand indexed -> flat non-indexed (the track specification is non-indexed,
      // matching every other mesh already in this modelStream).
      vector<int8_t> flat(indices.size() * stride);
      for (size_t k = 0; k < indices.size(); ++k) memcpy(flat.data() + k * stride, transformed.data() + static_cast<size_t>(indices[k]) * stride, stride);
      vector<int8_t> outputVertices = std::move(flat);
      if (pbr) {
        try {
          outputVertices = mono::addPbrTangentsToFlatTriangles(outputVertices, indices.size());
        } catch (exception const& error) {
          throw application::resourcesystem::ResourceException(
              map, "drivable mesh object sub-mesh '" + rawName + "' could not generate PBR tangents: " + error.what());
        }
      }

      string const meshName = "meshobject-" + placement.id + "-" + rawName;
      auto meshId = modelStream->createMesh(meshName, meshSpec, materialMppName, 16);
      modelStream->addVertexData(meshId, outputVertices);
    }
  }
}

}  // namespace

Map::Map(string const& name, string const& namesp, string const& source,
         map<string, string> const& tags, application::resourcesystem::ResourceLocation* location,
         wp::Logger* logger)
    : applib::Map(name, namesp, source, tags, location, 512), mwLogger(logger) {
}

Map::~Map() = default;

void Map::warn(string const& message) {
  mwLogger->warn(message);
  mLoadWarnings.push_back(message);
}

bool Map::load(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) {
  WP_UNUSED(renderSystem);
  mLoadWarnings.clear();
  auto directoryLocation = dynamic_cast<application::resourcesystem::DirectoryResourceLocation*>(mwLocation);
  if (directoryLocation == nullptr)
    throw application::resourcesystem::ResourceException(this, "Track resources require a directory-based resource location.");

  filesystem::path root(directoryLocation->getRootPath());
  filesystem::path modelPath, dataPath;
  try {
    modelPath = mono::safeRelativePath(root, mModelFileName, "ModelFile");
    dataPath = mono::safeRelativePath(root, mTrackDataFileName, "TrackData");
  } catch (exception const& error) {
    throw application::resourcesystem::ResourceException(this, error.what());
  }

  // Track::fromTrackDataFiles (TRACK_MODEL_LIST_PLAN.md Milestone 1.2) rather than fromFile: only
  // one Track-type Model is currently supported (see MapTungstenMonoxideDefinitionFactory.cpp's own
  // "more than one Type=Track Model" guard), so this is a single-element call today -- byte-identical
  // to fromFile -- but telegraphs that a future multi-Model union is this same entry point's job, not
  // a new one.
  tox::TrackLoadResult loaded = tox::Track::fromTrackDataFiles({dataPath});
  if (!loaded)
    throw application::resourcesystem::ResourceException(this, "failed to load TrackData '" + dataPath.string() + "': " + loaded.error);
  for (auto const& warning : loaded.warnings)
    warn("Track '" + getQualifiedName() + "' [" + warning.code + "] " + warning.objectId + ": " + warning.message);
  mTrack = make_shared<tox::Track>(std::move(*loaded.track));

  mpp::ModelSerializer serializer(resourceMgr);
  try {
    serializer.load(modelPath.string());
  } catch (exception const& error) {
    throw application::resourcesystem::ResourceException(this, "failed to load ModelFile '" + modelPath.string() + "': " + error.what());
  }

  // Collidable-mesh selection is derived straight from the baked Track (same as
  // cpp/app/tools/mesh_physics_diag.cpp's own buildCollisionSurface()) rather than read from XML --
  // the old flat <TrackMeshes> list this replaces was always required to equal this exact set anyway
  // (see TrackCollisionBuild.cpp's git history), so deriving it loses no real validation: the
  // dedicated per-mesh-name/vertex-data cross-check against the physical .mppmodel content, just
  // below, still fires independently of where the name list came from.
  vector<string> const selectedNames = mono::collidableGeometryBatchIds(*mTrack);
  vector<tox::CollisionTriangle> collisionTriangles;
  int nextSurfaceId = static_cast<int>(selectedNames.size());
  map<string, shared_ptr<mono::MeshObjectModel>> modelCache;
  try {
    collisionTriangles = mono::buildCollisionTriangles(serializer, *mTrack, selectedNames);
    // Drivable mesh object placements (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 3.3, now Type=Physical-
    // driven -- TRACK_MODEL_LIST_PLAN.md Milestone 7): merged into the same BVH as the road's own
    // collision triangles above, each collidable sub-mesh transformed by its placement. `modelCache`
    // is scoped to this one load() call -- nothing outside it needs a loaded placement model, so
    // there's no reason to keep it (or the mpp::ModelSerializer instances it owns) alive any longer
    // than building this BVH takes.
    auto meshObjectTriangles = mono::buildMeshObjectCollisionTriangles(*mTrack, root, resourceMgr, nextSurfaceId, modelCache, mEmbeddedModels);
    collisionTriangles.insert(collisionTriangles.end(), make_move_iterator(meshObjectTriangles.begin()), make_move_iterator(meshObjectTriangles.end()));
  } catch (exception const& error) {
    throw application::resourcesystem::ResourceException(this, error.what());
  }
  if (collisionTriangles.empty())
    throw application::resourcesystem::ResourceException(
        this,
        "this track has no drivable/collidable geometry and cannot be loaded; re-export it from "
        "the editor after adding a path or mesh region.");

  mTrack->collisionSurface = make_shared<tox::TrackCollisionSurface>(std::move(collisionTriangles));

  tox::Simulation simulation(*mTrack);
  mStartGridPoses = tox::StartGrid::startingGridPoses(simulation, *mTrack, tox::StartGrid::DEFAULT_SHIP_COUNT);
  if (mStartGridPoses.size() != tox::StartGrid::DEFAULT_SHIP_COUNT)
    throw application::resourcesystem::ResourceException(this, "could not generate all eight starting-grid poses from TrackData.");
  for (tox::Pose& pose : mStartGridPoses) {
    auto contact = mTrack->collisionSurface->nearestAlongAxis(pose.pos, pose.up, 4.0);
    if (!contact)
      throw application::resourcesystem::ResourceException(this, "a starting-grid pose could not be settled onto TrackMeshes.");
    pose.pos = contact->position;
    pose.up = contact->normal;
    pose.forward += pose.up * -glm::dot(pose.forward, pose.up);
    if (glm::dot(pose.forward, pose.forward) < 1e-9)
      throw application::resourcesystem::ResourceException(this, "a starting-grid pose has a degenerate forward direction.");
    pose.forward = tox::normalizeSafe(pose.forward);
  }

  auto buildModelStream = [&](bool pbr) {
    auto meshSpec = mono::gameMeshSpecification(false, pbr);
    auto modelStream = new mpp::ProgrammaticModelStream(resourceMgr);
    for (size_t i = 0; i < serializer.getMeshCount(); ++i) {
      string materialMppName;
      try {
        materialMppName = pbr ? resolveMaterialMppName(this, serializer.getMaterial(i))
                              : resolveLegacyMaterialMppName(this, serializer.getMaterial(i));
      } catch (exception const& error) {
        if (find(selectedNames.begin(), selectedNames.end(), serializer.getName(i)) != selectedNames.end())
          throw application::resourcesystem::ResourceException(
              this, "listed track mesh '" + serializer.getName(i) + "' has unresolved material: " + error.what());
        warn("Map '" + getQualifiedName() + "': skipping mesh '" + serializer.getName(i) + "': " + error.what());
        continue;
      }

      auto meshId = modelStream->createMesh(serializer.getName(i), meshSpec, materialMppName, 16);
      size_t vertexCount, vertexStride;
      shared_ptr<const int8_t> vertexData;
      serializer.getVertexStream(i, 0, &vertexCount, &vertexStride, &vertexData);
      if (vertexStride != mono::LegacyPbrVertexStride)
        throw application::resourcesystem::ResourceException(this, "mesh '" + serializer.getName(i) + "' has an unsupported vertex stride.");
      vector<int8_t> bytes(vertexData.get(), vertexData.get() + vertexCount * vertexStride);
      if (pbr) {
        try {
          bytes = mono::addPbrTangentsToFlatTriangles(bytes, vertexCount);
        } catch (exception const& error) {
          throw application::resourcesystem::ResourceException(
              this, "mesh '" + serializer.getName(i) + "' could not generate PBR tangents: " + error.what());
        }
      }
      modelStream->addVertexData(meshId, bytes);
    }

    // Drivable mesh object placements reuse `modelCache` from the collision-mesh pass above, so a
    // model already loaded there isn't reopened from disk here.
    appendMeshObjectRenderMeshes(this, *mTrack, root, resourceMgr, meshSpec, modelStream, pbr, modelCache, mEmbeddedModels);
    return modelStream;
  };

  // Stage the package-backed PBR model now. The legacy Play pass continues to consume mMppResource
  // until the render-pipeline migration switches presentation in Milestone 5.
  mPbrMppResource =
      resourceMgr->declareResource(getQualifiedName() + ".Pbr", mpp::ResourceStreamPtr(buildModelStream(true))).first;
  mPbrMppResource->acquire(this);
  mMppResource = resourceMgr->declareResource(getQualifiedName(), mpp::ResourceStreamPtr(buildModelStream(false))).first;
  mMppResource->acquire(this);
  // Do not call mMppResource->load() here. Map resources can be created on the
  // threaded Willpower loading worker, which has no OpenGL context. The map
  // load state's main-thread MPP phase loads every referenced render resource.
  return true;
}

bool Map::unload(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) {
  WP_UNUSED(renderSystem);
  WP_UNUSED(resourceMgr);
  if (mMppResource) mMppResource->release(this);
  if (mPbrMppResource) mPbrMppResource->release(this);
  mPbrMppResource.reset();
  mTrack.reset();
  mStartGridPoses.clear();
  return true;
}
