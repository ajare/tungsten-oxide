#include "WorldBatchRenderer.h"
#include "WorldTriangle3dDataProvider.h"


using namespace std;

WorldBatchRenderer::WorldBatchRenderer(string const& name,
	shared_ptr<mpp::helper::TriangleBatch3DBufferDataProvider<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeUnsignedByte>> dataProvider,
	mpp::ResourcePtr textureOrMaterial,
	mpp::RenderSystem* renderSystem,
	mpp::ResourceManager* resourceMgr,
	bw::core::World const* world)
	: BatchRenderer()
	, mRenderSystem(renderSystem)
	, mResourceMgr(resourceMgr)
	, mDataProvider(dataProvider)
{
	mBatch = new WorldBatch(
		name,
		textureOrMaterial,
		renderSystem,
		resourceMgr,
		world
	);
}

WorldBatchRenderer::~WorldBatchRenderer()
{
	delete mBatch;
}

mpp::ResourcePtr WorldBatchRenderer::getModel()
{
	return mBatch->getModel();
}

WorldBatch const* WorldBatchRenderer::getWorldBatch() const
{
	return mBatch;
}

void WorldBatchRenderer::create()
{
	mBatch->create();
	update();
}

size_t WorldBatchRenderer::update()
{
	auto worldDataProvider = static_pointer_cast<WorldTriangle3dDataProvider>(mDataProvider);

	auto numVertices = worldDataProvider->getNumVertices();
	auto numPrimitives = worldDataProvider->getNumPrimitives();

	mBatch->startUpdate(numPrimitives, numVertices);

	auto numMeshes = worldDataProvider->getNumMeshes();

	for (uint32_t meshIndex = 0; meshIndex < numMeshes; ++meshIndex)
	{
		auto const& meshData = worldDataProvider->getMeshData(meshIndex);

		if (meshData.numVertices > 0)
		{
			auto vertexBuffer = (int8_t*)mBatch->getAttributeData(meshIndex, "POSITION").first;
			memcpy(vertexBuffer, mDataProvider->getVertexData(meshIndex), mDataProvider->getVertexDataSize(meshIndex));
		}

		if (meshData.numTriangles > 0)
		{
			auto mesh = static_cast<mpp::Model*>(mBatch->getModel().get())->getMesh(meshIndex);
			mesh->setIndexData(mDataProvider->getIndexData(meshIndex), mDataProvider->getNumIndices(meshIndex), mDataProvider->getIndexWidth());
		}
	
		mBatch->finishUpdate(meshIndex, meshData.numTriangles, meshData.numVertices, true);
	}
	
	return mBatch->getCount(0);
}

void WorldBatchRenderer::render()
{
	auto const& model = static_cast<mpp::Model const&>(*mBatch->getModel().get());
	mRenderSystem->renderModelBatched(model, true);
}

