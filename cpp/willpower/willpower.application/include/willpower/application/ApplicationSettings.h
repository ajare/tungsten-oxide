#pragma once

#include "willpower/application/Platform.h"


namespace WP_NAMESPACE
{
	namespace application
	{

		/**	\class ApplicationSettings
		*   \brief Holds settings which need to be globally accessible from the Application.
		*/
		class WP_APPLICATION_API ApplicationSettings
		{

			// Helper function for copy constructor / assignment operator.
			void copyFrom(ApplicationSettings const& other);

		public:

			/**	\brief Constructor.
			*/
			ApplicationSettings() = default;

			/**	\brief Destructor.
			*/
			~ApplicationSettings() = default;

			/**	\brief Copy constructor.
			*
			*	\param other Application instance to copy from.
			*/
			ApplicationSettings(ApplicationSettings const& other);

			/**	\brief Assignment operator.
			*
			*	\param other Application instance to assign from.
			*/
			ApplicationSettings& operator=(ApplicationSettings const& other);

		public:

			/**	\brief Width of the render window, in pixels.
			*/
			int VideoWidth;
				
			/**	\brief Height of the render window, in pixels.
			*/
			int VideoHeight;

			/**	\brief Specifies whether the render window is fullscreen or not.
			*/
			bool Fullscreen;
		};

	} // application
} // WP_NAMESPACE
