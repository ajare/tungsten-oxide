#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/XmlFileResourceDefinitionFactory.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			using namespace std;

			XmlFileResourceDefinitionFactory::XmlFileResourceDefinitionFactory(string const& factoryType)
				: ResourceDefinitionFactory("XmlFile", factoryType)
			{
			}

		} // resourcesystem
	} // application
} // WP_NAMESPACE