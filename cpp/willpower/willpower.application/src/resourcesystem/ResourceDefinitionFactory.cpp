#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/ResourceExceptions.h"
#include "willpower/application/resourcesystem/ResourceDefinitionFactory.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			using namespace std;

			ResourceDefinitionFactory::ResourceDefinitionFactory(string const& resType, string const& factoryType)
				: mResType(resType)
				, mFactoryType(factoryType)
			{
			}

			string const& ResourceDefinitionFactory::getResourceType() const
			{
				return mResType;
			}

			string const& ResourceDefinitionFactory::getFactoryType() const
			{
				return mFactoryType;
			}

		} // resourcesystem
	} // application
} // WP_NAMESPACE