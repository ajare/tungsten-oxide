#include <format>

#include <mpp/mesh/VertexTypeSpecification.h>

#include "WorldBatch.h"
#include "GameException.h"


using namespace std;

WorldBatch::WorldBatch(string const& name, mpp::ResourcePtr textureOrMaterial, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr, bw::core::World const* world)
	: TriangleBatch(name,
		{
			mpp::TriangleBatchOptions::Dimension::P3D,
			true,
			mpp::mesh::DataTypeFloat::vertexDataType(),
			{ mpp::mesh::DataTypeFloat::vertexDataType(), false },
			{ mpp::mesh::DataTypeUnsignedByte::vertexDataType(), false },
			false,
			true
		},
		16,
		textureOrMaterial,
		0,
		renderSystem,
		resourceMgr),
	mWorld(world)
{
}

void WorldBatch::processMaterialDefinition(uint32_t index, bw::core::MaterialDefinition const& def, shared_ptr<mpp::ProgrammaticModelStream> modelStream)
{
	auto hashValue = def.data.hash(index);

	if (mMaterialHashToMesh.find(hashValue) == mMaterialHashToMesh.end())
	{
		auto const& spec = getSpecification();

		auto meshIndex = modelStream->createMesh(formatMeshName(hashValue), spec, getMaterial()->getName(), getIndexWidth(), getPointSize());
		auto numVertices = getVertexCount(mInitialCapacity);

		if (numVertices > 0)
		{
			modelStream->addVertexData(meshIndex, mpp::mesh::VertexData(spec, numVertices));
		}

		if (spec.verticesIndexed())
		{
			addIndexedPrimitives(modelStream, (int)meshIndex);
		}

		mMaterialHashToMesh[hashValue] = (uint32_t)meshIndex;
	}
}

shared_ptr<mpp::ModelStream> WorldBatch::createModelStream()
{
	auto modelStream = make_shared<mpp::ProgrammaticModelStream>(mResourceMgr);
	modelStream->setCalculateBounds(false);

	// Create meshes for each material
	auto numPrimitives = mWorld->getNumPrimitives();

	for (uint32_t i = 0; i < numPrimitives; ++i)
	{
		auto primitive = mWorld->getPrimitive(i);
		auto const& properties = primitive->getProperties();

		processMaterialDefinition(properties.floorMaterialIndex, properties.floorMaterialDef, modelStream);
		processMaterialDefinition(properties.ceilingMaterialIndex, properties.ceilingMaterialDef, modelStream);
		processMaterialDefinition(properties.wallMaterialIndex, properties.wallMaterialDef, modelStream);
	}

	return modelStream;
}

uint32_t WorldBatch::getMeshIndexForMaterialHash(uint64_t hashValue) const
{
	auto it = mMaterialHashToMesh.find(hashValue);

	if (it == mMaterialHashToMesh.end())
	{
		throw GameException("Could not find a mesh index for material hash.");
	}
	else
	{
		return it->second;
	}
}

string WorldBatch::formatMeshName(uint64_t hashValue) const
{
	return format("WorldMaterial-{}_Batch_Mesh", hashValue);
}

void WorldBatch::finishUpdate(uint32_t meshIndex, uint32_t numTriangles, size_t numVertices, bool updateFixedBuffers)
{
	mMeshes[meshIndex].curCount = numTriangles;
	
	auto mesh = static_pointer_cast<mpp::Model>(getModel())->getMesh(meshIndex);

	if (numTriangles > 0)
	{
		if (mesh->isIndexed())
		{
			mesh->mapIndexData(numTriangles);
		}

		for (size_t i = 0; i < mesh->getNumVertexBuffers(); ++i)
		{
			auto vertexBuffer = mesh->getVertexBuffer((int)i);

			if (updateFixedBuffers || !vertexBuffer->isStatic())
			{
				vertexBuffer->mapBufferData(numVertices);
			}
		}
	}

	mesh->setNumPrimitives(numTriangles);
}