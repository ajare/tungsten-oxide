#include <willpower/application/StateExceptions.h>

#include "StateManager.h"
#include "ExitApplicationException.h"

using namespace wp;
using namespace std;

StateManager::StateManager(application::resourcesystem::ResourceManager* resourceMgr, wp::application::AudioSystem* audioSystem, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr)
	: mCurState(nullptr)
	, mResourceMgr(resourceMgr)
	, mAudioSystem(audioSystem)
	, mRenderSystem(renderSystem)
	, mRenderResourceMgr(renderResourceMgr)
{
}

StateManager::~StateManager()
{
	// Unwind state stack.
	while (!mActiveStates.empty())
	{
		auto state = mActiveStates.back();
		
		state->_exit();
		delete state;
		
		mActiveStates.pop_back();
	}
}

void StateManager::registerStateFactory(application::StateFactory* factory)
{
	string const& type = factory->getType();
	
	// Check initial state
	if (mStateFactories.empty())
	{
		mInitialState = type;
	}

	// Make sure one is not already registered
	if (mStateFactories.find(type) != mStateFactories.end())
	{
		string errMsg = "State factory '" + type + "' already registered.";
		throw exception(errMsg.c_str());
	}
	
	mStateFactories[type] = factory;
}

application::StateFactory* StateManager::getStateFactory(string const& type)
{
	auto it = mStateFactories.find(type);
	if (it == mStateFactories.end())
	{
		string errMsg = "State factory '" + type + "' not registered.";
		throw exception(errMsg.c_str());
	}

	return it->second;
}

void StateManager::createCurrentState(string const& type)
{
	auto stateFactory = getStateFactory(type);
	mCurState = stateFactory->createState();
	mActiveStates.push_back(mCurState);
}

application::State* StateManager::exitCurrentState(bool deleteState)
{
	auto oldState = mCurState;

	if (mCurState)
	{
		mCurState->_exit();

		if (deleteState)
		{
			delete mCurState;
		}

		mActiveStates.pop_back();
		mCurState = nullptr;
	}

	return deleteState ? nullptr : oldState;
}

void StateManager::moveToState(string const& nextState, void* args)
{
	exitCurrentState(true);
	createCurrentState(nextState);
	mCurState->_enter(mResourceMgr, mAudioSystem, mRenderSystem, mRenderResourceMgr, args);
}

void StateManager::suspendAndMoveToState(string const& nextState, bool suspendEvents, bool suspendUpdate, bool suspendRender, void* args)
{
	if (mCurState)
	{
		mCurState->_suspend(suspendEvents, suspendUpdate, suspendRender, args);
	}

	createCurrentState(nextState);
	mCurState->_enter(mResourceMgr, mAudioSystem, mRenderSystem, mRenderResourceMgr, args);
}

void StateManager::returnFromState(void* args)
{
	assert(mCurState && "StateManager::returnFromState() no current state.");

	auto oldState = exitCurrentState(false);

	if (mActiveStates.empty())
	{
		// No states left, so exit the application
		delete oldState;
		throw ExitApplicationException(0, "State stack fully unwound.");
	}
	else
	{
		mCurState = mActiveStates.back();
		mCurState->_resume(args);
		delete oldState;
	}
}

void StateManager::executeStateChangeableAction(function<void()> func)
{
	try
	{
		func();
	}
	catch (application::MoveToStateException& e)
	{
		moveToState(e.getNextState(), e.getArguments());
	}
	catch (application::SuspendAndMoveToStateException& e)
	{
		suspendAndMoveToState(
			e.getNextState(),
			e.getEventsSuspended(),
			e.getUpdateSuspended(),
			e.getRenderSuspended(),
			e.getArguments());
	}
	catch (application::ReturnFromStateException& e)
	{
		returnFromState(e.getArguments());
	}
}

void StateManager::enterInitialState(string const& gameResourceName)
{
	string resourceName = gameResourceName;
	moveToState(mInitialState, &resourceName);
}

void StateManager::injectKeyInput(application::KeyEvent evt, application::Key key, application::KeyModifiers modifiers)
{
	executeStateChangeableAction([this, evt, key, modifiers]()
	{ 
		mCurState->_injectKeyInput(evt, key, modifiers); 
	});
}

void StateManager::injectMouseButtonInput(application::MouseButtonEvent evt, application::MouseButton mouseButton, wp::application::KeyModifiers modifiers)
{
	if (imGuiActive())
	{
		return;
	}

	executeStateChangeableAction([this, evt, mouseButton, modifiers]()
	{ 
		mCurState->_injectMouseButtonInput(evt, mouseButton, modifiers);
	});
}

void StateManager::injectMouseWheelInput(int y)
{
	if (imGuiActive())
	{
		return;
	}

	executeStateChangeableAction([this, y]()
	{
		mCurState->_injectMouseWheelInput(y);
	});
}

void StateManager::injectMouseButtonDoubleClicked(application::MouseButton mouseButton)
{
	if (imGuiActive())
	{
		return;
	}

	executeStateChangeableAction([this, mouseButton]()
	{ 
		mCurState->_injectMouseButtonDoubleClicked(mouseButton); 
	});
}

void StateManager::injectMouseDragStarted(application::MouseButton mouseButton, int startPositionX, int startPositionY, float dragX, float dragY)
{
	if (imGuiActive())
	{
		return;
	}

	executeStateChangeableAction([this, mouseButton, startPositionX, startPositionY, dragX, dragY]()
	{ 
		mCurState->_injectMouseDragStarted(mouseButton, startPositionX, startPositionY, dragX, dragY); 
	});
}

void StateManager::injectMouseDragFinished(application::MouseButton mouseButton, int finishPositionX, int finishPositionY)
{
	if (imGuiActive())
	{
		return;
	}

	executeStateChangeableAction([this, mouseButton, finishPositionX, finishPositionY]()
	{
		mCurState->_injectMouseDragFinished(mouseButton, finishPositionX, finishPositionY);
	});
}

void StateManager::injectMouseMotionInput(float positionX, float positionY)
{
	if (imGuiActive())
	{
		return;
	}

	executeStateChangeableAction([this, positionX, positionY]()
	{
		mCurState->_injectMouseMotionInput(positionX, positionY);
	});
}

vector<string> StateManager::getDebuggingText() const
{
	return mCurState->getDebuggingText();
}

void StateManager::update(float frameTime)
{
	executeStateChangeableAction([this, frameTime]()
	{
		for (auto state: mActiveStates)
		{
			state->_update(frameTime);
		}
	});
}

bool StateManager::imGuiActive() const
{
	return mCurState->_imGuiActive();
}

void StateManager::renderImGui(float frameTime, void* imguiCtx, void* imPlotCtx, void* allocFunc, void* freeFunc, void* userData)
{
	mCurState->_renderImGui(frameTime, imguiCtx, imPlotCtx, allocFunc, freeFunc, userData);
}

void StateManager::render(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
{
	for (auto state: mActiveStates)
	{
		state->_render(renderSystem, resourceMgr);
	}
}
