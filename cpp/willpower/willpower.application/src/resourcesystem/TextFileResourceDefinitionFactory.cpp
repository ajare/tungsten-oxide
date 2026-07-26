#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/TextFileResourceDefinitionFactory.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			using namespace std;

			TextFileResourceDefinitionFactory::TextFileResourceDefinitionFactory(string const& factoryType)
				: ResourceDefinitionFactory("TextFile", factoryType)
			{
			}

		} // resourcesystem
	} // application
} // WP_NAMESPACE