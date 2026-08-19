#include <stdexcept>

#include <mpp/helper/OrthoCamera.h>

#include <willpower/application/StateExceptions.h>
#include <willpower/application/ServiceLocator.h>
#include <willpower/application/ApplicationSettings.h>

#include "State.h"

namespace applib
{

	using namespace std;
	using namespace wp;

	State::State(string const& name)
		: application::State(name)
		, mwLogger(nullptr)
		, mwResourceMgr(nullptr)
		, mwAudioSystem(nullptr)
		, mwRenderSystem(nullptr)
		, mwRenderResourceMgr(nullptr)
	{
		mTransitionData.prevStateName = name;
	}

	wp::Vector2 State::getWindowSize() const
	{
		return {
			(float)mwRenderSystem->getWindowWidth(),
			(float)mwRenderSystem->getWindowHeight()
		};
	}

	void State::setLogger(wp::Logger* logger)
	{
		mwLogger = logger;
	}

	void State::enterImpl(wp::application::resourcesystem::ResourceManager* resourceMgr, wp::application::AudioSystem* audioSystem, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args)
	{
		mwLogger->info("Entering state: " + getName());

		mwResourceMgr = resourceMgr;
		mwAudioSystem = audioSystem;
		mwRenderSystem = renderSystem;
		mwRenderResourceMgr = renderResourceMgr;

		// Create MPP objects
		mRenderPipeline = selectRenderPipeline(renderSystem);
		if (!mRenderPipeline)
		{
			throw runtime_error("State '" + getName() + "' render pipeline selection returned null.");
		}

		mScene = renderSystem->createScene("Default");
		mScene->load();

		mCamera = make_shared<mpp::helper::OrthoCamera>(
			glm::vec2(0.0f, 0.0f),
			renderSystem->getWindowWidth(),
			renderSystem->getWindowHeight());

		// Implementation-specific setup
		setup(resourceMgr, renderSystem, renderResourceMgr, args);
	}

	mpp::RenderPipelinePtr State::selectRenderPipeline(mpp::RenderSystem* renderSystem)
	{
		return renderSystem->getOrCreateRenderPipeline(getName());
	}

	void State::exitImpl()
	{
		// Implementation-specific teardown
		teardown();

		mwLogger->info("Exiting state: " + getName());

		// Clean up
		mCamera.reset();

		mScene->unload();

		// Pipelines are managed by the RenderSystem, so resetting here won't release it, as it might also be used by something else.
		// We really need to reference count this, excepting the one stored in the RenderSystem, so if resetting ever takes it down
		// to 1, we can delete it from the RenderSystem.
		mRenderPipeline.reset();
	}

	void State::loadAllReferencedResources()
	{
		auto mppResources = mwRenderResourceMgr->getAllReferencedResources();
		for (auto res : mppResources)
		{
			if (!res->isLoaded())
			{
				res->load();
			}
		}
	}

	vector<string> State::getDebuggingText() const
	{
		vector<string> lines;
		return lines;
	}

} // applib