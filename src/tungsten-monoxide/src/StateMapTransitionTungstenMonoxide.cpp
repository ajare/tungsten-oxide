#include <willpower/application/StateExceptions.h>
#include <willpower/application/ServiceLocator.h>
#include <willpower/application/ApplicationSettings.h>

#include "StateMapTransitionTungstenMonoxide.h"
#include "Map.h"

using namespace std;
using namespace wp;


StateMapTransitionTungstenMonoxide::StateMapTransitionTungstenMonoxide(bool useThreading)
	: applib::StateMapTransition(useThreading)
{
}

vector<applib::ThreadableLoadState::ThreadableWorkFunction> StateMapTransitionTungstenMonoxide::getPreWork(applib::StateTransitionData* transitionData)
{
	VAR_UNUSED(transitionData);

	return {};
}

void StateMapTransitionTungstenMonoxide::processResources(application::resourcesystem::ResourceManager* resourceMgr, applib::MapTransitionData* transitionData)
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
		auto callback = bind(&StateMapTransitionTungstenMonoxide::unloadResourceCallback,
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
		auto loadCallbackFn = bind(&StateMapTransitionTungstenMonoxide::loadResourceCallback,
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
