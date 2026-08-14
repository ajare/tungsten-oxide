#include <mpp/ProgrammaticProgramStream.h>
#include <mpp/program/Parser.h>

#include "willpower/common/StringUtils.h"
#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/ResourceExceptions.h"
#include "willpower/application/resourcesystem/ProgramResourceDefinitionFactory.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {
using namespace std;

ProgramResourceDefinitionFactory::ProgramResourceDefinitionFactory(string const& factoryType)
    : ResourceDefinitionFactory("Program", factoryType) {
  // Primitives
  mMeshSpecificationPrimitive["POINTS"] = mpp::mesh::Primitive::Type::Points;
  mMeshSpecificationPrimitive["LINES"] = mpp::mesh::Primitive::Type::Lines;
  mMeshSpecificationPrimitive["TRIANGLES"] = mpp::mesh::Primitive::Type::Triangles;

  // Storage
  mMeshSpecificationStorage["STATIC"] = mpp::mesh::VertexBufferStorageType::Static;
  mMeshSpecificationStorage["DYNAMIC"] = mpp::mesh::VertexBufferStorageType::Dynamic;

  // Components
  mComponentTypes["POSITION2"] = mpp::mesh::Vertex::Component::Position2;
  mComponentTypes["POSITION3"] = mpp::mesh::Vertex::Component::Position3;
  mComponentTypes["POSITION4"] = mpp::mesh::Vertex::Component::Position4;
  mComponentTypes["NORMAL3"] = mpp::mesh::Vertex::Component::Normal3;
  mComponentTypes["NORMAL4"] = mpp::mesh::Vertex::Component::Normal4;
  mComponentTypes["TEXCOORD2"] = mpp::mesh::Vertex::Component::TexCoord2;
  mComponentTypes["TEXCOORD3"] = mpp::mesh::Vertex::Component::TexCoord3;
  mComponentTypes["TEXCOORD4"] = mpp::mesh::Vertex::Component::TexCoord4;
  mComponentTypes["COLOUR1"] = mpp::mesh::Vertex::Component::Colour1;
  mComponentTypes["COLOUR3"] = mpp::mesh::Vertex::Component::Colour3;
  mComponentTypes["COLOUR4"] = mpp::mesh::Vertex::Component::Colour4;
  mComponentTypes["USER1"] = mpp::mesh::Vertex::Component::UserDefined1;
  mComponentTypes["USER2"] = mpp::mesh::Vertex::Component::UserDefined2;
  mComponentTypes["USER3"] = mpp::mesh::Vertex::Component::UserDefined3;
  mComponentTypes["USER4"] = mpp::mesh::Vertex::Component::UserDefined4;

  // Datatypes
  mDataTypes["FLOAT16"] = mpp::mesh::Vertex::DataType::HalfFloat;
  mDataTypes["FLOAT32"] = mpp::mesh::Vertex::DataType::Float;
  mDataTypes["INT8"] = mpp::mesh::Vertex::DataType::Byte;
  mDataTypes["INT16"] = mpp::mesh::Vertex::DataType::Short;
  mDataTypes["INT32"] = mpp::mesh::Vertex::DataType::Int;
  mDataTypes["UINT8"] = mpp::mesh::Vertex::DataType::UnsignedByte;
  mDataTypes["UINT16"] = mpp::mesh::Vertex::DataType::UnsignedShort;
  mDataTypes["UINT32"] = mpp::mesh::Vertex::DataType::UnsignedInt;
}

void ProgramResourceDefinitionFactory::parseAttribs(ProgramResource* resource, DataNode* node) {
  auto texturesNode = node->getOptionalChild("textures");
  resource->mNumTextures = texturesNode ? StringUtils::parseUInt(texturesNode->getValue()) : 0;

  auto diffuseNode = node->getOptionalChild("diffuse");
  resource->mUseDiffuse = diffuseNode ? StringUtils::parseBool(diffuseNode->getValue()) : false;

  auto coloursNode = node->getOptionalChild("colours");
  resource->mUseColours = coloursNode ? StringUtils::parseBool(coloursNode->getValue()) : false;

  auto atlasNode = node->getOptionalChild("atlas");
  resource->mUseAtlas = atlasNode ? StringUtils::parseBool(atlasNode->getValue()) : false;

  auto rotationNode = node->getOptionalChild("rotation");
  resource->mUseRotation = rotationNode ? StringUtils::parseBool(rotationNode->getValue()) : false;
}

