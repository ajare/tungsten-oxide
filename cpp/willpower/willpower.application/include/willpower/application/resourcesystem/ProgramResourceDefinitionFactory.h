#pragma once

#include <mpp/mesh/MeshSpecification.h>

#include "willpower/common/XmlReader.h"

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/Resource.h"
#include "willpower/application/resourcesystem/ResourceFactory.h"
#include "willpower/application/resourcesystem/ProgramResource.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{

			class WP_APPLICATION_API ProgramResourceDefinitionFactory : public ResourceDefinitionFactory
			{
				std::map<std::string, mpp::mesh::VertexBufferStorageType> mMeshSpecificationStorage;
				std::map<std::string, mpp::mesh::Primitive::Type>  mMeshSpecificationPrimitive;
				std::map<std::string, mpp::mesh::Vertex::Component> mComponentTypes;
				std::map<std::string, mpp::mesh::Vertex::DataType> mDataTypes;

			private:

				void parseMeshSpecificationPrimitiveType(ProgramResource* resource, mpp::mesh::MeshSpecification* meshSpec, XmlNode* node);

				void parseMeshSpecificationIndexed(ProgramResource* resource, mpp::mesh::MeshSpecification* meshSpec, XmlNode* node);

				void parseMeshSpecificationStorage(ProgramResource* resource, mpp::mesh::MeshSpecification* meshSpec, XmlNode* node);

				void parseMeshSpecificationBuffer(ProgramResource* resource, mpp::mesh::VertexBufferAttributeLayout* layout, XmlNode* node);

			protected:

				void parseAttribs(ProgramResource* resource, XmlNode* node);

				void parseMeshSpecification(ProgramResource* resource, XmlNode* node);

			public:

				explicit ProgramResourceDefinitionFactory(std::string const& factoryType);
			};

		} // resourcesystem
	} // application
} // WP_NAMESPACE

