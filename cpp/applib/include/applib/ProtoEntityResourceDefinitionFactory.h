#pragma once

#include <string>
#include <map>

#include "willpower/application/resourcesystem/Resource.h"
#include "willpower/application/resourcesystem/ResourceFactory.h"
#include "willpower/application/resourcesystem/ResourceDefinitionFactory.h"

#include "willpower/common/XmlReader.h"

#include "Platform.h"
#include "ProtoEntity.h"
#include "Entity.h"
#include "EntityHandler.h"

namespace applib
{

	class APPLIB_API ProtoEntityResourceDefinitionFactory : public wp::application::resourcesystem::ResourceDefinitionFactory
	{
	protected:

		std::shared_ptr<EntityHandler> getEntityHandler(ProtoEntity* resource);

		void loadExtraDefinitions(ProtoEntity* resource, wp::XmlNode* node, entt::entity protoId);

		virtual uint32_t getAnimationIdFromName(std::string const& actor, std::string const& anim);

		virtual void createProtoEntity(ProtoEntity* entity, wp::application::resourcesystem::ResourceManager* resourceMgr, wp::XmlNode* node) = 0;

	public:

		explicit ProtoEntityResourceDefinitionFactory(std::string const& factoryType);

		void create(wp::application::resourcesystem::Resource* resource, wp::application::resourcesystem::ResourceManager* resourceMgr, wp::XmlNode* node) override;

	};

} // applib