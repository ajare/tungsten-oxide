#pragma once

#include "willpower/application/Platform.h"

namespace WP_NAMESPACE
{
	namespace application
	{

		/**	\brief Key input event
		*/
		enum class KeyEvent
		{
			Pressed,			/**< Key was pressed */
			Released			/**< Key was released */
		};

		/**	\brief Modifier keys which were active when the key was pressed
		*/
		enum KeyModifiers
		{
			None		= 0,
			LeftShift	= 1,
			RightShift	= 2,
			Shift		= 3,
			LeftCtrl	= 4,
			RightCtrl	= 8,
			Ctrl		= 12,
			LeftAlt		= 16,
			RightAlt	= 32,
			Alt			= 48,
			NumLock		= 64,
			CapsLock	= 128
		};

		/**	\brief Enumerated keys
		*/
		enum class Key
		{
			Escape = 0,
			_1,
			_2,
			_3,
			_4,
			_5,
			_6,
			_7,
			_8,
			_9,
			_0,
			Minus,
			Equals,
			Backspace,
			Tab,
			A,
			B,
			C,
			D,
			E,
			F,
			G,
			H,
			I,
			J,
			K,
			L,
			M,
			N,
			O,
			P,
			Q,
			R,
			S,
			T,
			U,
			V,
			W,
			X,
			Y,
			Z,
			LeftBracket,
			RightBracket,
			Enter,
			LeftControl,
			RightControl,
			Semicolon,
			Apostrophe,
			Tilde,
			LeftShift,
			RightShift,
			Backslash,
			Comma,
			Period,
			Slash,
			Hash,
			LeftAlt,
			RightAlt,
			Space,
			CapsLock,
			NumLock,
			ScrollLock,
			F1,
			F2,
			F3,
			F4,
			F5,
			F6,
			F7,
			F8,
			F9,
			F10,
			F11,
			F12,
			NumpadMultiply,
			NumpadMinus,
			NumpadPlus,
			NumpadDivide,
			Numpad_0,
			Numpad_1,
			Numpad_2,
			Numpad_3,
			Numpad_4,
			Numpad_5,
			Numpad_6,
			Numpad_7,
			Numpad_8,
			Numpad_9,
			NumpadPeriod,
			NumpadEnter,
			PrintScreen,
			Pause,
			Home,
			End,
			UpArrow,
			DownArrow,
			LeftArrow,
			RightArrow,
			PageUp,
			PageDown,
			Insert,
			Delete,
			NUMKEYS
		};

	} // application
} // WP_NAMESPACE
