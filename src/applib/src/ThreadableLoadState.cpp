#include <willpower/application/StateExceptions.h>
#include <willpower/application/ServiceLocator.h>
#include <willpower/application/ApplicationSettings.h>

#include "ThreadableLoadState.h"

namespace applib
{

	using namespace std;
	using namespace wp;

	ThreadableLoadState::ThreadableLoadState(string const& name, Action action, bool useThreading)
		: State(name)
		, mAction(action)
		, mStatus(Status::Idle)
		, mWillpowerComplete(true)
		, mUseThreading(useThreading)
		, mThread(nullptr)
	{
	}

	bool ThreadableLoadState::usingThreading() const
	{
		return mUseThreading;
	}

	void ThreadableLoadState::startProcessingWillpowerResources()
	{
		lock_guard<mutex> lock(mFlagMutex);
		mWillpowerComplete = false;
	}

	void ThreadableLoadState::addPendingResourceName(string const& name)
	{
		lock_guard<mutex> lock(mPendingResourceMutex);
		mPendingResourceNames.insert(name);
	}

	bool ThreadableLoadState::removePendingResourceName(string const& name)
	{
		lock_guard<mutex> lock(mPendingResourceMutex);
		mPendingResourceNames.erase(name);
		return mPendingResourceNames.empty();
	}

	bool ThreadableLoadState::isPendingResourceCountZero() const
	{
		lock_guard<mutex> lock(mPendingResourceMutex);
		return mPendingResourceNames.empty();
	}

	void ThreadableLoadState::finishProcessingWillpowerResources()
	{
		lock_guard<mutex> lock(mFlagMutex);
		mWillpowerComplete = true;
	}

	bool ThreadableLoadState::isWillpowerResourceProcessingFinished() const
	{
		lock_guard<mutex> lock(mFlagMutex);
		return mWillpowerComplete;
	}

	void ThreadableLoadState::addText(string const& line, bool log)
	{
		if (log)
		{
			mwLogger->info(line);
		}

		lock_guard<mutex> lock(mTextMutex);
		mText.push_back(line);
	}

	void ThreadableLoadState::loadResourceCallback(application::resourcesystem::ResourcePtr resource, application::resourcesystem::ResourceState state, bool rootResource)
	{
		WP_UNUSED(rootResource);

		auto const& resName = resource->getName();

		switch (state)
		{
		case application::resourcesystem::ResourceState::Loading:
			addText("Loading WP/" + resName);
			break;
		case application::resourcesystem::ResourceState::Loaded:
			removePendingResourceName(resName);
			break;
		default:
			return;
		}

		// Check to see if we're done
		if (isPendingResourceCountZero())
		{
			finishProcessingWillpowerResources();
		}
	}

	void ThreadableLoadState::unloadResourceCallback(application::resourcesystem::ResourcePtr resource, application::resourcesystem::ResourceState state, bool rootResource)
	{
		WP_UNUSED(rootResource);

		auto const& resName = resource->getName();

		switch (state)
		{
		case application::resourcesystem::ResourceState::Unloading:
			addText("Unloading WP/" + resName);
			break;
		case application::resourcesystem::ResourceState::Created:
			addText("Unloaded WP/" + resName);
			break;
		case application::resourcesystem::ResourceState::Releasing:
			addText("Releasing WP/" + resName);
			break;
		case application::resourcesystem::ResourceState::Released:
			removePendingResourceName(resName);
			break;
		default:
			return;
		}

		// Check to see if we're done
		if (isPendingResourceCountZero())
		{
			finishProcessingWillpowerResources();
		}
	}

	vector<ThreadableLoadState::ThreadableWorkFunction> ThreadableLoadState::getPreWork(StateTransitionData* transitionData)
	{
		return vector<ThreadableLoadState::ThreadableWorkFunction>();
	}

	void ThreadableLoadState::start(LoadFunction func)
	{
		auto wrappedFunc = [this, func]() {
			try
			{
				func();
			}
			catch (exception& e)
			{
				mWorkerException = make_exception_ptr(e);
				this->finishProcessingWillpowerResources();
			}
		};

		mStatus = Status::Initialise;
		startProcessingWillpowerResources();

		if (usingThreading())
		{
			mThread = new thread(wrappedFunc);
		}
		else
		{
			wrappedFunc();
		}
	}

	void ThreadableLoadState::setup(application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args)
	{
		WP_UNUSED(args);

		auto customPreWork = getPreWork(static_cast<StateTransitionData*>(args));
		for (auto func : customPreWork)
		{
			func(usingThreading());
		}

		mCustomPostWork.clear();

		start(getWorkFunction(resourceMgr, renderSystem, renderResourceMgr, args));
	}

