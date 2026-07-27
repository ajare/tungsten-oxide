#include <willpower/common/Logger.h>

#include <willpower/application/StateFactory.h>

#include <applib/ModelInstance.h>
#include <applib/StateLoad.h>
#include <applib/StateUnload.h>
#include <applib/StateMapLoad.h>
#include <applib/StateMapUnload.h>
#include <applib/StateMapTransition.h>
#include <applib/Game.h>
#include <applib/MapDefaultDefinitionFactory.h>
#include <applib/ProtoEntityDefaultDefinitionFactory.h>

#include "MapTungstenMonoxideDefinitionFactory.h"
#include "GameDefinitionFactory.h"
#include "ProtoEntityDefinitionFactory.h"

// Model
#include "TungstenMonoxideModel.h"
#include "EntityHandlerTungstenMonoxide.h"

// States
#include "StateMapLoadTungstenMonoxide.h"
#include "StateMapUnloadTungstenMonoxide.h"
#include "StateMapTransitionTungstenMonoxide.h"
#include "StateControllerTungstenMonoxide.h"
#include "StatePlayTungstenMonoxide.h"

// Resources
#include "Game.h"
#include "Map.h"
#include "ProtoEntity.h"

using namespace std;

// Model
static applib::Model* model = nullptr;

// State factories
static int nextStateFactory = 0;
static StateControllerTungstenMonoxideFactory* stateControllerFactory = nullptr;
static applib::StateLoadFactory* stateLoadFactory = nullptr;
static applib::StateUnloadFactory* stateUnloadFactory = nullptr;
static applib::StateMapLoadFactory* stateMapLoadFactory = nullptr;
static applib::StateMapUnloadFactory* stateMapUnloadFactory = nullptr;
static applib::StateMapTransitionFactory* stateMapTransitionFactory = nullptr;
static StatePlayTungstenMonoxideFactory* statePlayTungstenMonoxideFactory = nullptr;


// Arguments
static bool gThreadedLoading = true;

extern "C"
{
	__declspec(dllexport) char const* dllGetName()
	{
		return "TungstenMonoxide";
	}

	__declspec(dllexport) int dllSetArgument(char const* arg, char const* value)
	{
		if (!strcmp(arg, "ThreadedLoading"))
		{
			string v = string(value);

			if (v == "true")
			{
				gThreadedLoading = true;
			}
			else if (v == "false")
			{
				gThreadedLoading = false;
			}
			else
			{
				return 1;
			}
		}

		return 0;
	}

	__declspec(dllexport) wp::application::StateFactory* dllGetNextStateFactory()
	{
		wp::application::StateFactory* stateFactory;
		switch (nextStateFactory)
		{
		case 0:
			stateFactory = stateControllerFactory;
			break;
		case 1:
			stateFactory = stateLoadFactory;
			break;
		case 2:
			stateFactory = stateUnloadFactory;
			break;
		case 3:
			stateFactory = stateMapLoadFactory;
			break;
		case 4:
			stateFactory = stateMapUnloadFactory;
			break;
		case 5:
			stateFactory = stateMapTransitionFactory;
			break;
		case 6:
			stateFactory = statePlayTungstenMonoxideFactory;
			break;
		default:
			stateFactory = nullptr;
			break;
		}

		nextStateFactory++;
		return stateFactory;
	}

	__declspec(dllexport) void dllOnEntry(wp::Logger* logger, wp::application::resourcesystem::ResourceManager* resourceMgr)
	{
		auto entityHandlerFactory = []()
		{
			return new EntityHandlerTungstenMonoxide();
		};

		model = new TungstenMonoxideModel(entityHandlerFactory, resourceMgr);
		applib::ModelInstance::set(model);

		// Create state factories
		stateControllerFactory = new StateControllerTungstenMonoxideFactory(logger);
		stateLoadFactory = new applib::StateLoadFactory(logger, resourceMgr, gThreadedLoading);
		stateUnloadFactory = new applib::StateUnloadFactory(logger, resourceMgr, gThreadedLoading);
		stateMapLoadFactory = new StateMapLoadTungstenMonoxideFactory(logger, resourceMgr, gThreadedLoading);
		stateMapUnloadFactory = new StateMapUnloadTungstenMonoxideFactory(logger, resourceMgr, gThreadedLoading);
		stateMapTransitionFactory = new StateMapTransitionTungstenMonoxideFactory(logger, resourceMgr, gThreadedLoading);
		statePlayTungstenMonoxideFactory = new StatePlayTungstenMonoxideFactory(logger);

		// Add resource factories
		resourceMgr->addResourceFactory(new GameResourceFactory());
		resourceMgr->addResourceFactory(new MapResourceFactory(logger));
		resourceMgr->addResourceFactory(new ProtoEntityResourceFactory(model->entityHandler));

		// Add resource definition factories
		resourceMgr->addResourceDefinitionFactory(new GameDefinitionFactory());
		resourceMgr->addResourceDefinitionFactory(new MapTungstenMonoxideDefinitionFactory());
		resourceMgr->addResourceDefinitionFactory(new ProtoEntityDefinitionFactory());
	}

	__declspec(dllexport) void dllOnExit()
	{
		// Memory allocators
		// Destroy state factories
		delete stateControllerFactory;
		stateControllerFactory = nullptr;

		delete stateLoadFactory;
		stateLoadFactory = nullptr;

		delete stateUnloadFactory;
		stateUnloadFactory = nullptr;

		delete stateMapLoadFactory;
		stateMapLoadFactory = nullptr;

		delete stateMapUnloadFactory;
		stateMapUnloadFactory = nullptr;

		delete stateMapTransitionFactory;
		stateMapTransitionFactory = nullptr;

		delete statePlayTungstenMonoxideFactory;
		statePlayTungstenMonoxideFactory = nullptr;

		// Model
		delete model;
		model = nullptr;
	}

}