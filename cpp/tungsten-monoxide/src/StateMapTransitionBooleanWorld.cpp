#include <willpower/application/StateExceptions.h>
#include <willpower/application/ServiceLocator.h>
#include <willpower/application/ApplicationSettings.h>

#include "StateMapTransitionBooleanWorld.h"
#include "WorldRenderer.h"
#include "Map.h"

using namespace std;
using namespace wp;


StateMapTransitionBooleanWorld::StateMapTransitionBooleanWorld(bool useThreading)
	: applib::StateMapTransition(nullptr, useThreading)
{
}

vector<applib::ThreadableLoadState::ThreadableWorkFunction> StateMapTransitionBooleanWorld::getPreWork(applib::StateTransitionData* transitionData)
{
	VAR_UNUSED(transitionData);

	auto destroyWorldRendererFn = [this](bool useThreading)
	{
		VAR_UNUSED(useThreading);

		addText("Destroying world renderer");
	};

	return { destroyWorldRendererFn };
}

void StateMapTransitionBooleanWorld::processResources(application::resourcesystem::ResourceManager* resourceMgr, applib::MapTransitionData* transitionData)
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
		auto callback = bind(&StateMapTransitionBooleanWorld::unloadResourceCallback,
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
		auto loadCallbackFn = bind(&StateMapTransitionBooleanWorld::loadResourceCallback,
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

	auto createWorldRendererFn = [this, resourceMgr](bool useThreading)
	{
		VAR_UNUSED(useThreading);

		// Destroy
		auto worldRenderer = static_cast<WorldRenderer*>(this->mTransitionData.userData);
		delete worldRenderer;

		// Create new
		this->mTransitionData.userData = new WorldRenderer(resourceMgr, this->mwLogger);
	};

	processPostWork({ createWorldRendererFn });
}
