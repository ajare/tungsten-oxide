#include <willpower/application/StateExceptions.h>
#include <willpower/application/ServiceLocator.h>
#include <willpower/application/ApplicationSettings.h>

#include "Platform.h"
#include "StateMapLoadTungstenMonoxide.h"
#include "Map.h"

using namespace std;
using namespace wp;

StateMapLoadTungstenMonoxide::StateMapLoadTungstenMonoxide(bool useThreading)
    : applib::StateMapLoad(useThreading) {
}

void StateMapLoadTungstenMonoxide::loadResources(wp::application::resourcesystem::ResourceManager* resourceMgr, applib::MapTransitionData* transitionData) {
  string mapName = transitionData->nextMapName;
  string mapNamespace = transitionData->nextMapNamespace;

  // Set next map in transition data
  auto mapResource = resourceMgr->getResource(mapName, mapNamespace);
  mTransitionData.mapData.nextMap.map = mapResource;

  // Initialise resource loading
  mWillpowerResourcesToLoad = resourceMgr->getNamespaceResources(mapNamespace);
  mWillpowerResourcesToUnload.clear();

  if (usingThreading()) {
    // A game-level dependent (the ship material, for example) may already have
    // loaded a resource from the map namespace. ResourceManager deliberately
    // emits no Loaded callback for an already-loaded resource, so only add
    // genuinely pending roots to the callback completion set. Every root is
    // still acquired below so this map state owns its normal reference.
    for (auto res : mWillpowerResourcesToLoad) {
      if (!resourceMgr->isResourceLoaded(res))
        addPendingResourceName(res->getName());
    }

    auto loadCallbackFn = bind(&StateMapLoadTungstenMonoxide::loadResourceCallback,
                               this,
                               placeholders::_1,
                               placeholders::_2,
                               placeholders::_3);

    for (auto resource : mWillpowerResourcesToLoad) {
      resourceMgr->createResource(resource, loadCallbackFn);
      resourceMgr->loadResource(resource, loadCallbackFn);
      resourceMgr->acquireResource(resource);
    }

    if (isPendingResourceCountZero())
      finishProcessingWillpowerResources();
    mWillpowerResourcesToLoad.clear();
  }

  processPostWork({});
}
