#pragma once

#include <map>
#include <vector>
#include <string>

#include <mpp/Scene.h>
#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>

#include <willpower/application/resourcesystem/ResourceManager.h>

#include <core/World.h>

#include "WorldTriangle3dDataProvider.h"
#include "WorldRenderer3d.h"


class WorldRenderer
{
	typedef std::shared_ptr<WorldTriangle3dDataProvider> DataProvider;

	typedef std::shared_ptr<WorldRenderer3d> Renderer;

	typedef std::pair<Renderer, DataProvider> MaterialRenderer;

private:

	std::vector<MaterialRenderer> mMaterialRenderers;

	bool mWorldHasChanged;

#ifdef BW_PROFILING_BUILD
	int64_t mUpdateDataProviderTimeStampNs, mBuildRendererMeshDataTimeStampNs;
#endif

	wp::Logger* mwLogger;

private:

	void updateDataProviders(bw::core::World* world, bw::core::WorldData const& worldData, float frameTime);

	void addVertexToDataProvider(DataProvider dataProvider, uint32_t meshIndex, float px, float py, float pz, float nx, float ny, float nz, float u, float v, uint32_t c);

public:

	WorldRenderer(wp::application::resourcesystem::ResourceManager* resourceMgr, wp::Logger* logger);

	virtual ~WorldRenderer();

	void setWorldChanged();

	void create(mpp::ScenePtr scene, bw::core::World const* world, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr);

	void update(bw::core::World* world, bw::core::WorldData const& worldData, float frameTime);
};
