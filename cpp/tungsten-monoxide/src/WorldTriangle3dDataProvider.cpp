#include "WorldTriangle3dDataProvider.h"


WorldTriangle3dDataProvider::WorldTriangle3dDataProvider()
	: mVertexStride(sizeof(float) * 8 + sizeof(uint8_t) * 4)
{
}

WorldTriangle3dDataProvider::~WorldTriangle3dDataProvider()
{
	for (auto& meshData : mMeshData)
	{
		delete meshData.vertexData;
		delete meshData.indexData;
	}
}

void WorldTriangle3dDataProvider::getBounds(glm::vec3& bMin, glm::vec3& bMax)
{
	bMin.x = bMin.y = bMin.z = -1e10f;
	bMax.x = bMax.y = bMax.z = 1e10f;
}

void WorldTriangle3dDataProvider::clear()
{
	updateInternals(0, 0);
}

void WorldTriangle3dDataProvider::setMeshCount(uint32_t numMeshes)
{
	mMeshData.resize(numMeshes);
}

uint32_t WorldTriangle3dDataProvider::getNumMeshes() const
{
	return (uint32_t)mMeshData.size();
}

WorldTriangle3dDataProvider::MeshData const& WorldTriangle3dDataProvider::getMeshData(uint32_t index) const
{
	return mMeshData[index];
}

WorldTriangle3dDataProvider::DrawVert* WorldTriangle3dDataProvider::nextVertexPtr(uint32_t meshIndex)
{
	auto& meshData = mMeshData[meshIndex];

	meshData.numVertices++;

	return meshData._workVert++;
}

void WorldTriangle3dDataProvider::addTriangle(uint32_t meshIndex, uint16_t v0, uint16_t v1, uint16_t v2)
{
	auto& meshData = mMeshData[meshIndex];

	*meshData._workIndex++ = v0;
	*meshData._workIndex++ = v1;
	*meshData._workIndex++ = v2;
	meshData.numTriangles++;
}

uint32_t WorldTriangle3dDataProvider::getNumTriangles() const
{
	uint32_t numTriangles{ 0 };

	for (auto const& meshData : mMeshData)
	{
		numTriangles += meshData.numTriangles;
	}

	return numTriangles;
}

uint32_t WorldTriangle3dDataProvider::getNumVertices() const
{
	uint32_t numVertices{ 0 };

	for (auto const& meshData : mMeshData)
	{
		numVertices += meshData.numVertices;
	}

	return numVertices;
}

int8_t* WorldTriangle3dDataProvider::getVertexData(uint32_t meshIndex) const
{
	auto const& meshData = mMeshData[meshIndex];

	return meshData.vertexData;
}

uint32_t WorldTriangle3dDataProvider::getVertexDataSize(uint32_t meshIndex) const
{
	auto const& meshData = mMeshData[meshIndex];

	return meshData.numVertices * mVertexStride;
}

int8_t* WorldTriangle3dDataProvider::getIndexData(uint32_t meshIndex) const
{
	auto const& meshData = mMeshData[meshIndex];

	return (int8_t*)meshData.indexData;
}

uint32_t WorldTriangle3dDataProvider::getNumIndices(uint32_t meshIndex) const
{
	auto const& meshData = mMeshData[meshIndex];

	return meshData.numTriangles * 3;
}

uint32_t WorldTriangle3dDataProvider::getIndexWidth() const
{
	return 16;
}

mpp::Colour WorldTriangle3dDataProvider::diffuse()
{
	return mpp::Colour::White;
}

void WorldTriangle3dDataProvider::updateInternals(uint32_t numVertices, uint32_t numTriangles)
{
	for (auto& meshData : mMeshData)
	{
		auto newVertexDataSize = numVertices * mVertexStride;

		if (newVertexDataSize > meshData.vertexDataSize)
		{
			meshData.vertexDataSize = newVertexDataSize;

			delete[] meshData.vertexData;
			meshData.vertexData = new int8_t[meshData.vertexDataSize];
		}

		auto numIndices = numTriangles * 3;
		auto newIndexDataSize = (uint32_t)(numIndices * sizeof(uint16_t));

		if (newIndexDataSize > meshData.indexDataSize)
		{
			meshData.indexDataSize = newIndexDataSize;

			delete[] meshData.indexData;
			meshData.indexData = new uint16_t[numIndices];
		}

		meshData._workVert = (DrawVert*)meshData.vertexData;
		meshData._workIndex = meshData.indexData;

		meshData.numVertices = 0;
		meshData.numTriangles = 0;

		setNumPrimitives(meshData.numTriangles);
	}
}

