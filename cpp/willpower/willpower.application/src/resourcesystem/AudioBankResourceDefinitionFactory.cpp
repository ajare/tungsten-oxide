#include "willpower/application/resourcesystem/AudioBankResourceDefinitionFactory.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"
#include "willpower/common/Exceptions.h"


namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			using namespace std;
			using namespace wp;

			AudioBankResourceDefinitionFactory::AudioBankResourceDefinitionFactory(string const& factoryType)
				: ResourceDefinitionFactory("AudioBank", factoryType)
			{
			}

		} // resourcesystem
	} // application
} // WP_NAMESPACE