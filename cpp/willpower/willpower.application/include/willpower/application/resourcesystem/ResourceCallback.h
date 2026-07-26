#pragma once

#include <functional>

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/Resource.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			enum class ResourceState
			{
				Unknown,
				Instantiated,
				Destroying,
				Creating,
				Created,
				Loading,
				Loaded,
				Unloading,
				Releasing,
				Released
			};

			enum class ResourceLocationState
			{
				Unknown,
				Unscanned,
				Scanned
			};

			typedef std::function<void(ResourcePtr, ResourceState, bool)> ResourceCallback;

			typedef std::function<void(std::string const&, ResourceLocationState)> ResourceLocationCallback;

		} // resourcesystem
	} // application
} // WP_NAMESPACE

