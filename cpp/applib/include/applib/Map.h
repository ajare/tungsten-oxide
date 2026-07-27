#pragma once

#include <string>

#include "willpower/application/resourcesystem/Resource.h"
#include "willpower/application/resourcesystem/ResourceFactory.h"
#include "willpower/application/resourcesystem/ResourceDefinitionFactory.h"

#include "Platform.h"

namespace applib
{

	class APPLIB_API Map : public wp::application::resourcesystem::Resource
	{
		friend class MapResourceDefinitionFactory;

	private:

		void create(wp::application::resourcesystem::DataStreamPtr dataPtr, wp::application::resourcesystem::ResourceManager* resourceMgr) override;

		void destroy() override;

	public:

		Map(std::string const& name,
			std::string const& namesp,
			std::string const& source,
			std::map<std::string, std::string> const& tags,
			wp::application::resourcesystem::ResourceLocation* location,
			float accelGridSize);

		~Map();
	};

} // applib