	void ThreadableLoadState::teardown()
	{
	}

	void ThreadableLoadState::exitImpl()
	{
		if (usingThreading())
		{
			mThread->join();
			delete mThread;
			mThread = nullptr;
		}

		State::exitImpl();
	}

	void ThreadableLoadState::processPostWork(vector<ThreadableWorkFunction> funcs)
	{
		if (usingThreading())
		{
			for (auto func : funcs)
			{
				func(true);
			}
		}
		else
		{
			mCustomPostWork.clear();

			// Reverse order
			for (auto rit = funcs.rbegin(); rit != funcs.rend(); rit++)
			{
				mCustomPostWork.push_back(*rit);

			}
		}
	}


	void ThreadableLoadState::updateImpl(float frameTime)
	{
		WP_UNUSED(frameTime);

		switch (mStatus)
		{
		case Status::Idle:
			break;

		case Status::Initialise:
			mStatus = Status::ProcessingWillpowerResources;

			// Set up resources to process - unload second as it's processed in reverse.
			if (!usingThreading())
			{
				mWillpowerResourcesToProcess.clear();

				for (auto res : mWillpowerResourcesToLoad)
				{
					mWillpowerResourcesToProcess.push_back(make_pair(res, Action::Load));
				}
				for (auto res : mWillpowerResourcesToUnload)
				{
					mWillpowerResourcesToProcess.push_back(make_pair(res, Action::Unload));
				}
			}
			break;

		case Status::ProcessingWillpowerResources:
			if (!usingThreading())
			{
				if (!mWillpowerResourcesToProcess.empty())
				{
					auto item = mWillpowerResourcesToProcess.back();
					auto res = item.first;
					auto action = item.second;

					switch (action)
					{
					case Action::Load:
						addText("Loading WP/" + res->getName());
						mwResourceMgr->createResource(res);
						mwResourceMgr->loadResource(res);
						mwResourceMgr->acquireResource(res);
						break;

					case Action::Unload:
						addText("Unloading WP/" + res->getName());
						mwResourceMgr->releaseResource(res);
						break;
					}

					mWillpowerResourcesToProcess.pop_back();
				}
				else
				{
					finishProcessingWillpowerResources();
				}
			}

			if (isWillpowerResourceProcessingFinished())
			{
				// Did loading encounter an exception?
				if (mWorkerException)
				{
					rethrow_exception(mWorkerException);
				}

				mStatus = Status::ProcessingCustomPostWork;
			}
			break;

		case Status::ProcessingCustomPostWork:
			if (!mCustomPostWork.empty())
			{
				auto func = mCustomPostWork.back();
				func(false);
				mCustomPostWork.pop_back();
			}
			else
			{
				// Now get MPP resources to process
				vector<mpp::ResourcePtr> mppResources;
				mMppResourcesToProcess.clear();

				switch (mAction)
				{
				case Action::Load:
					mppResources = mwRenderResourceMgr->getAllReferencedResources();
					copy_if(mppResources.begin(), mppResources.end(), back_inserter(mMppResourcesToProcess), [](auto res)
					{
						return !res->isLoaded();
					});
					break;

				case Action::Unload:
					mppResources = mwRenderResourceMgr->getAllUnreferencedResources();
					copy_if(mppResources.begin(), mppResources.end(), back_inserter(mMppResourcesToProcess), [](auto res)
					{
						return res->isLoaded();
					});
					break;
				}

				mStatus = Status::ProcessingMppResources;
			}
			break;

		case Status::ProcessingMppResources:
			if (!mMppResourcesToProcess.empty())
			{
				auto res = mMppResourcesToProcess.back();

				switch (mAction)
				{
				case Action::Load:
					addText("Loading MPP/" + res->getName());
					res->load();
					break;

				case Action::Unload:
					addText("Unloading MPP/" + res->getName());
					res->unload();
					break;
				}

				mMppResourcesToProcess.pop_back();
			}
			else
			{
				mStatus = Status::ProcessingComplete;
			}
			break;

		case Status::ProcessingComplete:
			throw application::ReturnFromStateException(&mTransitionData);
			break;
		}
	}

	void ThreadableLoadState::renderImpl(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
	{
		WP_UNUSED(resourceMgr);

		auto const& viewPos = mCamera->getPosition();
		auto halfWindowSize = getWindowSize() / 2.0f;

		renderSystem->renderScene(
			mScene,
			mCamera,
			glm::vec2(viewPos.x - halfWindowSize.x,	viewPos.y - halfWindowSize.y),
			getName());

		// Render text
		lock_guard<mutex> lock(mTextMutex);
		renderSystem->renderText(mText, 8, 0, mpp::Colour::White);
	}

} // applib