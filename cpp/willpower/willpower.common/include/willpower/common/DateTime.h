#pragma once

#include <chrono>

#include "Platform.h"

namespace WP_NAMESPACE
{

	class WP_COMMON_API TimeSpan
	{
		std::chrono::duration<int32_t, std::ratio<1, 1>> mDuration;

	public:

		TimeSpan(int seconds);

		TimeSpan(int minutes, int seconds);

		TimeSpan(int hours, int minutes, int seconds);
	};

	class WP_COMMON_API DateTime
	{
		std::chrono::system_clock::time_point mTimepoint;

	public:

		enum class Month
		{
			January,
			February,
			March,
			April,
			May,
			June,
			July,
			August,
			September,
			October,
			November,
			December
		};

	public:

		DateTime(int hours, int minutes, int seconds, int day, Month month, int year);
	};

} // WP_NAMESPACE
