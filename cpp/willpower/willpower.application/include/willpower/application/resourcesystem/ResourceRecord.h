#pragma once

#include <string>
#include <vector>
#include <map>


#include "willpower/application/Platform.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			
			class ResourceLocation;

			struct ResourceRecordBaseData
			{
				std::string name;
				bool nameFound{ false };

				std::string type;
				bool typeFound{ false };

				std::string location;
				bool locationFound{ false };

				std::map<std::string, std::string> tags;
			};

			struct DependentResourceRecord
			{
				std::string owner;
				std::string id;
				std::string ref;
			};

			struct ResourceRecord
			{
				ResourceRecord() = default;

				ResourceRecord(std::string const& nmsp, ResourceLocation* location, bool composite)
					: namesp(nmsp)
					, resourceLocation(location)
					, isComposite(composite)
				{
				}

			public:

				ResourceRecordBaseData baseData;
				ResourceLocation* resourceLocation;

				std::string namesp;
				bool isComposite;

				std::vector<std::pair<std::string, std::string>> definitions;

				std::vector<DependentResourceRecord> dependentResources;
			};

		} // resourcesystem
	} // application
} // WP_NAMESPACE

