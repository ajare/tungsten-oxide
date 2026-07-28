#pragma once

#include <vector>
#include <deque>
#include <mutex>

#include <mpp/Camera.h>
#include <mpp/ResourceWrangler.h>

#include <willpower/application/StateFactory.h>

#include <willpower/common/AccelerationGrid.h>

#include <applib/StatePlay.h>

#include "imgui/imgui.h"

#include "Platform.h"
#include "Map.h"
#include "DisplayMessage.h"


class TmResourceWrangler : public mpp::ResourceWrangler {
public:

	TmResourceWrangler() 
		: ResourceWrangler("TungstenMonoxidePlay")
	{
	}
};


class APPLICATION_API StatePlayTungstenMonoxide : public applib::StatePlay
{
	double mGlobalTime;

	mpp::CameraPtr mCamera3d;

	bool mExitScheduled;

	TmResourceWrangler mWrangler;

	mpp::ResourcePtr mTrackModel;

private:

	void createCamera();
	
	void registerInput() override;

	void createGameObjects(wp::application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args) override;

	void destroyGameObjects() override;

	void setupEntityFacades() override;

	void setupEntities() override;

	void updatePreInput(float frameTime) override;

	void updatePreEntities(float frameTime) override;

	void updatePostEntities(float frameTime) override;

	void updateAudio(float frameTime);

	Map* getMap();

	Map const* getMap() const;

	void exit();

protected:

	void updateActions(std::vector<std::string> const& activeStates, float frameTime) override;

	void updatePreRenderers(float frameTime) override;

	void setupScene() override;

	void setup(wp::application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args) override;

	void suspendImpl(void* args = nullptr) override;

	void resumeImpl(void* args) override;

	void updateImpl(float frameTime) override;

	void renderImpl(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) override;

public:

	StatePlayTungstenMonoxide();

	~StatePlayTungstenMonoxide();

	std::vector<std::string> getDebuggingText() const override;
};

class StatePlayTungstenMonoxideFactory : public wp::application::StateFactory
{
	wp::Logger* mLogger;

public:

	explicit StatePlayTungstenMonoxideFactory(wp::Logger* logger)
		: wp::application::StateFactory("Play")
		, mLogger(logger)
	{
	}

	wp::application::State* createState()
	{
		auto state = new StatePlayTungstenMonoxide();
		state->setLogger(mLogger);
		return state;
	}
};
