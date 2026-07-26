#include "willpower/application/ApplicationSettings.h"

using namespace std;

namespace WP_NAMESPACE
{
	namespace application
	{

		using namespace WP_NAMESPACE;

		ApplicationSettings::ApplicationSettings(ApplicationSettings const& other)
		{
			copyFrom(other);
		}

		ApplicationSettings& ApplicationSettings::operator=(ApplicationSettings const& other)
		{
			copyFrom(other);
			return *this;
		}

		void ApplicationSettings::copyFrom(ApplicationSettings const& other)
		{
			VideoWidth = other.VideoWidth;
			VideoHeight = other.VideoHeight;
			Fullscreen = other.Fullscreen;
		}

	} // application
} //WP_NAMESPACE
