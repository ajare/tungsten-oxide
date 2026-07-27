#include <clipper2/clipper.h>

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
#include <applib/MapTiledDefinitionFactory.h>
#include <applib/ProtoEntityDefaultDefinitionFactory.h>
#include <applib/ImageSetTiledDefinitionFactory.h>

#include "MapBooleanWorldDefinitionFactory.h"
#include "GameDefinitionFactory.h"
#include "ProtoEntityDefinitionFactory.h"

// Model
#include "BooleanWorldModel.h"
#include "EntityHandlerBooleanWorld.h"

// States
#include "StateMapLoadBooleanWorld.h"
#include "StateMapUnloadBooleanWorld.h"
#include "StateMapTransitionBooleanWorld.h"
#include "StateControllerBooleanWorld.h"
#include "StatePlayBooleanWorld.h"

// Resources
#include "Game.h"
#include "Map.h"
#include "ProtoEntity.h"

using namespace std;

// Model
static applib::Model* model = nullptr;

// State factories
static int nextStateFactory = 0;
static StateControllerBooleanWorldFactory* stateControllerFactory = nullptr;
static applib::StateLoadFactory* stateLoadFactory = nullptr;
static applib::StateUnloadFactory* stateUnloadFactory = nullptr;
static applib::StateMapLoadFactory* stateMapLoadFactory = nullptr;
static applib::StateMapUnloadFactory* stateMapUnloadFactory = nullptr;
static applib::StateMapTransitionFactory* stateMapTransitionFactory = nullptr;
static StatePlayBooleanWorldFactory* statePlayBooleanWorldFactory = nullptr;


// Arguments
static bool gThreadedLoading = true;

extern "C"
{
	__declspec(dllexport) char const* dllGetName()
	{
		return "BooleanWorld";
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
			stateFactory = statePlayBooleanWorldFactory;
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
		auto entityHandlerFactory = [](shared_ptr<applib::AnimationDatabase> animDatabase)
		{
			return new EntityHandlerBooleanWorld(animDatabase);
		};

		model = new BooleanWorldModel(entityHandlerFactory, resourceMgr);
		applib::ModelInstance::set(model);

		// Create state factories
		stateControllerFactory = new StateControllerBooleanWorldFactory(logger);
		stateLoadFactory = new applib::StateLoadFactory(logger, resourceMgr, gThreadedLoading);
		stateUnloadFactory = new applib::StateUnloadFactory(logger, resourceMgr, gThreadedLoading);
		stateMapLoadFactory = new StateMapLoadBooleanWorldFactory(logger, resourceMgr, gThreadedLoading);
		stateMapUnloadFactory = new StateMapUnloadBooleanWorldFactory(logger, resourceMgr, gThreadedLoading);
		stateMapTransitionFactory = new StateMapTransitionBooleanWorldFactory(logger, resourceMgr, gThreadedLoading);
		statePlayBooleanWorldFactory = new StatePlayBooleanWorldFactory(logger);

		// Add resource factories
		resourceMgr->addResourceFactory(new GameResourceFactory(model->animationDatabase));
		resourceMgr->addResourceFactory(new MapResourceFactory(logger));
		resourceMgr->addResourceFactory(new ProtoEntityResourceFactory(model->entityHandler, model->animationDatabase));

		// Add resource definition factories
		resourceMgr->addResourceDefinitionFactory(new GameDefinitionFactory());
		resourceMgr->addResourceDefinitionFactory(new MapBooleanWorldDefinitionFactory());
		resourceMgr->addResourceDefinitionFactory(new applib::MapTiledDefinitionFactory());
		resourceMgr->addResourceDefinitionFactory(new ProtoEntityDefinitionFactory());
		resourceMgr->addResourceDefinitionFactory(new applib::ImageSetTiledDefinitionFactory());

		// Memory allocators
		Clipper2Lib::WmInitialiseAllocators(4, 2 * 1024 * 1024);
	}

	__declspec(dllexport) void dllOnExit()
	{
		// Memory allocators
		Clipper2Lib::WmDestroyAllocators();

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

		delete statePlayBooleanWorldFactory;
		statePlayBooleanWorldFactory = nullptr;

		// Model
		delete model;
		model = nullptr;
	}

}