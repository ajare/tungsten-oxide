#include <willpower/application/StateExceptions.h>
#include <willpower/application/ServiceLocator.h>
#include <willpower/application/ApplicationSettings.h>

#include "StateMapTransition.h"
#include "Map.h"

namespace applib
{

	using namespace std;
	using namespace wp;

	StateMapTransition::StateMapTransition(bool useThreading)
		: ThreadableLoadState("MapTransition", Action::Unload, useThreading)
	{
	}

	ThreadableLoadState::LoadFunction StateMapTransition::getWorkFunction(wp::application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args)
	{
		WP_UNUSED(renderSystem);
		WP_UNUSED(renderResourceMgr);

		auto transitionData = static_cast<StateTransitionData*>(args);
		return bind(&StateMapTransition::processResources, this, resourceMgr, &transitionData->mapData);
	}

	void StateMapTransition::acquireMapResource(application::resourcesystem::ResourceManager* resourceMgr, application::resourcesystem::ResourcePtr resource, bool useThreading)
	{
		WP_UNUSED(useThreading);

		resourceMgr->acquireResource(resource);
	}

	vector<ThreadableLoadState::ThreadableWorkFunction> StateMapTransition::getPreWork(StateTransitionData* transitionData)
	{
		return { };
	}

	void StateMapTransition::processResources(application::resourcesystem::ResourceManager* resourceMgr, MapTransitionData* transitionData)
	{
		// Get transition data from previous state
		auto prevMap = transitionData->prevMap.map;
		auto nextMap = transitionData->nextMap.map;

		// Set up new transition data to pass to next state
		mTransitionData.mapData.nextMap.map = nextMap;

		// Release the previous map
		if (usingThreading())
		{
			addPendingResourceName(prevMap->getName());
			addPendingResourceName(nextMap->getName());
		}

		mWillpowerResourcesToUnload = { prevMap };

		if (usingThreading())
		{
			auto callback = bind(&StateMapTransition::unloadResourceCallback,
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

		// Load next map
		mWillpowerResourcesToLoad = { nextMap };

		if (usingThreading())
		{
			auto loadCallbackFn = bind(&StateMapTransition::loadResourceCallback,
				this,
				placeholders::_1,
				placeholders::_2,
				placeholders::_3);

			for (auto resource : mWillpowerResourcesToLoad)
			{
				resourceMgr->createResource(resource, loadCallbackFn);
				resourceMgr->loadResource(resource, loadCallbackFn);
			}

			mWillpowerResourcesToLoad.clear();
		}

		processPostWork({});
	}


} // applib