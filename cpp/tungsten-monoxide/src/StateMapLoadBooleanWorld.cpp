#include <willpower/application/StateExceptions.h>
#include <willpower/application/ServiceLocator.h>
#include <willpower/application/ApplicationSettings.h>

#include "Platform.h"
#include "StateMapLoadBooleanWorld.h"
#include "WorldRenderer.h"
#include "Map.h"

using namespace std;
using namespace wp;


StateMapLoadBooleanWorld::StateMapLoadBooleanWorld(bool useThreading)
	: applib::StateMapLoad(nullptr, useThreading)
{
}

void StateMapLoadBooleanWorld::loadResources(wp::application::resourcesystem::ResourceManager* resourceMgr, applib::MapTransitionData* transitionData)
{
	string mapName = transitionData->nextMapName;
	string mapNamespace = mapName;

	// Set next map in transition data
	auto mapResource = resourceMgr->getResource(mapName, mapNamespace);
	mTransitionData.mapData.nextMap.map = mapResource;

	// Initialise resource loading
	mWillpowerResourcesToLoad = resourceMgr->getNamespaceResources(mapNamespace);
	mWillpowerResourcesToUnload.clear();

	if (usingThreading())
	{
		for (auto res : mWillpowerResourcesToLoad)
		{
			addPendingResourceName(res->getName());
		}

		auto loadCallbackFn = bind(&StateMapLoadBooleanWorld::loadResourceCallback,
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

	auto createWorldRendererFn = [this, resourceMgr](bool useThreading)
	{
		VAR_UNUSED(useThreading);

		this->addText("Creating world renderer");

		this->mTransitionData.userData = new WorldRenderer(resourceMgr, this->mwLogger);

	};

	processPostWork({ createWorldRendererFn });
}
