#pragma once

#include <vector>

#include <willpower/application/StateFactory.h>
#include <willpower/application/resourcesystem/ResourceManager.h>

#include <applib/StateMapTransition.h>

#include "Platform.h"


class APPLICATION_API StateMapTransitionTungstenMonoxide : public applib::StateMapTransition
{
	void processResources(wp::application::resourcesystem::ResourceManager* resourceMgr, applib::MapTransitionData* transitionData);

protected:

	std::vector<ThreadableWorkFunction> getPreWork(applib::StateTransitionData* transitionData) override;

public:

	explicit StateMapTransitionTungstenMonoxide(bool useThreading);
};

class StateMapTransitionTungstenMonoxideFactory : public applib::StateMapTransitionFactory
{
public:

	explicit StateMapTransitionTungstenMonoxideFactory(wp::Logger* logger, wp::application::resourcesystem::ResourceManager* resourceMgr, bool useThreading)
		: applib::StateMapTransitionFactory(logger, resourceMgr, useThreading)
	{
	}

	wp::application::State* createState()
	{
		auto state = new StateMapTransitionTungstenMonoxide(mUseThreading);
		state->setLogger(mwLogger);
		return state;
	}
};
