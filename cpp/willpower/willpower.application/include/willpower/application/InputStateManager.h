#pragma once

#include <string>
#include <map>
#include <vector>

#include "willpower/application/Platform.h"
#include "willpower/application/Key.h"
#include "willpower/application/MouseButton.h"

namespace WP_NAMESPACE
{
	namespace application
	{

		/*
		Rewrite this.  Need:
		- Named state maps to a combination of keys and/or buttons/wheel plus optionally, key modifiers.
		- Need to differentiate between constant state (eg movement) and state-on, state-off.
		*/

		class WP_APPLICATION_API InputStateManager
		{
			struct StateDefinition
			{
				std::vector<Key> keysPressed, keysReleased, keysDown;
				std::vector<MouseButton> buttonsPressed, buttonsReleased, buttonsDown;
				bool mouseWheelUp, mouseWheelDown;
				uint32_t keyModifiers;
				bool disableInGui;
				bool active;
			};

		private:

			int mKeyState[(int)Key::NUMKEYS], mPrevKeyState[(int)Key::NUMKEYS];

			int mButtonState[(int)MouseButton::NUMBUTTONS], mPrevButtonState[(int)MouseButton::NUMBUTTONS];

			uint32_t mKeyModifiers, mPrevKeyModifiers;

			int mMouseWheelUp, mMouseWheelDown;

			float mMouseX, mMouseY;

			float mMouseDeltaX, mMouseDeltaY;

			std::map<std::string, StateDefinition> mStates;

		public:

			InputStateManager();

			void registerState(std::string const& name,
				std::vector<Key> const& keysPressed,
				std::vector<Key> const& keysReleased,
				std::vector<Key> const& keysDown,
				std::vector<MouseButton> const& buttonsPressed,
				std::vector<MouseButton> const& buttonsReleased,
				std::vector<MouseButton> const& buttonsDown,
				bool mouseWheelUp,
				bool mouseWheelDown,
				uint32_t keyModifiers,
				bool disableInGui = false);

			void unregisterState(std::string const& name);

			void injectKeyInput(KeyEvent evt, Key key, KeyModifiers modifiers);

			void injectMouseInput(MouseButtonEvent evt, MouseButton button, KeyModifiers modifiers);

			void injectMouseWheelInput(int y);

			void setMousePosition(float x, float y);

			bool stateActive(std::string const& state) const;

			std::vector<std::string> getActiveStates() const;

			void mousePosition(float* x, float *y) const;

			std::vector<std::string> process(bool guiActive, float* mouseDeltaX, float* mouseDeltaY);
		};

	} // application
} // WP_NAMESPACE

