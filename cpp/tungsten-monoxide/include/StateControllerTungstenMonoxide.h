#pragma once

#include <vector>

#include <willpower/application/StateFactory.h>
#include <willpower/application/resourcesystem/ResourceManager.h>

#include <applib/StateController.h>

#include "Platform.h"


class APPLICATION_API StateControllerTungstenMonoxide : public applib::StateController
{
	uint32_t mMapCount, mNumMaps;

private:

	std::string getNextStateName(std::string const& prevStateName, applib::StateTransitionData* transitionData) override;

	void updateTransitionData(std::string const& prevStateName, std::string const& nextStateName, applib::StateTransitionData* transitionData) override;

protected:

public:

	StateControllerTungstenMonoxide();
};

class StateControllerTungstenMonoxideFactory : public wp::application::StateFactory
{
	wp::Logger* mwLogger;

public:

	explicit StateControllerTungstenMonoxideFactory(wp::Logger* logger)
		: wp::application::StateFactory("Controller")
		, mwLogger(logger)
	{
	}

	wp::application::State* createState()
	{
		auto state = new StateControllerTungstenMonoxide();
		state->setLogger(mwLogger);
		return state;
	}
};
