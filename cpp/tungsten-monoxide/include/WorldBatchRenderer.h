#pragma once

#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>
#include <mpp/BatchRenderer.h>

#include <mpp/helper/TriangleBatchRenderer.h>

#include <core/World.h>

#include "WorldBatch.h"


class WorldBatchRenderer : public mpp::BatchRenderer
{
	mpp::RenderSystem* mRenderSystem{ nullptr };

	mpp::ResourceManager* mResourceMgr{ nullptr };

	WorldBatch* mBatch{ nullptr };

	std::shared_ptr<mpp::helper::TriangleBatch3DBufferDataProvider<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeUnsignedByte>> mDataProvider{ nullptr };

public:

	WorldBatchRenderer(std::string const& name,
		std::shared_ptr<mpp::helper::TriangleBatch3DBufferDataProvider<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeUnsignedByte>> dataProvider,
		mpp::ResourcePtr textureOrMaterial,
		mpp::RenderSystem* renderSystem,
		mpp::ResourceManager* resourceMgr,
		bw::core::World const* world);

	virtual ~WorldBatchRenderer();

	mpp::ResourcePtr getModel();

	WorldBatch const* getWorldBatch() const;

	void create() override;

	size_t update() override;

	void render() override;

};


