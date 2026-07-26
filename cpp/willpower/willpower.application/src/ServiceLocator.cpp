#include "willpower/application/ServiceLocator.h"
#include "willpower/application/ApplicationSettings.h"

namespace WP_NAMESPACE
{
	namespace application
	{

		ApplicationSettings* ServiceLocator::mswApplicationSettings = nullptr;

		void ServiceLocator::provideApplicatonSettings(ApplicationSettings* applicationSettings)
		{
			mswApplicationSettings = applicationSettings;
		}

		ApplicationSettings* ServiceLocator::requestApplicationSettings()
		{
			return mswApplicationSettings;
		}


	} // application
} //WP_NAMESPACE
