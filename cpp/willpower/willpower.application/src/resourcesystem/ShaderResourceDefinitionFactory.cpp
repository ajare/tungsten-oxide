#include "willpower/common/StringUtils.h"

#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/ShaderResourceDefinitionFactory.h"
#include "willpower/application/resourcesystem/ShaderResource.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			using namespace std;

			ShaderResourceDefinitionFactory::ShaderResourceDefinitionFactory(string const& factoryType)
				: ResourceDefinitionFactory("Shader", factoryType)
			{
			}

		} // resourcesystem
	} // application
} // WP_NAMESPACE