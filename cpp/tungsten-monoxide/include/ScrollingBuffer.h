#pragma once

#include <array>

#include "imgui/imgui.h"


template<size_t C>
struct ScrollingBuffer
{
	int MaxSize;
	int Offset;
	float CurMin, CurMax;
	ImVector<std::array<float, C + 1>> Data;

	ScrollingBuffer(int max_size = 2000)
	{
		MaxSize = max_size;
		Offset = 0;
		CurMin = 0;
		CurMax = 0;
		Data.reserve(MaxSize);
	}

	void addPoint(std::array<float, C + 1> const& value)
	{
		if (Data.size() < MaxSize)
		{
			Data.push_back(value);
		}
		else
		{
			Data[Offset] = value;
			Offset = (Offset + 1) % MaxSize;
		}

		/*
		if (y > CurMax)
		{
			CurMax = y;
		}
		if (y < CurMin)
		{
			CurMin = y;
		}
		*/
	}

	void erase()
	{
		if (Data.size() > 0)
		{
			Data.shrink(0);
			Offset = 0;
		}

		CurMin = 0;
		CurMax = 0;
	}
};