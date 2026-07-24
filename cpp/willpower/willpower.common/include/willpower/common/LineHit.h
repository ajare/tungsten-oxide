#pragma once

#include "willpower/common/Vector2.h"

namespace WP_NAMESPACE
{

	class WP_COMMON_API LineHit
	{
	public:

		enum Flags
		{
			None,
			HitEnters = 1,
			HitExits = 2
		};

	private:

		float mTime;

		Vector2 mPosition, mNormal;

		bool mTouching;
		
		uint32_t mFlags;

	public:

		LineHit();

		LineHit(float time, Vector2 const& position, Vector2 const& normal, bool touching, uint32_t flags = Flags::None);

		float getTime() const;

		Vector2 const& getPosition() const;

		Vector2 const& getNormal() const;

		bool isTouching() const;

		uint32_t getFlags() const;
	};

} // WP_NAMESPACE
