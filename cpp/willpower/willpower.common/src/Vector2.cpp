#include "willpower/common/Vector2.h"

namespace WP_NAMESPACE
{

	const Vector2 Vector2::ZERO(0, 0);
	const Vector2 Vector2::UNIT_X(1, 0);
	const Vector2 Vector2::UNIT_Y(0, 1);
	const Vector2 Vector2::NEGATIVE_UNIT_X(-1, 0);
	const Vector2 Vector2::NEGATIVE_UNIT_Y(0, -1);

	Vector2 operator*(float value, Vector2 const& vec)
	{
		return vec * value;
	}

} // WP_NAMESPACE
