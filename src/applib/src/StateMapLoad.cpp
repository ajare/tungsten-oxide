#include <willpower/application/StateExceptions.h>
#include <willpower/application/ServiceLocator.h>
#include <willpower/application/ApplicationSettings.h>

#include "StateMapLoad.h"
#include "Map.h"

namespace applib
{

	using namespace std;
	using namespace wp;

	StateMapLoad::StateMapLoad(bool useThreading)
		: ThreadableLoadState("MapLoad", Action::Load, useThreading)
	{
	}

	ThreadableLoadState::LoadFunction StateMapLoad::getWorkFunction(wp::application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args)
	{
		WP_UNUSED(renderSystem);
		WP_UNUSED(renderResourceMgr);
		WP_UNUSED(args);

		auto transitionData = static_cast<StateTransitionData*>(args);
		return bind(&StateMapLoad::loadResources, this, resourceMgr, &transitionData->mapData);
	}

	void StateMapLoad::loadResources(application::resourcesystem::ResourceManager* resourceMgr, MapTransitionData* transitionData)
	{
		string mapName = transitionData->nextMapName;
		string mapNamespace = transitionData->nextMapNamespace;

		// Set next map in transition data
		auto mapResource = resourceMgr->getResource(mapName, mapNamespace);
		mTransitionData.mapData.nextMap.map = mapResource;

		// Initialise resource loading
		auto resources = resourceMgr->getNamespaceResources(mapNamespace);

		mWillpowerResourcesToLoad = { resources };
		mWillpowerResourcesToUnload.clear();

		if (usingThreading())
		{
			for (auto res : mWillpowerResourcesToLoad)
			{
				addPendingResourceName(res->getName());
			}

			auto loadCallbackFn = bind(&StateMapLoad::loadResourceCallback,
				this,
				placeholders::_1,
				placeholders::_2,
				placeholders::_3);

			for (auto resource : mWillpowerResourcesToLoad)
			{
				resourceMgr->createResource(resource, loadCallbackFn);
				resourceMgr->loadResource(resource, loadCallbackFn);
				resourceMgr->acquireResource(resource);
			}

			mWillpowerResourcesToLoad.clear();
		}

		processPostWork({});
	}


} // applib