#include <willpower/application/StateExceptions.h>
#include <willpower/application/ServiceLocator.h>
#include <willpower/application/ApplicationSettings.h>

#include "StateControllerBooleanWorld.h"

using namespace std;
using namespace wp;


StateControllerBooleanWorld::StateControllerBooleanWorld()
	: applib::StateController("Load")
	, mMapCount(0)
	, mNumMaps(1)
{
}

string StateControllerBooleanWorld::getNextStateName(string const& prevStateName, applib::StateTransitionData* transitionData)
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

	throw Exception("Unknown state: " + prevStateName);
}

void StateControllerBooleanWorld::updateTransitionData(string const& prevStateName, string const& nextStateName, applib::StateTransitionData* transitionData)
{
	if (prevStateName == "Load")
	{
		setTransitionNextMap(&mTransitionData.mapData, "World", nullptr);
	}
	else if (prevStateName == "Play")
	{
		transferTransitionMapData(&transitionData->mapData.prevMap,	&mTransitionData.mapData.prevMap);

		string nextMapName;
		switch (mMapCount)
		{
		case 1:
			nextMapName = "World";

			setTransitionNextMap(
				&mTransitionData.mapData, 
				nextMapName, 
				mwResourceMgr->getResource(nextMapName, nextMapName));
			break;
		}
	}

	if (nextStateName == "Play")
	{
		transferTransitionMapData(&transitionData->mapData.nextMap, &mTransitionData.mapData.nextMap);
	}
}