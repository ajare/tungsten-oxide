#include "willpower/application/InputStateManager.h"

#include "willpower/common/Exceptions.h"

using namespace std;

namespace WP_NAMESPACE
{
	namespace application
	{

		InputStateManager::InputStateManager()
			: mKeyModifiers(0)
			, mPrevKeyModifiers(0)
			, mMouseWheelUp(0)
			, mMouseWheelDown(0)
			, mMouseX(0.0f)
			, mMouseY(0.0f)
			, mMouseDeltaX(0.0f)
			, mMouseDeltaY(0.0f)
		{
			for (int i = 0; i < (int)Key::NUMKEYS; ++i)
			{
				mKeyState[i] = 0;
				mPrevKeyState[i] = 0;
			}

			for (int i = 0; i < (int)MouseButton::NUMBUTTONS; ++i)
			{
				mButtonState[i] = 0;
				mPrevButtonState[i] = 0;
			}
		}

		void InputStateManager::registerState(string const& name,
			vector<Key> const& keysPressed,
			vector<Key> const& keysReleased,
			vector<Key> const& keysDown,
			vector<MouseButton> const& buttonsPressed,
			vector<MouseButton> const& buttonsReleased,
			vector<MouseButton> const& buttonsDown,
			bool mouseWheelUp,
			bool mouseWheelDown,
			uint32_t keyModifiers,
			bool disableInGui)
		{
			StateDefinition stateDef
			{
				keysPressed,
				keysReleased,
				keysDown,
				buttonsPressed,
				buttonsReleased,
				buttonsDown,
				mouseWheelUp,
				mouseWheelDown,
				keyModifiers,
				disableInGui
			};

			mStates[name] = stateDef;
		}

		void InputStateManager::unregisterState(string const& name)
		{
			mStates.erase(name);
		}

		void InputStateManager::injectKeyInput(KeyEvent evt, Key key, application::KeyModifiers modifiers)
		{
			switch (evt)
			{
			case KeyEvent::Pressed:
				mKeyState[(int)key] = 1;
				break;

			case KeyEvent::Released:
				mKeyState[(int)key] = 0;
				break;
			}

			mKeyModifiers |= modifiers;
		}

		void InputStateManager::injectMouseInput(MouseButtonEvent evt, MouseButton button, KeyModifiers modifiers)
		{
			switch (evt)
			{
			case MouseButtonEvent::Pressed:
				mButtonState[(int)button] = 1;
				break;

			case MouseButtonEvent::Released:
				mButtonState[(int)button] = 0;
				break;
			}

			mKeyModifiers |= modifiers;
		}

		void InputStateManager::injectMouseWheelInput(int y)
		{
			if (y > 0)
			{
				mMouseWheelUp = 1;
			}
			if (y < 0)
			{
				mMouseWheelDown = 1;
			}
		}

		void InputStateManager::setMousePosition(float x, float y)
		{
			mMouseDeltaX = x - mMouseX;
			mMouseDeltaY = y - mMouseY;
			mMouseX = x;
			mMouseY = y;
		}

		bool InputStateManager::stateActive(string const& state) const
		{
			return mStates.find(state)->second.active;
		}

		vector<string> InputStateManager::getActiveStates() const
		{
			vector<string> states;

			for (auto item : mStates)
			{
				auto const&[name, def] = item;
				if (def.active)
				{
					states.push_back(name);
				}
			}

			return states;
		}

		void InputStateManager::mousePosition(float* x, float *y) const
		{
			if (x)
			{
				*x = mMouseX;
			}
			if (y)
			{
				*y = mMouseY;
			}
		}

		vector<string> InputStateManager::process(bool guiActive, float* mouseDeltaX, float* mouseDeltaY)
		{
			vector<string> activeStates;

			// Go over all states and see if any are active
			for (auto item : mStates)
			{
				auto&[name, stateDef] = item;

				stateDef.active = true;

				// Is the GUI active?
				if (guiActive && stateDef.disableInGui)
				{
					stateDef.active = false;
					goto next_state;
				}

				// Check keys
				for (auto key : stateDef.keysPressed)
				{
					if (!mKeyState[(int)key] || mPrevKeyState[(int)key])
					{
						stateDef.active = false;
						goto next_state;
					}
				}

				for (auto key : stateDef.keysReleased)
				{
					if (mKeyState[(int)key] || !mPrevKeyState[(int)key])
					{
						stateDef.active = false;
						goto next_state;
					}
				}

				for (auto key : stateDef.keysDown)
				{
					if (!mKeyState[(int)key])
					{
						stateDef.active = false;
						goto next_state;
					}
				}

				// Buttons
				for (auto button : stateDef.buttonsPressed)
				{
					if (!mButtonState[(int)button] || mPrevButtonState[(int)button])
					{
						stateDef.active = false;
						goto next_state;
					}
				}

				for (auto button : stateDef.buttonsReleased)
				{
					if (mButtonState[(int)button] || !mButtonState[(int)button])
					{
						stateDef.active = false;
						goto next_state;
					}
				}

				for (auto button : stateDef.buttonsDown)
				{
					if (!mButtonState[(int)button])
					{
						stateDef.active = false;
						goto next_state;
					}
				}

				// Key modifiers
				if ((mKeyModifiers & stateDef.keyModifiers) != stateDef.keyModifiers)
				{
					stateDef.active = false;
					goto next_state;
				}

			next_state:;
				if (stateDef.active)
				{
					activeStates.push_back(name);
				}
			}

			// Set up for next process
			for (int i = 0; i < (int)Key::NUMKEYS; ++i)
			{
				mPrevKeyState[i] = mKeyState[i];
			}

			for (int i = 0; i < (int)MouseButton::NUMBUTTONS; ++i)
			{
				mPrevButtonState[i] = mButtonState[i] = 0;
			}

			// Consume relative mouse motion if requested
			if (mouseDeltaX)
			{
				*mouseDeltaX = mMouseDeltaX;
				mMouseDeltaX = 0.0f;
			}

			if (mouseDeltaY)
			{
				*mouseDeltaY = mMouseDeltaY;
				mMouseDeltaY = 0.0f;
			}

			mMouseWheelUp = 0;
			mMouseWheelDown = 0;
			mPrevKeyModifiers = mKeyModifiers;

			return activeStates;
		}

	} // application
} //WP_NAMESPACE