void ProgramResourceDefinitionFactory::parseMeshSpecificationPrimitiveType(ProgramResource* resource, mpp::mesh::MeshSpecification* meshSpec, DataNode* node) {
  auto value = node->getValue();
  StringUtils::toUpper(value);

  auto it = mMeshSpecificationPrimitive.find(value);

  if (it != mMeshSpecificationPrimitive.end()) {
    meshSpec->setPrimitiveType(it->second);
  } else {
    throw ResourceException(resource, "could not set mesh specification primitive type to  '" + node->getValue() + "'.");
  }
}

void ProgramResourceDefinitionFactory::parseMeshSpecificationIndexed(ProgramResource* resource, mpp::mesh::MeshSpecification* meshSpec, DataNode* node) {
  WP_UNUSED(resource);

  auto value = node->getValue();
  meshSpec->setIndexedVertices(StringUtils::parseBool(value));
}

void ProgramResourceDefinitionFactory::parseMeshSpecificationStorage(ProgramResource* resource, mpp::mesh::MeshSpecification* meshSpec, DataNode* node) {
  auto value = node->getValue();
  StringUtils::toUpper(value);

  auto it = mMeshSpecificationStorage.find(value);

  if (it != mMeshSpecificationStorage.end()) {
    meshSpec->setStorageType(it->second);
  } else {
    throw ResourceException(resource, "could not set mesh specification storage type to  '" + node->getValue() + "'.");
  }
}

void ProgramResourceDefinitionFactory::parseMeshSpecificationBuffer(ProgramResource* resource, mpp::mesh::VertexBufferAttributeLayout* layout, DataNode* node) {
  auto channelsNode = node->getChild("Channels");
  auto channelNode = channelsNode->getChild("Channel");

  do {
    mpp::mesh::Vertex::Component component{mpp::mesh::Vertex::Component::Unused};
    mpp::mesh::Vertex::DataType datatype{mpp::mesh::Vertex::DataType::None};
    bool normalised{false};

    // Data
    auto data = channelNode->getChild("data")->getValue();
    StringUtils::toUpper(data);

    auto itd = mComponentTypes.find(data);

    if (itd != mComponentTypes.end()) {
      component = itd->second;
    } else {
      throw ResourceException(resource, "could not set mesh specification channel component to  '" + channelNode->getChild("data")->getValue() + "'.");
    }

    // Type
    auto type = channelNode->getChild("type")->getValue();
    StringUtils::toUpper(type);

    auto itt = mDataTypes.find(type);

    if (itt != mDataTypes.end()) {
      datatype = itt->second;
    } else {
      throw ResourceException(resource, "could not set mesh specification channel datatype to  '" + channelNode->getChild("type")->getValue() + "'.");
    }

    // Normalised
    auto normalisedNode = channelNode->getOptionalChild("normalised");
    if (normalisedNode) {
      normalised = StringUtils::parseBool(normalisedNode->getValue());
    }

    // Add to layout
    layout->createAttribute(component, datatype, normalised);
  } while (channelNode->next());
}

void ProgramResourceDefinitionFactory::parseMeshSpecification(ProgramResource* resource, DataNode* node) {
  parseMeshSpecificationPrimitiveType(resource, &resource->mSpecification, node->getChild("primitive"));
  parseMeshSpecificationIndexed(resource, &resource->mSpecification, node->getChild("indexed"));
  parseMeshSpecificationStorage(resource, &resource->mSpecification, node->getChild("storage"));

  auto buffersNode = node->getChild("Buffers");
  auto bufferNode = buffersNode->getChild("Buffer");

  do {
    auto buffer = resource->mSpecification.createVertexBufferAttributeLayout(resource->mSpecification.getStorageType() == mpp::mesh::VertexBufferStorageType::Static);
    parseMeshSpecificationBuffer(resource, buffer, bufferNode);
  } while (bufferNode->next());
}

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE