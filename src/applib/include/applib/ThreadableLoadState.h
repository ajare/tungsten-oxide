#pragma once

#include <vector>
#include <deque>
#include <thread>
#include <mutex>

#include <willpower/application/StateFactory.h>
#include <willpower/application/resourcesystem/ResourceManager.h>
#include <willpower/application/resourcesystem/ResourceCallback.h>

#include "Platform.h"
#include "State.h"

namespace applib
{

	class APPLIB_API ThreadableLoadState : public State
	{
	public:

		enum class Action
		{
			Load,
			Unload
		};

	private:

		enum class Status
		{
			Idle,
			Initialise,
			ProcessingWillpowerResources,
			ProcessingCustomPostWork,
			ProcessingMppResources,
			ProcessingComplete
		};

	public:

		typedef std::function<void()> LoadFunction;

		typedef std::function<void(bool)> ThreadableWorkFunction;

	private:

		Action mAction;

		Status mStatus;

		bool mWillpowerComplete;

		bool mUseThreading;

		std::vector<std::string> mText;

		std::thread* mThread;

		mutable std::mutex mTextMutex, mFlagMutex, mPendingResourceMutex;

		std::exception_ptr mWorkerException;

		std::set<std::string> mPendingResourceNames;

		std::string mNextState;

		void* mNextStateArgs;

		std::vector<mpp::ResourcePtr> mMppResourcesToProcess;

		std::vector<std::pair<wp::application::resourcesystem::ResourcePtr, Action>> mWillpowerResourcesToProcess;

	protected:

		std::vector<wp::application::resourcesystem::ResourcePtr> mWillpowerResourcesToLoad, mWillpowerResourcesToUnload;

		std::vector<ThreadableWorkFunction> mCustomPostWork;

	private:

		bool isWillpowerResourceProcessingFinished() const;

		virtual LoadFunction getWorkFunction(wp::application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args) = 0;

	protected:

		virtual std::vector<ThreadableWorkFunction> getPreWork(StateTransitionData* transitionData);

		void loadResourceCallback(wp::application::resourcesystem::ResourcePtr resource, wp::application::resourcesystem::ResourceState state, bool rootResource);

		void unloadResourceCallback(wp::application::resourcesystem::ResourcePtr resource, wp::application::resourcesystem::ResourceState state, bool rootResource);

		void addPendingResourceName(std::string const& name);

		bool removePendingResourceName(std::string const& name);

		bool isPendingResourceCountZero() const;

		void startProcessingWillpowerResources();

		void finishProcessingWillpowerResources();

		void processPostWork(std::vector<ThreadableWorkFunction> funcs);

		void start(LoadFunction func);

		void addText(std::string const& line, bool log = false);

		void exitImpl() override;

		void updateImpl(float frameTime) override;

		void renderImpl(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) override;

	public:

		ThreadableLoadState(std::string const& name, Action action, bool useThreading);

		void setup(wp::application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args);

		void teardown() override;

		bool usingThreading() const;
	};


} // applib