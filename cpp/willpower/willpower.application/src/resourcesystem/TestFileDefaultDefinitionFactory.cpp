#include "willpower/common/StringUtils.h"

#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/TextFileDefaultDefinitionFactory.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			using namespace std;

			TextFileDefaultDefinitionFactory::TextFileDefaultDefinitionFactory()
				: TextFileResourceDefinitionFactory("")
			{
			}

			void TextFileDefaultDefinitionFactory::create(Resource* resource, ResourceManager* resourceMgr, XmlNode* node)
			{
				WP_UNUSED(resource);
				WP_UNUSED(resourceMgr);
				WP_UNUSED(node);
			}

		} // resourcesystem
	} // application
} // WP_NAMESPACE