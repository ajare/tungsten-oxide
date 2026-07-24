#pragma once

#include "willpower/common/Platform.h"

#define WP_PI			3.141592653f
#define WP_TWOPI		(2 * WP_PI)
#define WP_TAU			WP_TWOPI
#define WP_DEGTORAD(x)	((x) * WP_PI / 180.0f)
#define WP_RADTODEG(x)	((x) * 180.0f / WP_PI)

namespace WP_NAMESPACE
{
	enum Winding
	{
		Unknown,
		Clockwise,
		Anticlockwise
	};
}