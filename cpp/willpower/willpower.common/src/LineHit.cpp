#include <algorithm>

#include "willpower/common/LineHit.h"

namespace WP_NAMESPACE
{

	LineHit::LineHit()
		: mTime(-1)
		, mPosition(Vector2::ZERO)
		, mNormal(Vector2::ZERO)
		, mTouching(false)
		, mFlags(Flags::None)
	{
	}

	LineHit::LineHit(float time, Vector2 const& position, Vector2 const& normal, bool touching, uint32_t flags)
		: mTime(time)
		, mPosition(position)
		, mNormal(normal)
		, mTouching(touching)
		, mFlags(flags)
	{
	}

	float LineHit::getTime() const
	{
		return mTime;
	}

	Vector2 const& LineHit::getPosition() const
	{
		return mPosition;
	}

	Vector2 const& LineHit::getNormal() const
	{
		return mNormal;
	}

	bool LineHit::isTouching() const
	{
		return mTouching;
	}

	uint32_t LineHit::getFlags() const
	{
		return mFlags;
	}

} // WP_NAMESPACE
