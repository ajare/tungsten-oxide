#pragma once

#include <memory>
#include <map>

#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>
#include <mpp/UniformCollection.h>

#include <willpower/application/resourcesystem/Resource.h>

#include <willpower/common/Logger.h>

#include "WorldBatchRenderer.h"
#include "WorldTriangle3dDataProvider.h"


class WorldRenderer3d
{
	wp::application::resourcesystem::ResourcePtr mMaterial;

	mpp::SceneModel3dPtr mSceneModel;

	std::vector<std::shared_ptr<mpp::UniformCollection>> mUniforms;

	float mGlobalTime;

	wp::Logger* mwLogger;

public:

	typedef WorldBatchRenderer RendererType;

private:

	RendererType* mRenderer;

	std::shared_ptr<WorldTriangle3dDataProvider> mDataProvider;

public:

	WorldRenderer3d(wp::application::resourcesystem::ResourcePtr resource, wp::Logger* logger);

	virtual ~WorldRenderer3d();

	uint32_t getMeshIndexForMaterialHash(uint64_t hashValue) const;

	void create(std::shared_ptr< WorldTriangle3dDataProvider> dataProvider, bw::core::World const* world, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr);

	void addToScene(mpp::ScenePtr scene, bw::core::World const* world);

	void update(float frameTime);
};
