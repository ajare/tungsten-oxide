#include <willpower/application/StateExceptions.h>
#include <willpower/application/ServiceLocator.h>
#include <willpower/application/ApplicationSettings.h>

#include "StateMapUnload.h"

namespace applib
{

	using namespace std;
	using namespace wp;

	StateMapUnload::StateMapUnload(bool useThreading)
		: ThreadableLoadState("MapUnload", Action::Unload, useThreading)
	{
	}

	ThreadableLoadState::LoadFunction StateMapUnload::getWorkFunction(wp::application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args)
	{
		WP_UNUSED(renderSystem);
		WP_UNUSED(renderResourceMgr);

		auto transitionData = static_cast<StateTransitionData*>(args);
		return bind(&StateMapUnload::unloadResources, this, resourceMgr, &transitionData->mapData);
	}

	vector<ThreadableLoadState::ThreadableWorkFunction> StateMapUnload::getPreWork(StateTransitionData* transitionData)
	{
		return {};
	}

	void StateMapUnload::unloadResources(application::resourcesystem::ResourceManager* resourceMgr, MapTransitionData* transitionData)
	{
		// Unload given map & its resources
		string mapName = transitionData->prevMap.map->getName();
		string mapNamespace = transitionData->prevMap.map->getNamespace();

		// Willpower resources
		mWillpowerResourcesToLoad.clear();
		mWillpowerResourcesToUnload = resourceMgr->getNamespaceResources(mapNamespace);

		if (usingThreading())
		{
			int resourceCount = (int)mWillpowerResourcesToUnload.size();

			if (resourceCount > 0)
			{
				for (auto res : mWillpowerResourcesToUnload)
				{
					addPendingResourceName(res->getName());
				}

				auto callback = bind(&StateMapUnload::unloadResourceCallback,
					this,
					placeholders::_1,
					placeholders::_2,
					placeholders::_3);

				for (auto resource : mWillpowerResourcesToUnload)
				{
					resourceMgr->releaseResource(resource, callback);
				}

				mWillpowerResourcesToUnload.clear();
			}
		}
	}


} // applib