#include "willpower/application/State.h"

using namespace std;

namespace WP_NAMESPACE
{
	namespace application
	{

		State::State(std::string const& name)
			: mName(name)
			, mEventsActive(false)
			, mUpdateActive(false)
			, mRenderActive(false)
		{
		}

		string const& State::getName() const
		{
			return mName;
		}


		vector<string> State::getDebuggingText() const
		{
			return vector<string>();
		}

		void State::_update(float frameTime)
		{
			if (mUpdateActive)
			{
				updateImpl(frameTime);
			}
		}

		bool State::_imGuiActive() const
		{
			return false;
		}

		void State::_renderImGui(float frameTime, void* imGuiCtx, void* imPlotCtx, void* allocFunc, void* freeFunc, void* userData)
		{
		}

		void State::_render(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
		{
			if (mRenderActive)
			{
				renderImpl(renderSystem, resourceMgr);
			}
		}

		void State::_injectKeyInput(KeyEvent evt, Key key, KeyModifiers modifiers)
		{
			injectKeyInputImpl(evt, key, modifiers);
		}

		void State::_injectMouseButtonInput(MouseButtonEvent evt, MouseButton mouseButton, KeyModifiers modifiers)
		{
			injectMouseButtonInputImpl(evt, mouseButton, modifiers);
		}

		void State::_injectMouseWheelInput(int y)
		{
			injectMouseWheelInputImpl(y);
		}

		void State::_injectMouseButtonDoubleClicked(MouseButton mouseButton)
		{
			injectMouseButtonDoubleClickedImpl(mouseButton);
		}

		void State::_injectMouseDragStarted(MouseButton mouseButton, int startPositionX, int startPositionY, float dragX, float dragY)
		{
			injectMouseDragStartedImpl(mouseButton, startPositionX, startPositionY, dragX, dragY);
		}

		void State::_injectMouseDragFinished(MouseButton mouseButton, int finishPositionX, int finishPositionY)
		{
			injectMouseDragFinishedImpl(mouseButton, finishPositionX, finishPositionY);
		}

		void State::_injectMouseMotionInput(float positionX, float positionY)
		{
			injectMouseMotionInputImpl(positionX, positionY);
		}

		void State::_enter(resourcesystem::ResourceManager* resourceMgr, wp::application::AudioSystem* audioSystem, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args)
		{
			mEventsActive = true;
			mUpdateActive = true;
			mRenderActive = true;
			enterImpl(resourceMgr, audioSystem, renderSystem, renderResourceMgr, args);
		}

		void State::_exit()
		{
			mEventsActive = false;
			mUpdateActive = false;
			mRenderActive = false;
			exitImpl();
		}

		void State::_suspend(bool suspendEvents, bool suspendUpdate, bool suspendRender, void* args)
		{
			mEventsActive = !suspendEvents;
			mUpdateActive = !suspendUpdate;
			mRenderActive = !suspendRender;
			suspendImpl(args);
		}

		void State::_resume(void* args)
		{
			mEventsActive = true;
			mUpdateActive = true;
			mRenderActive = true;
			resumeImpl(args);
		}

	} // application
} // WP_NAMESPACE