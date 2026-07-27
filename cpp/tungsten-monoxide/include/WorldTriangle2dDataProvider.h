#pragma once

#include <mpp/helper/TriangleBatchDataProvider.h>

#include <willpower/common/AccelerationGrid.h>
#include <willpower/common/BoundingBox.h>

#include <core/World.h>


class WorldTriangle2dDataProvider : public mpp::helper::TriangleBatch2DDataProvider<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeFloat>
{
	std::vector<float> mVertexData;

public:

	WorldTriangle2dDataProvider()
	{
		setNumPrimitives(0);
	}

	void getBounds(glm::vec3& bMin, glm::vec3& bMax) override
	{
		bMin.x = bMin.y = bMin.z = -1e10f;
		bMax.x = bMax.y = bMax.z = 1e10f;
	}

	void position(uint32_t batch, uint32_t index, float& x0, float& y0, float& x1, float& y1, float& x2, float& y2) override
	{
		VAR_UNUSED(batch);

		auto const& vd = &mVertexData[index * 6];

		x0 = vd[0];
		y0 = vd[1];
		x1 = vd[2];
		y1 = vd[3];
		x2 = vd[4];
		y2 = vd[5];
	}

	void texcoords(uint32_t batch, uint32_t index, float& u0, float& v0, float& u1, float& v1, float& u2, float& v2) override
	{
		VAR_UNUSED(batch);
		VAR_UNUSED(index);

		u0 = 0;
		v0 = 0;
		u1 = 1;
		v1 = 1;
		u2 = 1;
		v2 = 0;
	}

	void colour(uint32_t batch, uint32_t index, float& red, float& green, float& blue, float& alpha) override
	{
		VAR_UNUSED(batch);
		VAR_UNUSED(index);

		red = 0;
		green = 0.3f;
		blue = 0.8f;
		alpha = 1;
	}

	mpp::Colour diffuse(uint32_t batch) override
	{
		VAR_UNUSED(batch);

		return mpp::Colour::White;
	}

	bool update(wp::BoundingBox const& viewBounds, bw::core::WorldData const& worldData)
	{
		VAR_UNUSED(viewBounds);

		mVertexData.clear();

		auto const& triangulation = worldData.getTriangulation();

		uint32_t numTriangles = (uint32_t)triangulation.tris.size();
		for (uint32_t i = 0; i < numTriangles; ++i)
		{
			auto const& vertices = triangulation.tris[i].v;

			mVertexData.push_back(vertices[0].p.x);
			mVertexData.push_back(vertices[0].p.y);
			mVertexData.push_back(vertices[1].p.x);
			mVertexData.push_back(vertices[1].p.y);
			mVertexData.push_back(vertices[2].p.x);
			mVertexData.push_back(vertices[2].p.y);
		}

		setNumPrimitives(numTriangles);

		return true;
	}
};