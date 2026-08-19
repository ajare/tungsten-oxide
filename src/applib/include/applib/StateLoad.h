#pragma once

#include <vector>

#include <willpower/application/StateFactory.h>
#include <willpower/application/resourcesystem/ResourceManager.h>
#include <willpower/application/resourcesystem/ResourceCallback.h>

#include "Platform.h"
#include "ThreadableLoadState.h"
#include "Model.h"

namespace applib
{

	class APPLIB_API StateLoad : public ThreadableLoadState
	{
	private:

		LoadFunction getWorkFunction(wp::application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args = nullptr) override;

		void scanResourceLocationCallback(std::string const& locationName, wp::application::resourcesystem::ResourceLocationState state);

		void loadResources(wp::application::resourcesystem::ResourceManager* resourceMgr);

	public:

		explicit StateLoad(bool useThreading);
	};

	class StateLoadFactory : public wp::application::StateFactory
	{
		wp::Logger* mwLogger;

		wp::application::resourcesystem::ResourceManager* mwResourceMgr;

		bool mUseThreading;

	public:

		StateLoadFactory(wp::Logger* logger, wp::application::resourcesystem::ResourceManager* resourceMgr, bool useThreading)
			: wp::application::StateFactory("Load")
			, mwLogger(logger)
			, mwResourceMgr(resourceMgr)
			, mUseThreading(useThreading)
		{
		}

		wp::application::State* createState()
		{
			auto state = new StateLoad(mUseThreading);
			state->setLogger(mwLogger);
			return state;
		}
	};

} // applib