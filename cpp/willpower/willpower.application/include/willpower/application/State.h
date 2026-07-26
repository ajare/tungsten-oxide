#pragma once

#include <string>

#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>

#include "willpower/application/Platform.h"
#include "willpower/application/Key.h"
#include "willpower/application/MouseButton.h"
#include "willpower/application/resourcesystem/ResourceManager.h"

#pragma warning(push)
#pragma warning(disable: 4100) // Unreferenced formal parameter

namespace WP_NAMESPACE
{
	namespace application
	{

		/**	\class State
		*   \brief Base class for implemented Application states.
		*
		*   States are used to specify how the Application behaves at a given point, in three places:
		*   <list type="bullet">
		*     <item>Input handling</item>
		*     <item>Logic</item>
		*     <item>Rendering</item>
		*   </list>
		*
		*  States can be stacked so that multiple states can run logic and be rendered at the same time,
		*  however only one state can process input at any time.  States are designed to be handled by a
		*  manager, and not manipulated directly.
		*/
		class WP_APPLICATION_API State
		{
			std::string mName;

			bool mEventsActive, mUpdateActive, mRenderActive;

		private:

			virtual void enterImpl(resourcesystem::ResourceManager* resourceMgr, AudioSystem* audioSystem, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args = nullptr) {}

			virtual void exitImpl() {}

			virtual void suspendImpl(void* args = nullptr) {}

			virtual void resumeImpl(void* args = nullptr) {}

			virtual void injectKeyInputImpl(application::KeyEvent evt, Key key, KeyModifiers modifiers) {}

			virtual void injectMouseButtonInputImpl(application::MouseButtonEvent evt, MouseButton mouseButton, KeyModifiers modifiers) {}

			virtual void injectMouseWheelInputImpl(int y) {}

			virtual void injectMouseButtonDoubleClickedImpl(MouseButton mouseButton) {}

			virtual void injectMouseDragStartedImpl(MouseButton mouseButton, int startPositionX, int startPositionY, float dragX, float dragY) {}

			virtual void injectMouseDragFinishedImpl(MouseButton mouseButton, int finishPositionX, int finishPositionY) {}
			
			virtual void injectMouseMotionInputImpl(float positionX, float positionY) {}

			virtual void updateImpl(float frameTime) {}

			virtual void renderImpl(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) {}

		public:

			/**	\brief Constructor.
			*
			*	\param name State name.
			*/
			explicit State(std::string const& name);

			/**	\brief Destructor.
			*/
			virtual ~State() = default;

			/**	\brief Returns State name.
			*
			*	\return the State name.
			*/
			std::string const& getName() const;

			/** \brief Returns a list of lines displaying debug output.
			*
			* \return debug output.
			*/
			virtual std::vector<std::string> getDebuggingText() const;

			/**	\brief Run state logic.
			*
			*   To be called by external manager.
			*
			*	\param frameTime the time, in milliseconds, to run the State-specific logic for.
			*/
			void _update(float frameTime);

			/**	\brief Is ImGui being displayed
			*
			*   \return true if rendering was done, else false
			*/
			virtual bool _imGuiActive() const;

			/**	\brief Render ImGui objects.
			*
			*   To be called by external manager.
			*
			*	\param frameTime the time, in milliseconds, to run the State-specific logic for.
			*   \param ctx ImGui context.
			*   \param allocFunc ImGui allocator
			*   \param freeFunc ImGui deallocator function
			*   \param userData userData for allocation
			*   \return true if rendering was done, else false
			*/			
			virtual void _renderImGui(float frameTime, void* imGuiCtx, void* imPlotCtx, void* allocFunc, void* freeFunc, void* userData);

			/**	\brief Render State.
			*
			*   To be called by external manager.
			*
			*	\param renderSystem RenderSystem object to use.
			*	\param resourceMgr RenderSystem ResourceManager object to use.
			*/
			void _render(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr);

			/**	\brief Injects a key event into the state.
			*
			*   To be called by external manager.  This event can then be processed by the subclass.
			*
			*	\param evt The type of event
			*	\param key the key in question
			*	\param modifiers which modifiers were active
			*/
			void _injectKeyInput(KeyEvent evt, Key key, KeyModifiers modifiers);

			/**	\brief Injects a mousebutton event into the state.
			*
			*   To be called by external manager.  This event can then be processed by the subclass.
			*
			*	\param evt The type of event
			*	\param mouseButton the button in question
			*	\param modifiers which modifiers were active
			*/
			void _injectMouseButtonInput(MouseButtonEvent evt, MouseButton mouseButton, KeyModifiers modifiers);

			/**	\brief Injects mousewheel input into the state.
			*
			*   To be called by external manager.  This event can then be processed by the subclass.
			*
			*	\param y positive for up, negative for down
			*/
			void _injectMouseWheelInput(int y);

			/**	\brief Injects a mousebutton double-click event into the state.
			*
			*   To be called by external manager.  This event can then be processed by the subclass.
			*
			*	\param mouseButton the button in question
			*/
			void _injectMouseButtonDoubleClicked(MouseButton mouseButton);

			/**	\brief Signals that the mouse has started to be dragged
			*
			*   To be called by external manager.  This event can then be processed by the subclass.
			*
			*	\param mouseButton the button in question
			*	\param startPositionX the x screen position of the mouse when the button was pressed
			*	\param startPositionY the y screen position of the mouse when the button was pressed
			*	\param dragX the horizontal movement of the mouse since the button was pressed
			*	\param dragY the vertical movement of the mouse since the button was pressed
			*/
			void _injectMouseDragStarted(MouseButton mouseButton, int startPositionX, int startPositionY, float dragX, float dragY);

			/**	\brief Signals that the mouse has finished being dragged
			*
			*   To be called by external manager.  This event can then be processed by the subclass.
			*
			*	\param mouseButton the button in question
			*	\param finishPositionX the x screen position of the mouse when the button was released
			*	\param finishPositionY the y screen position of the mouse when the button was released
			*/
			void _injectMouseDragFinished(MouseButton mouseButton, int finishPositionX, int finishPositionY);

			/**	\brief Signals that the mouse was moved
			*
			*   To be called by external manager.  This event can then be processed by the subclass.
			*
			*	\param positionX the horizontal movement of the mouse since the button was pressed
			*	\param positionY the vertical movement of the mouse since the button was pressed
			*/
			void _injectMouseMotionInput(float positionX, float positionY);

			/**	\brief Enters the state.
			*
			*   To be called by external manager.
			*
			*	\param args arguments passed from the previous state.
			*/
			void _enter(resourcesystem::ResourceManager* resourceMgr, wp::application::AudioSystem* audioSystem, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args = nullptr);

			/**	\brief Exits the state.
			*
			*   To be called by external manager.
			*/
			void _exit();

			/**	\brief Suspends the state.
			*
			*   To be called by external manager.
			*
			*	\param suspendEvents whether to keep processing events or not.
			*	\param suspendUpdate whether to keep processing logic or not.
			*	\param suspendRender whether to keep rendering or not.
			*	\param args arguments passed.
			*/
			void _suspend(bool suspendEvents = true, bool suspendUpdate = true, bool suspendRender = true, void * args = nullptr);

			/**	\brief Resumes the state.
			*
			*   To be called by external manager.
			*
			*	\param args arguments passed.
			*/
			void _resume(void* args = nullptr);
		};

	} // application
} // WP_NAMESPACE

#pragma warning(pop)