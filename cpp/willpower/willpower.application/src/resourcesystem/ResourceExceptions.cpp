include "willpower/application/resourcesystem/ResourceExceptions.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{

			using namespace std;

			explicit ResourceSystemException::ResourceSystemException(string const& message)
				: Exception(message)
			{
			}

			ResourceException::ResourceException(Resource const* resource, string const& message)
				: ResourceSystemException(message)
				, mResource(resource)
			{
			}

			Resource const* ResourceException::getResource()
			{
				return mResource;
			}

		} // resourcesystem
	} // application
} // WP_NAMESPACE

