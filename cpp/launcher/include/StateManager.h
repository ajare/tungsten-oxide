#pragma once

#include <map>
#include <string>
#include <deque>
#include <functional>

#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>

#include <willpower/application/State.h>
#include <willpower/application/StateFactory.h>
#include <willpower/application/resourcesystem/ResourceManager.h>
#include <willpower/application/AudioSystem.h>


class StateManager
{
	std::map<std::string, wp::application::StateFactory*> mStateFactories;

	std::string mInitialState;

	wp::application::State* mCurState;

	std::deque<wp::application::State*> mActiveStates;

	// Resource manager
	wp::application::resourcesystem::ResourceManager* mResourceMgr;

	// Audio system
	wp::application::AudioSystem* mAudioSystem;

	// Render system
	mpp::RenderSystem* mRenderSystem;

	mpp::ResourceManager* mRenderResourceMgr;

private:

	wp::application::StateFactory* getStateFactory(std::string const& type);

	void createCurrentState(std::string const& type);

	void moveToState(std::string const& nextState, void* args);

	void suspendAndMoveToState(std::string const& nextState, bool suspendEvents, bool suspendUpdate, bool suspendRender, void* args);

	void returnFromState(void* args);

	void executeStateChangeableAction(std::function<void()> func);

	wp::application::State* exitCurrentState(bool deleteState);

public:

	StateManager(wp::application::resourcesystem::ResourceManager* resourceMgr, wp::application::AudioSystem* audioSystem, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr);

	~StateManager();

	void registerStateFactory(wp::application::StateFactory* factory);

	void enterInitialState(std::string const& gameResourceName);

	void injectKeyInput(wp::application::KeyEvent evt, wp::application::Key key, wp::application::KeyModifiers modifiers);

	void injectMouseButtonInput(wp::application::MouseButtonEvent evt, wp::application::MouseButton mouseButton, wp::application::KeyModifiers modifiers);

	void injectMouseWheelInput(int y);

	void injectMouseButtonDoubleClicked(wp::application::MouseButton mouseButton);

	void injectMouseDragStarted(wp::application::MouseButton mouseButton, int startPositionX, int startPositionY, float dragX, float dragY);

	void injectMouseDragFinished(wp::application::MouseButton mouseButton, int finishPositionX, int finishPositionY);

	void injectMouseMotionInput(float positionX, float positionY);

	std::vector<std::string> getDebuggingText() const;
	
	void update(float frameTime);

	bool imGuiActive() const;

	void renderImGui(float frameTime, void* imguiCtx, void* imPlotCtx, void* allocFunc, void* freeFunc, void* userData);

	void render(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr);
};