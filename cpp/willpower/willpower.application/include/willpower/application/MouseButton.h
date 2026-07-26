#pragma once

#include "willpower/application/Platform.h"

namespace WP_NAMESPACE
{
	namespace application
	{

		/**	\brief Key input event
		*/
		enum class MouseButtonEvent
		{
			Pressed,				/**< MouseButton was pressed */
			Released				/**< MouseButton was released */
		};

		/**	\brief Enumerated mouse buttons
		*/
		enum class MouseButton
		{
			Left = 1,
			Right,
			Middle,
			WheelUp,
			WheelDown,
			Button4,
			Button5,
			Button6,
			Unused, // Because mouse definitions start at 1, need this to make NUMBUTTONS correct
			NUMBUTTONS
		};

	} // application
} // WP_NAMESPACE

