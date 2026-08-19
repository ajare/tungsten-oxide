#pragma once

#include <array>

#include <willpower/application/resourcesystem/Resource.h>

#include <willpower/common/Vector2.h>
#include <willpower/common/BoundingBox.h>

#include <entt/entt.hpp>

#include "Platform.h"
#include "EntityProperties.h"

namespace applib
{

	struct Entity
	{
		friend class EntityHandler;

		entt::entity mCompSysId;

	private:
		
		static int msEntityIdGen;

	public:

		static const int NumLookups = 32;

	private:

		int mId;

		int mType;

		int mAlive;

	private:

		void setup(int type)
		{
			mId = msEntityIdGen++;
			mType = type;
			mAlive = 1;
		}

		void destroy()
		{
			mAlive = 0;
		}

	public:

		static void _resetIdGenerator()
		{
			msEntityIdGen = 0;
		}

		int getId() const
		{
			return mId;
		}

		int getType() const
		{
			return mType;
		}
	};

} // applib