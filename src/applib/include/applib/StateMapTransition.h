#pragma once

#include <vector>

#include <willpower/application/StateFactory.h>
#include <willpower/application/InputStateManager.h>
#include <willpower/application/resourcesystem/ResourceManager.h>
#include <willpower/application/resourcesystem/ResourceCallback.h>

#include "Platform.h"
#include "ThreadableLoadState.h"

namespace applib
{

	class APPLIB_API StateMapTransition : public ThreadableLoadState
	{
	private:

		LoadFunction getWorkFunction(wp::application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args = nullptr) override;

		void acquireMapResource(wp::application::resourcesystem::ResourceManager* resourceMgr, wp::application::resourcesystem::ResourcePtr resource, bool useThreading);

		void processResources(wp::application::resourcesystem::ResourceManager* resourceMgr, MapTransitionData* transitionData);

	protected:

		std::vector<ThreadableWorkFunction> getPreWork(StateTransitionData* transitionData) override;

	public:

		explicit StateMapTransition(bool useThreading);
	};

	class StateMapTransitionFactory : public wp::application::StateFactory
	{
	protected:

		wp::Logger* mwLogger;

		wp::application::resourcesystem::ResourceManager* mwResourceMgr;

		bool mUseThreading;

	public:

		StateMapTransitionFactory(wp::Logger* logger, wp::application::resourcesystem::ResourceManager* resourceMgr, bool useThreading)
			: wp::application::StateFactory("MapTransition")
			, mwLogger(logger)
			, mwResourceMgr(resourceMgr)
			, mUseThreading(useThreading)
		{
		}

		wp::application::State* createState()
		{
			auto state = new StateMapTransition(mUseThreading);
			state->setLogger(mwLogger);
			return state;
		}
	};

} // applib