#pragma once

#include <string>
#include <map>

#include "willpower/application/Key.h"
#include "willpower/application/MouseButton.h"

namespace WP_NAMESPACE
{
	namespace application
	{

		/**	\class InputHelper
		*   \brief Translates input codes into friendly names.
		*/
		class InputHelper
		{
			struct Detail
			{
				std::map<wp::application::Key, std::string> mKeyNames;

				std::map<wp::application::MouseButton, std::string> mMouseButtonNames;

			public:

				Detail();
			};

		private:

			static Detail msDetail;

		public:

			/**	\brief Return friendly name of a given Key code.
			*
			*	\param key Key code.
			*   \return text description of Key code.
			*/
			static std::string getKeyName(Key key);

			/**	\brief Return friendly name of a given MouseButton code.
			*
			*	\param mouseButton MouseButton code.
			*   \return text description of MouseButton code.
			*/
			static std::string getMouseButtonName(MouseButton mouseButton);
		};

	} // application
} // WP_NAMESPACE

