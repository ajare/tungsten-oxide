#include <willpower/application/StateExceptions.h>
#include <willpower/application/ServiceLocator.h>
#include <willpower/application/ApplicationSettings.h>

#include "StateController.h"
#include "Game.h"


namespace applib
{

	using namespace std;
	using namespace wp;

	StateController::StateController(string const& initialState)
		: State("Controller")
		, mNextState(initialState)
	{
	}

	StateController::~StateController()
	{
	}

	void StateController::setup(application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args)
	{
		WP_UNUSED(resourceMgr);
		WP_UNUSED(renderResourceMgr);
		WP_UNUSED(renderSystem);
		
		// Args is the name of the game resource
		mGameResourceName = *static_cast<string*>(args);
	}

	void StateController::teardown()
	{
	}

	void StateController::transferTransitionMapData(MapTransitionData::MapData* from, MapTransitionData::MapData* to)
	{
		to->map = from->map;
	}

	void StateController::setTransitionNextMap(MapTransitionData* data, string const& name, application::resourcesystem::ResourcePtr resource)
	{
		data->nextMap.map = resource;
		data->nextMapName = name;
	}

	void StateController::exitImpl()
	{
	}

	void StateController::suspendImpl(void* args)
	{
		WP_UNUSED(args);
	}

	void StateController::resumeImpl(void* args)
	{
		auto transitionData = static_cast<StateTransitionData*>(args);

		if (transitionData->prevStateName == "Load")
		{
			string gameResName, gameResNamespace;
			application::resourcesystem::Resource::splitName(mGameResourceName, gameResNamespace, &gameResNamespace, &gameResName);

			mTransitionData.gameResource = mwResourceMgr->getResource(gameResName, gameResNamespace);
		}

		mTransitionData.userData = transitionData->userData;

		// Based on previous state name, set the next state and set up the transition data
		mNextState = getNextStateName(transitionData->prevStateName, transitionData);
		updateTransitionData(transitionData->prevStateName, mNextState, transitionData);
	}

	void StateController::updateImpl(float frameTime)
	{
		WP_UNUSED(frameTime);

		if (mNextState != "")
		{
			throw application::SuspendAndMoveToStateException(
				mNextState,
				true,
				true,
				true,
				&mTransitionData);
		}
		else
		{
			throw application::ReturnFromStateException();
		}
	}


	void StateController::renderImpl(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
	{
		WP_UNUSED(resourceMgr);

		auto const& viewPos = mCamera->getPosition();
		auto halfScreenSize = getWindowSize() / 2.0f;

		renderSystem->renderScene(
			mScene,
			mCamera,
			glm::vec2(viewPos.x - halfScreenSize.x,	viewPos.y - halfScreenSize.y),
			getName());
	}

} // applib