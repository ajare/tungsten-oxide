#pragma once

#include <vector>

#include <willpower/application/StateFactory.h>
#include <willpower/application/resourcesystem/ResourceManager.h>
#include <willpower/application/resourcesystem/ResourceCallback.h>

#include "Platform.h"
#include "ThreadableLoadState.h"

namespace applib
{

	class APPLIB_API StateMapUnload : public ThreadableLoadState
	{
		LoadFunction getWorkFunction(wp::application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args = nullptr) override;

		void unloadResources(wp::application::resourcesystem::ResourceManager* resourceMgr, MapTransitionData* transitionData);

	protected:

		std::vector<ThreadableWorkFunction> getPreWork(StateTransitionData* transitionData) override;

	public:

		explicit StateMapUnload(bool useThreading);
	};

	class StateMapUnloadFactory : public wp::application::StateFactory
	{
	protected:

		wp::Logger* mwLogger;

		wp::application::resourcesystem::ResourceManager* mwResourceMgr;

		bool mUseThreading;

	public:

		StateMapUnloadFactory(wp::Logger* logger, wp::application::resourcesystem::ResourceManager* resourceMgr, bool useThreading)
			: wp::application::StateFactory("MapUnload")
			, mwLogger(logger)
			, mwResourceMgr(resourceMgr)
			, mUseThreading(useThreading)
		{
		}

		wp::application::State* createState()
		{
			auto state = new StateMapUnload(mUseThreading);
			state->setLogger(mwLogger);
			return state;
		}
	};

} // applib