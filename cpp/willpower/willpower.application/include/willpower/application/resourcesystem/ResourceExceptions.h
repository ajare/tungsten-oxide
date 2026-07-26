#pragma once

#include "willpower/common/Exceptions.h"

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/Resource.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{

			class ResourceSystemException : public Exception
			{
			public:

				explicit ResourceSystemException(std::string const& message)
					: Exception(message)
				{
				}
			};

			class ResourceException : public ResourceSystemException
			{
				Resource const* mwResource;

			public:

				ResourceException(Resource const* resource, std::string const& message)
					: ResourceSystemException(resource->getType() + " '" + resource->getQualifiedName() + "': " + message)
					, mwResource(resource)
				{
				}

				Resource const* getResource() const
				{
					return mwResource;
				}
			};

		} // resourcesystem
	} // application
} // WP_NAMESPACE

