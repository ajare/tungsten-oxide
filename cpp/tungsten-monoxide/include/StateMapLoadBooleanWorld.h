#pragma once

#include <vector>

#include <willpower/application/StateFactory.h>
#include <willpower/application/resourcesystem/ResourceManager.h>

#include <applib/StateMapLoad.h>

#include "Platform.h"


class APPLICATION_API StateMapLoadBooleanWorld : public applib::StateMapLoad
{
	void loadResources(wp::application::resourcesystem::ResourceManager* resourceMgr, applib::MapTransitionData* transitionData) override;

public:

	explicit StateMapLoadBooleanWorld(bool useThreading);
};

class StateMapLoadBooleanWorldFactory : public applib::StateMapLoadFactory
{
public:

	explicit StateMapLoadBooleanWorldFactory(wp::Logger* logger, wp::application::resourcesystem::ResourceManager* resourceMgr, bool useThreading)
		: applib::StateMapLoadFactory(logger, resourceMgr, nullptr, useThreading)
	{
	}

	wp::application::State* createState()
	{
		auto state = new StateMapLoadBooleanWorld(mUseThreading);
		state->setLogger(mwLogger);
		return state;
	}
};
