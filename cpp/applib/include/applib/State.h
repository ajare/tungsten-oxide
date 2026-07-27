#pragma once

#include <mpp/Scene.h>

#include <willpower/common/Logger.h>

#include <willpower/application/State.h>
#include <willpower/application/AudioSystem.h>

#include "Platform.h"
#include "StateTransitionData.h"
#include "Model.h"

namespace applib
{

	class APPLIB_API State : public wp::application::State
	{
	protected:

		wp::Logger* mwLogger;

		mpp::RenderPipelinePtr mRenderPipeline;

		mpp::ScenePtr mScene;

		mpp::CameraPtr mCamera;

		wp::application::resourcesystem::ResourceManager* mwResourceMgr;

		wp::application::AudioSystem* mwAudioSystem;

		mpp::RenderSystem* mwRenderSystem;

		mpp::ResourceManager* mwRenderResourceMgr;

		StateTransitionData mTransitionData;

	protected:

		void enterImpl(wp::application::resourcesystem::ResourceManager* resourceMgr, wp::application::AudioSystem* audioSystem, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args = nullptr) override;

		void exitImpl() override;

		virtual void setup(wp::application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args = nullptr) = 0;

		virtual void teardown() = 0;

		void loadAllReferencedResources();

		std::vector<std::string> getDebuggingText() const override;

	public:

		explicit State(std::string const& name);

		wp::Vector2 getWindowSize() const;

		void setLogger(wp::Logger* logger);
	};

} // applib