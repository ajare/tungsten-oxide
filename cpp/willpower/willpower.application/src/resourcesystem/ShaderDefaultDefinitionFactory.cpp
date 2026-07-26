#include "willpower/common/StringUtils.h"

#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/ShaderResource.h"
#include "willpower/application/resourcesystem/ShaderDefaultDefinitionFactory.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			using namespace std;

			ShaderDefaultDefinitionFactory::ShaderDefaultDefinitionFactory()
				: ShaderResourceDefinitionFactory("")
			{
			}

			void ShaderDefaultDefinitionFactory::create(Resource* resource, ResourceManager* resourceMgr, XmlNode* node)
			{
				WP_UNUSED(resource);
				WP_UNUSED(resourceMgr);
				WP_UNUSED(node);
			}

		} // resourcesystem
	} // application
} // WP_NAMESPACE