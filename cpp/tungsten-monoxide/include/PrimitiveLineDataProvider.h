#pragma once

#include <vector>

#include <mpp/helper/LineBatchDataProvider.h>

#include <willpower/common/BoundingBox.h>

#include <core/World.h>

#include "Platform.h"


class PrimitiveLineDataProvider : public mpp::helper::LineBatchDataProvider<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeUnsignedByte>
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

	PrimitiveLineDataProvider()
	{
	}

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

	void addLine(wp::Vector2 const& v0, wp::Vector2 const& v1, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
	{
		mLines.push_back({ v0, v1, r, g, b, a });
		setNumPrimitives((uint32_t)mLines.size());
	}

	bool update(wp::BoundingBox const& viewBounds, bw::core::World* world)
	{
		mBounds = viewBounds;
		mLines.clear();

		auto numPrimitives = world->getNumPrimitives();
		for (uint32_t i = 0; i < numPrimitives; ++i)
		{
			auto const* primitive = world->getPrimitive(i);
			auto complexPolygons = primitive->getVertices();

			for (auto const& complexPolygon : complexPolygons)
			{
				for (auto const& polygon : complexPolygon)
				{
					auto numVertices = (uint32_t)polygon.size();

					for (uint32_t j = 0; j < numVertices; ++j)
					{
						uint32_t k = (j + 1) % numVertices;

						mLines.push_back({
							{ polygon[j].p, polygon[k].p },
							{ 0, 255, 0, 255 }
							});
					}
				}
			}
		}

		setNumPrimitives(mLines.size());
		return true;
	}
};
