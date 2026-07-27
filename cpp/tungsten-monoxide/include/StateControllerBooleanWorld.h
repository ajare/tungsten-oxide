#pragma once

#include <vector>

#include <willpower/application/StateFactory.h>
#include <willpower/application/resourcesystem/ResourceManager.h>

#include <applib/StateController.h>

#include "Platform.h"


class APPLICATION_API StateControllerBooleanWorld : public applib::StateController
{
	uint32_t mMapCount, mNumMaps;

private:

	std::string getNextStateName(std::string const& prevStateName, applib::StateTransitionData* transitionData) override;

	void updateTransitionData(std::string const& prevStateName, std::string const& nextStateName, applib::StateTransitionData* transitionData) override;

protected:

public:

	StateControllerBooleanWorld();
};

class StateControllerBooleanWorldFactory : public wp::application::StateFactory
{
	wp::Logger* mwLogger;

public:

	explicit StateControllerBooleanWorldFactory(wp::Logger* logger)
		: wp::application::StateFactory("Controller")
		, mwLogger(logger)
	{
	}

	wp::application::State* createState()
	{
		auto state = new StateControllerBooleanWorld();
		state->setLogger(mwLogger);
		return state;
	}
};
