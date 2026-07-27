#pragma once

#include <vector>
#include <memory>

#include <willpower/application/StateFactory.h>
#include <willpower/application/resourcesystem/ResourceManager.h>
#include <willpower/application/resourcesystem/ResourceCallback.h>

#include "Platform.h"
#include "ThreadableLoadState.h"


namespace applib
{

	class APPLIB_API StateMapLoad : public ThreadableLoadState
	{
	private:

		LoadFunction getWorkFunction(wp::application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args = nullptr) override;

		virtual void loadResources(wp::application::resourcesystem::ResourceManager* resourceMgr, MapTransitionData* transitionData);

	public:

		explicit StateMapLoad(bool useThreading);
	};

	class StateMapLoadFactory : public wp::application::StateFactory
	{
	protected:

		wp::Logger* mwLogger;

		wp::application::resourcesystem::ResourceManager* mwResourceMgr;

		bool mUseThreading;

	public:

		StateMapLoadFactory(wp::Logger* logger, wp::application::resourcesystem::ResourceManager* resourceMgr, bool useThreading)
			: wp::application::StateFactory("MapLoad")
			, mwLogger(logger)
			, mwResourceMgr(resourceMgr)
			, mUseThreading(useThreading)
		{
		}

		wp::application::State* createState()
		{
			auto state = new StateMapLoad(mUseThreading);
			state->setLogger(mwLogger);
			return state;
		}
	};


} // applib