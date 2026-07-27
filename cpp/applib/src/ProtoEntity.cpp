#include "willpower/application/resourcesystem/ResourceExceptions.h"

#include "ProtoEntity.h"

namespace applib
{

	using namespace std;
	using namespace wp;

	ProtoEntity::ProtoEntity(string const& name,
		string const& namesp,
		string const& source,
		map<string, string> const& tags,
		application::resourcesystem::ResourceLocation* location,
		shared_ptr<EntityHandler> entityHandler)
		: application::resourcesystem::Resource(name, namesp, "ProtoEntity", source, tags, location)
		, mEntityHandler(entityHandler)
	{
	}

	void ProtoEntity::loadExtraDefinitions(XmlNode* node, entt::entity protoId)
	{
		// To be implemented by child class.
	}

	void ProtoEntity::setComponentSystemId(entt::entity id)
	{
		mCompSysId = id;
	}

	entt::entity ProtoEntity::getComponentSystemId() const
	{
		return mCompSysId;
	}

} // applib