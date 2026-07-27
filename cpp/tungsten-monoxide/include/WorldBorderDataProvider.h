#pragma once

#include <vector>

#include <mpp/helper/LineBatchDataProvider.h>

#include <willpower/common/BoundingBox.h>

#include <core/World.h>
#include <core/WorldData.h>

#include "Platform.h"


class WorldBorderDataProvider : public mpp::helper::LineBatchDataProvider<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeUnsignedByte>
{
	struct Line
	{
		wp::Vector2 v[2];
		uint8_t c[4];
	};

private:

	wp::BoundingBox mBounds;

	std::vector<Line> mLines;

public:

	WorldBorderDataProvider() = default;

	void getBounds(glm::vec3& bMin, glm::vec3& bMax) override
	{
		wp::Vector2 minExtent, maxExtent;

		mBounds.getExtents(minExtent, maxExtent);

		bMin = { minExtent.x, 0.0f, minExtent.y };
		bMax = { maxExtent.x, 0.0f, maxExtent.y };
	}

	void position(uint32_t index, float& x0, float& y0, float& x1, float& y1)
	{
		auto const& line = mLines[index];

		x0 = line.v[0].x;
		y0 = line.v[0].y;
		x1 = line.v[1].x;
		y1 = line.v[1].y;
	}

	void colour(uint32_t index, uint8_t& red, uint8_t& green, uint8_t& blue, uint8_t& alpha)
	{
		auto const& line = mLines[index];

		red = line.c[0];
		green = line.c[1];
		blue = line.c[2];
		alpha = line.c[3];
	}

	mpp::Colour diffuse()
	{
		return mpp::Colour::White;
	}

	bool update(wp::BoundingBox const& viewBounds, bw::core::WorldData const& worldData, int32_t intersectingPolygon)
	{
		VAR_UNUSED(intersectingPolygon);

		mBounds = viewBounds;
		mLines.clear();

		auto const& polygons = worldData.getArrangementPolygons();
		uint32_t numPolygons = (uint32_t)polygons.size();

		for (uint32_t i = 0; i < numPolygons; ++i)
		{
			auto const& polygon = polygons[i];
			auto const& v = polygon.vertices;
			auto nv = (uint32_t)v.size();

			for (uint32_t j = 0; j < nv; ++j)
			{
				auto k = (j + 1) % nv;

				mLines.push_back({ v[j].p, v[k].p, 255, 255, 255, 255 });
			}
		}

		setNumPrimitives(mLines.size());

		return true;
	}
};
