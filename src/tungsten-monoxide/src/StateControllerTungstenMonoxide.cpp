#include <mpp/RenderSystem.h>

#include <willpower/application/StateExceptions.h>
#include <willpower/application/ServiceLocator.h>
#include <willpower/application/ApplicationSettings.h>

#include <applib/Exceptions.h>
#include <applib/ModelInstance.h>

#include "StateControllerTungstenMonoxide.h"
#include "TungstenMonoxideModel.h"

using namespace std;
using namespace wp;


StateControllerTungstenMonoxide::StateControllerTungstenMonoxide()
	: applib::StateController("Load")
	, mMapCount(0)
	, mNumMaps(1)
{
}

void StateControllerTungstenMonoxide::setup(application::resourcesystem::ResourceManager* resourceMgr,
                                             mpp::RenderSystem* renderSystem,
                                             mpp::ResourceManager* renderResourceMgr,
                                             void* args)
{
	applib::StateController::setup(resourceMgr, renderSystem, renderResourceMgr, args);

	auto tungstenModel = dynamic_cast<TungstenMonoxideModel*>(applib::ModelInstance::get());
	if (!tungstenModel || !tungstenModel->pbrPackage)
	{
		throw applib::Exception("TungstenMonoxide PBR package service is unavailable.");
	}
	tungstenModel->pbrPackage->initialize(
		renderSystem,
		renderResourceMgr,
		static_cast<uint32_t>(renderSystem->getWindowWidth()),
		static_cast<uint32_t>(renderSystem->getWindowHeight()));
}

void StateControllerTungstenMonoxide::teardown()
{
	if (auto tungstenModel = dynamic_cast<TungstenMonoxideModel*>(applib::ModelInstance::get()); tungstenModel && tungstenModel->pbrPackage)
	{
		tungstenModel->pbrPackage->shutdown();
	}
	applib::StateController::teardown();
}

string StateControllerTungstenMonoxide::getNextStateName(string const& prevStateName, applib::StateTransitionData* transitionData)
{
	VAR_UNUSED(transitionData);

	if (prevStateName == "Load")
	{
		return "MapLoad";
	}
	else if (prevStateName == "MapLoad")
	{
		return "Play";
	}
	else if (prevStateName == "MapTransition")
	{
		return "Play";
	}
	else if (prevStateName == "Play")
	{
		mMapCount++;

		return mMapCount < mNumMaps ? "MapTransition" : "MapUnload";
	}
	else if (prevStateName == "MapUnload")
	{
		return "Unload";
	}
	else if (prevStateName == "Unload")
	{
		return "";
	}

	throw applib::Exception("Unknown state: " + prevStateName);
}

void StateControllerTungstenMonoxide::updateTransitionData(string const& prevStateName, string const& nextStateName, applib::StateTransitionData* transitionData)
{
	if (prevStateName == "Load")
	{
		setTransitionNextMap(&mTransitionData.mapData, "NewTrack", "Tracks", nullptr);
	}
	else if (prevStateName == "Play")
	{
		transferTransitionMapData(&transitionData->mapData.prevMap,	&mTransitionData.mapData.prevMap);

		switch (mMapCount)
		{
		case 1:
			setTransitionNextMap(
				&mTransitionData.mapData, 
				"Track",
                "Tracks",
				mwResourceMgr->getResource("NewTrack", "Tracks"));
			break;
		}
	}

	if (nextStateName == "Play")
	{
		transferTransitionMapData(&transitionData->mapData.nextMap, &mTransitionData.mapData.nextMap);
	}
}