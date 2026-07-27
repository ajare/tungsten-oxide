#include <mpp/ProgrammaticMaterialStream.h>

#include <core/World.h>

#include <common/GameDefines.h>

#include "WorldRenderer3d.h"


using namespace std;
using namespace wp::application::resourcesystem;

WorldRenderer3d::WorldRenderer3d(ResourcePtr resource, wp::Logger* logger)
	: mRenderer(nullptr)
	, mMaterial(resource)
	, mGlobalTime(0.0f)
	, mwLogger(logger)
{
}

WorldRenderer3d::~WorldRenderer3d()
{
	delete mRenderer;
}

uint32_t WorldRenderer3d::getMeshIndexForMaterialHash(uint64_t hashValue) const
{
	auto worldBatch = mRenderer->getWorldBatch();

	return worldBatch->getMeshIndexForMaterialHash(hashValue);
}

void WorldRenderer3d::create(shared_ptr<WorldTriangle3dDataProvider> dataProvider, bw::core::World const* world, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
{
	mDataProvider = dataProvider;

	// Renderer
	auto materialName = mMaterial->getQualifiedName();

	mRenderer = new RendererType(
		format("World3d_{}_", materialName),
		mDataProvider,
		resourceMgr->getResource(materialName),
		renderSystem,
		resourceMgr,
		world);

	mRenderer->create();

	mDataProvider->setMeshCount(static_pointer_cast<mpp::Model>(mRenderer->getModel())->getNumMeshes());
}

void WorldRenderer3d::addToScene(mpp::ScenePtr scene, bw::core::World const* world)
{
	mSceneModel = scene->add3dModel(mRenderer->getModel());

	auto params = mSceneModel->getParams();

	auto model = static_pointer_cast<mpp::Model>(mSceneModel->getModel());
	auto numMeshes = model->getNumMeshes();
	auto worldBatch = mRenderer->getWorldBatch();

	// Create uniforms for each mesh
	mUniforms.resize(numMeshes, nullptr);

	auto numPrimitives = world->getNumPrimitives();

	for (uint32_t i = 0; i < numPrimitives; ++i)
	{
		auto primitive = world->getPrimitive(i);
		auto const& properties = primitive->getProperties();

		// Floor
		auto hashValue = properties.floorMaterialDef.data.hash(properties.floorMaterialIndex);
		auto meshIndex = worldBatch->getMeshIndexForMaterialHash(hashValue);

		if (mUniforms[meshIndex] == nullptr)
		{
			auto uniforms = make_shared<mpp::UniformCollection>();

			params->setMeshUniforms(worldBatch->formatMeshName(hashValue), uniforms);

			uniforms->setUniform("MATERIAL_INDEX", (int32_t)properties.floorMaterialIndex);
			uniforms->setUniform("MATERIAL_PARAMS", BW_MATERIAL_PARAMS_MAX, 1, properties.floorMaterialDef.data.params.data());
		
			mUniforms[meshIndex] = uniforms;
		}

		// Ceiling
		hashValue = properties.ceilingMaterialDef.data.hash(properties.ceilingMaterialIndex);
		meshIndex = worldBatch->getMeshIndexForMaterialHash(hashValue);

		if (mUniforms[meshIndex] == nullptr)
		{
			auto uniforms = make_shared<mpp::UniformCollection>();

			params->setMeshUniforms(worldBatch->formatMeshName(hashValue), uniforms);

			uniforms->setUniform("MATERIAL_INDEX", (int32_t)properties.ceilingMaterialIndex);
			uniforms->setUniform("MATERIAL_PARAMS", BW_MATERIAL_PARAMS_MAX, 1, properties.ceilingMaterialDef.data.params.data());

			mUniforms[meshIndex] = uniforms;
		}

		// Wall
		hashValue = properties.wallMaterialDef.data.hash(properties.wallMaterialIndex);
		meshIndex = worldBatch->getMeshIndexForMaterialHash(hashValue);

		if (mUniforms[meshIndex] == nullptr)
		{
			auto uniforms = make_shared<mpp::UniformCollection>();

			params->setMeshUniforms(worldBatch->formatMeshName(hashValue), uniforms);

			uniforms->setUniform("MATERIAL_INDEX", (int32_t)properties.wallMaterialIndex);
			uniforms->setUniform("MATERIAL_PARAMS", BW_MATERIAL_PARAMS_MAX, 1, properties.wallMaterialDef.data.params.data());

			mUniforms[meshIndex] = uniforms;
		}
	}
}

void WorldRenderer3d::update(float frameTime)
{
	mGlobalTime += frameTime;

	// Globals
	for (auto uc : mUniforms)
	{
		uc->setUniform("VIEW_DISTANCE", BW_PLAYER_VIEW_DISTANCE);
		uc->setUniform("GLOBAL_TIME", mGlobalTime);
		uc->setUniform("PIXEL_SIZE", 1.0f / 32);
	}

	mRenderer->update();
}