#pragma once

#include <mpp/mesh/MeshSpecification.h>

#include "willpower/common/DataNode.h"

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/Resource.h"
#include "willpower/application/resourcesystem/ResourceFactory.h"
#include "willpower/application/resourcesystem/ProgramResource.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {

class WP_APPLICATION_API ProgramResourceDefinitionFactory : public ResourceDefinitionFactory {
  std::map<std::string, mpp::mesh::VertexBufferStorageType> mMeshSpecificationStorage;
  std::map<std::string, mpp::mesh::Primitive::Type> mMeshSpecificationPrimitive;
  std::map<std::string, mpp::mesh::Vertex::Component> mComponentTypes;
  std::map<std::string, mpp::mesh::Vertex::DataType> mDataTypes;

private:
  void parseMeshSpecificationPrimitiveType(ProgramResource* resource, mpp::mesh::MeshSpecification* meshSpec, wp::DataNode* node);

  void parseMeshSpecificationIndexed(ProgramResource* resource, mpp::mesh::MeshSpecification* meshSpec, wp::DataNode* node);

  void parseMeshSpecificationStorage(ProgramResource* resource, mpp::mesh::MeshSpecification* meshSpec, wp::DataNode* node);

  void parseMeshSpecificationBuffer(ProgramResource* resource, mpp::mesh::VertexBufferAttributeLayout* layout, wp::DataNode* node);

protected:
  void parseAttribs(ProgramResource* resource, wp::DataNode* node);

  void parseMeshSpecification(ProgramResource* resource, wp::DataNode* node);

public:
  explicit ProgramResourceDefinitionFactory(std::string const& factoryType);
};

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE
