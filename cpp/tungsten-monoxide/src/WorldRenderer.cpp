#include <common/GameDefines.h>

#include "WorldRenderer.h"

using namespace std;


WorldRenderer::WorldRenderer(wp::application::resourcesystem::ResourceManager* resourceMgr, wp::Logger* logger)
	: mWorldHasChanged(true)
	, mwLogger(logger)
{
	set<string> materialsFound;

	auto defaultMaterial = resourceMgr->getResource("Material.Default", "World");
	auto dataProvider = make_shared<WorldTriangle3dDataProvider>();
	auto renderer = make_shared<WorldRenderer3d>(defaultMaterial, mwLogger);

	mMaterialRenderers.push_back({ renderer, dataProvider });
}

WorldRenderer::~WorldRenderer()
{
}

void WorldRenderer::create(mpp::ScenePtr scene, bw::core::World const* world, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
{
	for (auto& item : mMaterialRenderers)
	{
		auto& [renderer, dataProvider] = item;

		renderer->create(dataProvider, world, renderSystem, resourceMgr);
		renderer->addToScene(scene, world);
	}
}

void WorldRenderer::setWorldChanged()
{
	mWorldHasChanged = true;
}

void WorldRenderer::addVertexToDataProvider(DataProvider dataProvider, uint32_t meshIndex, float px, float py, float pz, float nx, float ny, float nz, float u, float v, uint32_t c)
{
	auto vertexPtr = dataProvider->nextVertexPtr(meshIndex);

	vertexPtr->pos[0] = px;
	vertexPtr->pos[1] = py;
	vertexPtr->pos[2] = pz;
	vertexPtr->nor[0] = nx;
	vertexPtr->nor[1] = ny;
	vertexPtr->nor[2] = nz;
	vertexPtr->tex[0] = u;
	vertexPtr->tex[1] = v;
	vertexPtr->col = c;
}

void WorldRenderer::updateDataProviders(bw::core::World* world, bw::core::WorldData const& worldData, float frameTime)
{
	const float floorHeightMin{ BW_WORLD_FLOOR_HEIGHT_MIN };
	const float ceilingHeightMax{ BW_WORLD_CRILING_HEIGHT_MAX };

	// Triangles for rendering floor and ceiling
	auto const& triangulation = worldData.getTriangulation();
	auto numTriangles = (uint32_t)triangulation.tris.size();

	// Vertex data for properties
	auto const& vertexData = worldData.getVertexData();

	auto const& graph = worldData.getGraph();
	auto numEdges = (uint32_t)graph.edges.size();

	// Maximum number of floor/ceiling triangles will be 2 per triangle: 1 for floor,
	// once for ceiling

	// Maximum number of wall triangles will be if we assume that each edge requires 4:
	// - Normally a border edge will just need 2 and an internal edge will need 0, 2 or 4.
	auto maxNumTrianglePrimitives = numTriangles * 2 + numEdges * 4;

	// Reset data providers
	for (auto& item : mMaterialRenderers)
	{
		auto& [renderer, dataProvider] = item;
	
		dataProvider->clear();

		
		// Allocate max number of primitives for each data provider, even
		// though it won't all be used.
		dataProvider->updateInternals(maxNumTrianglePrimitives * 3, maxNumTrianglePrimitives);
	}

	// Just hardcode renderer 0 for now
	auto& matRenderer = mMaterialRenderers[0];
	auto& dataProvider = matRenderer.second;

	// Floors & ceilings
	WorldTriangle3dDataProvider::DrawVert* vertexPtr = nullptr;

	for (uint32_t i = 0; i < numTriangles; ++i)
	{
		auto const& v = triangulation.tris[i].v;
		auto const* primitive = world->getPrimitive(triangulation.tris[i].primitiveIndex);
		auto const& primitiveProps = primitive->getProperties();

		wp::Vector2 uv[3] =
		{
			v[0].p / 64.0f,
			v[1].p / 64.0f,
			v[2].p / 64.0f
		};

		// Floor
		auto floorZ = primitiveProps.floorZ;
		auto floorColour = primitiveProps.floorMaterialDef.data.baseColourUint;
		auto floorHash = primitiveProps.floorMaterialDef.data.hash(primitiveProps.floorMaterialIndex);
		auto floorMeshIndex = matRenderer.first->getMeshIndexForMaterialHash(floorHash);

		for (int j = 0; j < 3; ++j)
		{
			addVertexToDataProvider(dataProvider, floorMeshIndex, v[j].p.x, floorZ, v[j].p.y, 0, 1, 0, uv[j].x, uv[j].y, floorColour);
		}

		auto numIndices = (uint16_t)dataProvider->getNumIndices(floorMeshIndex);
		dataProvider->addTriangle(floorMeshIndex, numIndices + 0, numIndices + 1, numIndices + 2);

		// Ceiling
		auto ceilingZ = primitiveProps.ceilingZ;
		auto ceilingColour = primitiveProps.ceilingMaterialDef.data.baseColourUint;
		auto ceilingHash = primitiveProps.ceilingMaterialDef.data.hash(primitiveProps.ceilingMaterialIndex);
		auto ceilingMeshIndex = matRenderer.first->getMeshIndexForMaterialHash(ceilingHash);

		for (int j = 0; j < 3; ++j)
		{
			addVertexToDataProvider(dataProvider, ceilingMeshIndex, v[2 - j].p.x, ceilingZ, v[2 - j].p.y, 0, -1, 0, uv[2 - j].x, uv[2 - j].y, ceilingColour);
		}

		numIndices = (uint16_t)dataProvider->getNumIndices(ceilingMeshIndex);
		dataProvider->addTriangle(ceilingMeshIndex, numIndices + 0, numIndices + 1, numIndices + 2);
	}
	
	// Walls
	for (uint32_t i = 0; i < numEdges; ++i)
	{
		auto const& edge = graph.edges[i];

		float y0, y1;
		bw::core::PrimitivePropertySet wallProps;

		auto const& v0 = wp::Vector2(graph.vertices[edge.v[0]].x, graph.vertices[edge.v[0]].y);
		auto const& v1 = wp::Vector2(graph.vertices[edge.v[1]].x, graph.vertices[edge.v[1]].y);
		auto l = v1 - v0;
		auto n = l.normalisedCopy().perpendicular();

		if (edge.is2Sided())
		{
			auto prim0 = world->getPrimitive(edge.p[0]);
			auto prim1 = world->getPrimitive(edge.p[1]);
			auto const& primProps0 = prim0->getProperties();
			auto const& primProps1 = prim1->getProperties();

			if (primProps0.floorZ != primProps1.floorZ)
			{
				// Material etc should be of the lower one
				if (primProps0.floorZ < primProps1.floorZ)
				{
					y0 = primProps0.floorZ;
					y1 = primProps1.floorZ;
					wallProps = primProps0;
				}
				else
				{
					y0 = primProps1.floorZ;
					y1 = primProps0.floorZ;
					wallProps = primProps1;
				}

				auto wallColour = wallProps.wallMaterialDef.data.baseColourUint;

				// Hash the relevant material def and then get the mesh index
				auto wallHash = wallProps.wallMaterialDef.data.hash(wallProps.wallMaterialIndex);
				auto wallMeshIndex = matRenderer.first->getMeshIndexForMaterialHash(wallHash);

				auto& dp = matRenderer.second;
				auto numIndices = (uint16_t)dp->getNumIndices(wallMeshIndex);

				// Down from floor
				addVertexToDataProvider(dp, wallMeshIndex, v0.x, y0, v0.y, n.x, 0, n.y, 0, 0, wallColour);
				addVertexToDataProvider(dp, wallMeshIndex, v1.x, y0, v1.y, n.x, 0, n.y, 0, 0, wallColour);
				addVertexToDataProvider(dp, wallMeshIndex, v1.x, y1, v1.y, n.x, 0, n.y, 0, 0, wallColour);

				addVertexToDataProvider(dp, wallMeshIndex, v1.x, y1, v1.y, n.x, 0, n.y, 0, 0, wallColour);
				addVertexToDataProvider(dp, wallMeshIndex, v0.x, y1, v0.y, n.x, 0, n.y, 0, 0, wallColour);
				addVertexToDataProvider(dp, wallMeshIndex, v0.x, y0, v0.y, n.x, 0, n.y, 0, 0, wallColour);

				dp->addTriangle(wallMeshIndex, numIndices + 0, numIndices + 1, numIndices + 2);
				dp->addTriangle(wallMeshIndex, numIndices + 3, numIndices + 4, numIndices + 5);
				numIndices += 6;
			}

			if (primProps0.ceilingZ != primProps1.ceilingZ)
			{
				// Material etc should be of the higher one
				if (primProps0.ceilingZ > primProps1.ceilingZ)
				{
					y0 = primProps1.ceilingZ;
					y1 = primProps0.ceilingZ;
					wallProps = primProps0;
				}
				else
				{
					y0 = primProps0.ceilingZ;
					y1 = primProps1.ceilingZ;
					wallProps = primProps1;
				}

				auto wallColour = wallProps.wallMaterialDef.data.baseColourUint;

				// Hash the relevant material def and then get the mesh index
				auto wallHash = wallProps.wallMaterialDef.data.hash(wallProps.wallMaterialIndex);
				auto wallMeshIndex = matRenderer.first->getMeshIndexForMaterialHash(wallHash);

				auto& dp = matRenderer.second;
				auto numIndices = (uint16_t)dp->getNumIndices(wallMeshIndex);

				// Up to ceiling
				addVertexToDataProvider(dp, wallMeshIndex, v0.x, y0, v0.y, n.x, 0, n.y, 0, 0, wallColour);
				addVertexToDataProvider(dp, wallMeshIndex, v1.x, y0, v1.y, n.x, 0, n.y, 0, 0, wallColour);
				addVertexToDataProvider(dp, wallMeshIndex, v1.x, y1, v1.y, n.x, 0, n.y, 0, 0, wallColour);

				addVertexToDataProvider(dp, wallMeshIndex, v1.x, y1, v1.y, n.x, 0, n.y, 0, 0, wallColour);
				addVertexToDataProvider(dp, wallMeshIndex, v0.x, y1, v0.y, n.x, 0, n.y, 0, 0, wallColour);
				addVertexToDataProvider(dp, wallMeshIndex, v0.x, y0, v0.y, n.x, 0, n.y, 0, 0, wallColour);

				dataProvider->addTriangle(wallMeshIndex, numIndices + 0, numIndices + 1, numIndices + 2);
				dataProvider->addTriangle(wallMeshIndex, numIndices + 3, numIndices + 4, numIndices + 5);
				numIndices += 6;
			}
		}
		else
		{
			// Wall properties should be taken from vertex
			auto vIndex = BW_VERTEX_Z_UNPACK_VERTEX_INDEX(graph.vertices[edge.v[0]].z);
			wallProps = vertexData[vIndex].properties[1];

			y0 = wallProps.floorZ;
			y1 = wallProps.ceilingZ;
			auto wallColour = wallProps.wallMaterialDef.data.baseColourUint;

			// Hash the relevant material def and then get the mesh index
			auto wallHash = wallProps.wallMaterialDef.data.hash(wallProps.wallMaterialIndex);
			auto wallMeshIndex = matRenderer.first->getMeshIndexForMaterialHash(wallHash);

			auto& dp = matRenderer.second;
			auto numIndices = (uint16_t)dp->getNumIndices(wallMeshIndex);

			addVertexToDataProvider(dp, wallMeshIndex, v0.x, y0, v0.y, n.x, 0, n.y, 0, 0, wallColour);
			addVertexToDataProvider(dp, wallMeshIndex, v1.x, y0, v1.y, n.x, 0, n.y, 0, 0, wallColour);
			addVertexToDataProvider(dp, wallMeshIndex, v1.x, y1, v1.y, n.x, 0, n.y, 0, 0, wallColour);

			addVertexToDataProvider(dp, wallMeshIndex, v1.x, y1, v1.y, n.x, 0, n.y, 0, 0, wallColour);
			addVertexToDataProvider(dp, wallMeshIndex, v0.x, y1, v0.y, n.x, 0, n.y, 0, 0, wallColour);
			addVertexToDataProvider(dp, wallMeshIndex, v0.x, y0, v0.y, n.x, 0, n.y, 0, 0, wallColour);

			dataProvider->addTriangle(wallMeshIndex, numIndices + 0, numIndices + 1, numIndices + 2);
			dataProvider->addTriangle(wallMeshIndex, numIndices + 3, numIndices + 4, numIndices + 5);
			numIndices += 6;
		}
	}

	for (auto& item : mMaterialRenderers)
	{
		auto& [renderer, dp] = item;

		dp->setNumPrimitives(dp->getNumTriangles());
	}
}

void WorldRenderer::update(bw::core::World* world, bw::core::WorldData const& worldData, float frameTime)
{
	if (mWorldHasChanged)
	{
		updateDataProviders(world, worldData, frameTime);
		mWorldHasChanged = false;
	}

	for (auto& item : mMaterialRenderers)
	{
		auto& [renderer, dataProvider] = item;

		// Update renderer
		renderer->update(frameTime);
	}
}