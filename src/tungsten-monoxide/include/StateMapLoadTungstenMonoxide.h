#pragma once

#include <vector>

#include <willpower/application/StateFactory.h>
#include <willpower/application/resourcesystem/ResourceManager.h>

#include <applib/StateMapLoad.h>

#include "Platform.h"


class APPLICATION_API StateMapLoadTungstenMonoxide : public applib::StateMapLoad
{
	void loadResources(wp::application::resourcesystem::ResourceManager* resourceMgr, applib::MapTransitionData* transitionData) override;

public:

	explicit StateMapLoadTungstenMonoxide(bool useThreading);
};

class StateMapLoadTungstenMonoxideFactory : public applib::StateMapLoadFactory
{
public:

	explicit StateMapLoadTungstenMonoxideFactory(wp::Logger* logger, wp::application::resourcesystem::ResourceManager* resourceMgr, bool useThreading)
		: applib::StateMapLoadFactory(logger, resourceMgr, useThreading)
	{
	}

	wp::application::State* createState()
	{
		auto state = new StateMapLoadTungstenMonoxide(mUseThreading);
		state->setLogger(mwLogger);
		return state;
	}
};
