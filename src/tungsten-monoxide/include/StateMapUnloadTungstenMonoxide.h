#pragma once

#include <vector>

#include <willpower/application/StateFactory.h>
#include <willpower/application/resourcesystem/ResourceManager.h>

#include <applib/StateMapUnload.h>

#include "Platform.h"


class APPLICATION_API StateMapUnloadTungstenMonoxide : public applib::StateMapUnload
{
protected:

	std::vector<ThreadableWorkFunction> getPreWork(applib::StateTransitionData* transitionData) override;

public:

	explicit StateMapUnloadTungstenMonoxide(bool useThreading);
};

class StateMapUnloadTungstenMonoxideFactory : public applib::StateMapUnloadFactory
{
public:

	explicit StateMapUnloadTungstenMonoxideFactory(wp::Logger* logger, wp::application::resourcesystem::ResourceManager* resourceMgr, bool useThreading)
		: applib::StateMapUnloadFactory(logger, resourceMgr, useThreading)
	{
	}

	wp::application::State* createState()
	{
		auto state = new StateMapUnloadTungstenMonoxide(mUseThreading);
		state->setLogger(mwLogger);
		return state;
	}
};
