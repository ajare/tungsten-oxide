#pragma once

#include <cstdint>

#include <core/Stats.h>

struct ClippingRecord
{
	uint32_t clippingId;
	double generationStartedTime{ -1.0 }, generationCompleteTime{ -1.0 }, commitedTime{ -1.0 };
	uint64_t generationTimeNs{ 0 };
	bw::core::Stats stats;
};