#include "willpower/application/InputHelper.h"

using namespace std;

namespace WP_NAMESPACE
{
	namespace application
	{
		using namespace WP_NAMESPACE;

		InputHelper::Detail InputHelper::msDetail;

		InputHelper::Detail::Detail()
		{
			// Key names
			mKeyNames[wp::application::Key::Escape] = "Escape";

			mKeyNames[wp::application::Key::_1] = "1";
			mKeyNames[wp::application::Key::_2] = "2";
			mKeyNames[wp::application::Key::_3] = "3";
			mKeyNames[wp::application::Key::_4] = "4";
			mKeyNames[wp::application::Key::_5] = "5";
			mKeyNames[wp::application::Key::_6] = "6";
			mKeyNames[wp::application::Key::_7] = "7";
			mKeyNames[wp::application::Key::_8] = "8";
			mKeyNames[wp::application::Key::_9] = "9";
			mKeyNames[wp::application::Key::_0] = "0";
			mKeyNames[wp::application::Key::Minus] = "-";
			mKeyNames[wp::application::Key::Equals] = "=";

			mKeyNames[wp::application::Key::Backspace] = "Backspace";
			mKeyNames[wp::application::Key::Tab] = "Tab";

			mKeyNames[wp::application::Key::Q] = "Q";
			mKeyNames[wp::application::Key::W] = "W";
			mKeyNames[wp::application::Key::E] = "E";
			mKeyNames[wp::application::Key::R] = "R";
			mKeyNames[wp::application::Key::T] = "T";
			mKeyNames[wp::application::Key::Y] = "Y";
			mKeyNames[wp::application::Key::U] = "U";
			mKeyNames[wp::application::Key::I] = "I";
			mKeyNames[wp::application::Key::O] = "O";
			mKeyNames[wp::application::Key::P] = "P";
			mKeyNames[wp::application::Key::LeftBracket] = "[";
			mKeyNames[wp::application::Key::RightBracket] = "]";

			mKeyNames[wp::application::Key::Enter] = "Enter";
			mKeyNames[wp::application::Key::LeftControl] = "Left Control";

			mKeyNames[wp::application::Key::A] = "A";
			mKeyNames[wp::application::Key::S] = "S";
			mKeyNames[wp::application::Key::D] = "D";
			mKeyNames[wp::application::Key::F] = "F";
			mKeyNames[wp::application::Key::G] = "G";
			mKeyNames[wp::application::Key::H] = "H";
			mKeyNames[wp::application::Key::J] = "J";
			mKeyNames[wp::application::Key::K] = "K";
			mKeyNames[wp::application::Key::L] = "L";
			mKeyNames[wp::application::Key::Semicolon] = ";";
			mKeyNames[wp::application::Key::Apostrophe] = "'";
			mKeyNames[wp::application::Key::Tilde] = "Tilde";

			mKeyNames[wp::application::Key::LeftShift] = "Left Shift";
			mKeyNames[wp::application::Key::Backslash] = "\\";
			mKeyNames[wp::application::Key::Z] = "Z";
			mKeyNames[wp::application::Key::X] = "X";
			mKeyNames[wp::application::Key::C] = "C";
			mKeyNames[wp::application::Key::V] = "V";
			mKeyNames[wp::application::Key::B] = "B";
			mKeyNames[wp::application::Key::N] = "N";
			mKeyNames[wp::application::Key::M] = "M";
			mKeyNames[wp::application::Key::Comma] = ",";
			mKeyNames[wp::application::Key::Period] = ".";
			mKeyNames[wp::application::Key::Slash] = "/";
			mKeyNames[wp::application::Key::Hash] = "#";
			mKeyNames[wp::application::Key::RightShift] = "Right Shift";

			mKeyNames[wp::application::Key::NumpadMultiply] = "Numpad *";
			mKeyNames[wp::application::Key::LeftAlt] = "Left Alt";
			mKeyNames[wp::application::Key::Space] = "Space";
			mKeyNames[wp::application::Key::CapsLock] = "Caps Lock";

			mKeyNames[wp::application::Key::F1] = "F1";
			mKeyNames[wp::application::Key::F2] = "F2";
			mKeyNames[wp::application::Key::F3] = "F3";
			mKeyNames[wp::application::Key::F4] = "F4";
			mKeyNames[wp::application::Key::F5] = "F5";
			mKeyNames[wp::application::Key::F6] = "F6";
			mKeyNames[wp::application::Key::F7] = "F7";
			mKeyNames[wp::application::Key::F8] = "F8";
			mKeyNames[wp::application::Key::F9] = "F9";
			mKeyNames[wp::application::Key::F10] = "F10";

			mKeyNames[wp::application::Key::NumLock] = "Num Lock";
			mKeyNames[wp::application::Key::ScrollLock] = "Scroll Lock";

			mKeyNames[wp::application::Key::Numpad_7] = "Numpad 7";
			mKeyNames[wp::application::Key::Numpad_8] = "Numpad 8";
			mKeyNames[wp::application::Key::Numpad_9] = "Numpad 9";
			mKeyNames[wp::application::Key::NumpadMinus] = "Numpad -";
			mKeyNames[wp::application::Key::Numpad_4] = "Numpad 4";
			mKeyNames[wp::application::Key::Numpad_5] = "Numpad 5";
			mKeyNames[wp::application::Key::Numpad_6] = "Numpad 6";
			mKeyNames[wp::application::Key::NumpadPlus] = "Numpad +";
			mKeyNames[wp::application::Key::Numpad_1] = "Numpad 1";
			mKeyNames[wp::application::Key::Numpad_2] = "Numpad 2";
			mKeyNames[wp::application::Key::Numpad_3] = "Numpad 3";
			mKeyNames[wp::application::Key::Numpad_0] = "Numpad 0";
			mKeyNames[wp::application::Key::NumpadPeriod] = "Numpad .";

			mKeyNames[wp::application::Key::F11] = "F11";
			mKeyNames[wp::application::Key::F12] = "F12";

			mKeyNames[wp::application::Key::NumpadEnter] = "Numpad Enter";
			mKeyNames[wp::application::Key::RightControl] = "Right Control";
			mKeyNames[wp::application::Key::NumpadDivide] = "Numpad /";

			mKeyNames[wp::application::Key::PrintScreen] = "PrintScreen";
			mKeyNames[wp::application::Key::RightAlt] = "Right Alt";

			mKeyNames[wp::application::Key::Pause] = "Pause";
			mKeyNames[wp::application::Key::Home] = "Home";
			mKeyNames[wp::application::Key::UpArrow] = "Up Arrow";
			mKeyNames[wp::application::Key::PageUp] = "Page Up";
			mKeyNames[wp::application::Key::LeftArrow] = "Left Arrow";
			mKeyNames[wp::application::Key::RightArrow] = "Right Arrow";
			mKeyNames[wp::application::Key::End] = "End";
			mKeyNames[wp::application::Key::DownArrow] = "Down Arrow";
			mKeyNames[wp::application::Key::PageDown] = "Page Down";
			mKeyNames[wp::application::Key::Insert] = "Insert";
			mKeyNames[wp::application::Key::Delete] = "Delete";

			// Mouse button names
			mMouseButtonNames[wp::application::MouseButton::Left] = "Mouse Left Button";
			mMouseButtonNames[wp::application::MouseButton::Right] = "Mouse Right Button";
			mMouseButtonNames[wp::application::MouseButton::Middle] = "Mouse Middle Button";
			mMouseButtonNames[wp::application::MouseButton::WheelUp] = "Mouse Wheel Up";
			mMouseButtonNames[wp::application::MouseButton::WheelDown] = "Mouse Wheel Down";
			mMouseButtonNames[wp::application::MouseButton::Button4] = "Mouse Button 4";
			mMouseButtonNames[wp::application::MouseButton::Button5] = "Mouse Button 5";
			mMouseButtonNames[wp::application::MouseButton::Button6] = "Mouse Button 6";
		}

		string InputHelper::getKeyName(Key key)
		{
			return msDetail.mKeyNames[key];
		}

		string InputHelper::getMouseButtonName(MouseButton mouseButton)
		{
			return msDetail.mMouseButtonNames[mouseButton];
		}

	} // application
} //WP_NAMESPACE
