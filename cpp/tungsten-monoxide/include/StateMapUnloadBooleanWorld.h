#pragma once

#include <vector>

#include <willpower/application/StateFactory.h>
#include <willpower/application/resourcesystem/ResourceManager.h>

#include <applib/StateMapUnload.h>

#include "Platform.h"


class APPLICATION_API StateMapUnloadBooleanWorld : public applib::StateMapUnload
{
protected:

	std::vector<ThreadableWorkFunction> getPreWork(applib::StateTransitionData* transitionData) override;

public:

	explicit StateMapUnloadBooleanWorld(bool useThreading);
};

class StateMapUnloadBooleanWorldFactory : public applib::StateMapUnloadFactory
{
public:

	explicit StateMapUnloadBooleanWorldFactory(wp::Logger* logger, wp::application::resourcesystem::ResourceManager* resourceMgr, bool useThreading)
		: applib::StateMapUnloadFactory(logger, resourceMgr, useThreading)
	{
	}

	wp::application::State* createState()
	{
		auto state = new StateMapUnloadBooleanWorld(mUseThreading);
		state->setLogger(mwLogger);
		return state;
	}
};
