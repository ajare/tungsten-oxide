#pragma once

#include <functional>
#include <vector>

#include <mpp/TriangleBatch.h>
#include <mpp/ProgrammaticModelStream.h>

#include <core/World.h>
#include <core/MaterialDefinition.h>


class WorldBatch : public mpp::TriangleBatch
{
	bw::core::World const* mWorld;

	std::map<uint64_t, uint32_t> mMaterialHashToMesh;

private:

	void processMaterialDefinition(uint32_t index, bw::core::MaterialDefinition const& def, std::shared_ptr<mpp::ProgrammaticModelStream> modelStream);

public:

	WorldBatch(std::string const& name, mpp::ResourcePtr textureOrMaterial, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr, bw::core::World const* world);

	std::shared_ptr<mpp::ModelStream> createModelStream() override;

	uint32_t getMeshIndexForMaterialHash(uint64_t hashValue) const;

	std::string formatMeshName(uint64_t hashValue) const;

	void finishUpdate(uint32_t meshIndex, uint32_t numTriangles, size_t numVertices, bool updateFixedBuffers);
};

