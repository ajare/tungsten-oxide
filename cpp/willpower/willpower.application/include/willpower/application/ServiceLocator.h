#pragma once


#include "willpower/application/Platform.h"
#include "willpower/application/ApplicationSettings.h"

namespace WP_NAMESPACE
{
	namespace application
	{

		/**	\class SeriveLocator
		*   \brief Provides global single-point access to various systems.
		*/
		class WP_APPLICATION_API ServiceLocator
		{
			static ApplicationSettings* mswApplicationSettings;

		public:

			/**	\brief Set the ApplicationSettings instance to be used.
			*
			*	\param applicationSettings instance to use.
			*/
			static void provideApplicatonSettings(ApplicationSettings* applicationSettings);

			/**	\brief Return global ApplicationSettings instance.
			*
			*   \return instance in use.
			*/
			static ApplicationSettings* requestApplicationSettings();
		};

	} // application
} // WP_NAMESPACE
