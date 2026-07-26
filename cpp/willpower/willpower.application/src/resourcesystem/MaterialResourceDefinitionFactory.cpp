#include "willpower/common/StringUtils.h"
#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/MaterialResourceDefinitionFactory.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			using namespace std;

			MaterialResourceDefinitionFactory::MaterialResourceDefinitionFactory(string const& factoryType)
				: ResourceDefinitionFactory("Material", factoryType)
			{
			}

			void MaterialResourceDefinitionFactory::addResourceTextureDefinition(MaterialResource* resource, string const& sampler, string const& resName)
			{
				resource->mTextureDefinitions.push_back({ sampler, true, resName });
			}

			void MaterialResourceDefinitionFactory::addDefaultTextureDefinition(MaterialResource* resource, string const& sampler)
			{
				resource->mTextureDefinitions.push_back({ sampler, false, "" });
			}


		} // resourcesystem
	} // application
} // WP_NAMESPACE