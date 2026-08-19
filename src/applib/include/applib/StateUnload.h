#pragma once

#include <vector>
#include <mutex>

#include <willpower/application/StateFactory.h>
#include <willpower/application/resourcesystem/ResourceManager.h>
#include <willpower/application/resourcesystem/ResourceCallback.h>

#include "Platform.h"
#include "ThreadableLoadState.h"

namespace applib
{

	class APPLIB_API StateUnload : public ThreadableLoadState
	{
		LoadFunction getWorkFunction(wp::application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args = nullptr) override;

		void unloadResources(wp::application::resourcesystem::ResourceManager* resourceMgr);

	public:

		explicit StateUnload(bool useThreading);
	};

	class StateUnloadFactory : public wp::application::StateFactory
	{
		wp::Logger* mwLogger;

		wp::application::resourcesystem::ResourceManager* mwResourceMgr;

		bool mUseThreading;

	public:

		StateUnloadFactory(wp::Logger* logger, wp::application::resourcesystem::ResourceManager* resourceMgr, bool useThreading)
			: wp::application::StateFactory("Unload")
			, mwLogger(logger)
			, mwResourceMgr(resourceMgr)
			, mUseThreading(useThreading)
		{
		}

		wp::application::State* createState()
		{
			auto state = new StateUnload(mUseThreading);
			state->setLogger(mwLogger);
			return state;
		}
	};


} // applib