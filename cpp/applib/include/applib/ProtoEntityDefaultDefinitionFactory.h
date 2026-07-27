#pragma once

#include "Platform.h"
#include "ProtoEntityResourceDefinitionFactory.h"

namespace applib
{
	class APPLIB_API ProtoEntityDefaultDefinitionFactory : public ProtoEntityResourceDefinitionFactory
	{
		void createProtoEntity(ProtoEntity* entity, wp::application::resourcesystem::ResourceManager* resourceMgr, wp::XmlNode* node) override;

	public:

		ProtoEntityDefaultDefinitionFactory();

	};

} // applib

