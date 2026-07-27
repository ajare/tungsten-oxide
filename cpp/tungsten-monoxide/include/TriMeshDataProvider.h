#pragma once

#include <applib/VisualTriMeshDataProvider.h>

#include "Platform.h"


class TriMeshDataProvider : public applib::VisualTriMeshDataProvider
{
public:

	TriMeshDataProvider(applib::EntityFacade* facade)
		: VisualTriMeshDataProvider(facade)
	{
	}

	void position(uint32_t batch, uint32_t index, float& x0, float& y0, float& x1, float& y1, float& x2, float& y2) override
	{
		VAR_UNUSED(batch);
		VAR_UNUSED(index);

		x0 = 100;
		y0 = 100;

		x1 = 200;
		y1 = 100;

		x2 = 150;
		y2 = 200;
	}

	void texcoords(uint32_t batch, uint32_t index, float& u0, float& v0, float& u1, float& v1, float& u2, float& v2) override
	{
		VAR_UNUSED(batch);
		VAR_UNUSED(index);

		u0 = 0;
		v0 = 0;

		u1 = 1;
		v1 = 0;

		u2 = 1;
		v2 = 1;
	}

	void colour(uint32_t batch, uint32_t index, float& red, float& green, float& blue, float& alpha) override
	{
		VAR_UNUSED(batch);
		VAR_UNUSED(index);

		red = 1;
		green = 1;
		blue = 1;
		alpha = 1;
	}

	mpp::Colour diffuse(uint32_t batch) override
	{
		VAR_UNUSED(batch);

		return mpp::Colour::White;
	}

	virtual bool update(float frameTime)
	{
		VAR_UNUSED(frameTime);

		setNumPrimitives(0);
		return true;
	}

};
